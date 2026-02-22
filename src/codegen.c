#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/main.h"

// ─────────────────────────────────────────
// ASM hot path functions (codegen_asm.asm)
// ─────────────────────────────────────────
extern void buf_write_str(char *buf, size_t *cursor, const char *str);
extern void buf_write_int(char *buf, size_t *cursor, long val);
extern int  buf_flush(char *buf, size_t len, const char *filename);

// ─────────────────────────────────────────
// Output buffer — 4MB
// String buffer — 64KB for .data string literals
// ─────────────────────────────────────────
#define OUT_BUF_SIZE (4 * 1024 * 1024)
#define STR_BUF_SIZE (64 * 1024)

static char   out_buf[OUT_BUF_SIZE];
static size_t out_cursor = 0;
static char   str_buf[STR_BUF_SIZE];
static size_t str_cursor = 0;

// ─────────────────────────────────────────
// Emit helpers
// ─────────────────────────────────────────
static void emit_str(const char *s) {
    buf_write_str(out_buf, &out_cursor, s);
}
static void emit(const char *s) {
    buf_write_str(out_buf, &out_cursor, "    ");
    buf_write_str(out_buf, &out_cursor, s);
}
static void emitln(const char *s) {
    buf_write_str(out_buf, &out_cursor, "    ");
    buf_write_str(out_buf, &out_cursor, s);
    buf_write_str(out_buf, &out_cursor, "\n");
}
static void emit_label(int id) {
    buf_write_str(out_buf, &out_cursor, ".L");
    buf_write_int(out_buf, &out_cursor, id);
    buf_write_str(out_buf, &out_cursor, ":\n");
}
static void emit_named_label(const char *name) {
    buf_write_str(out_buf, &out_cursor, name);
    buf_write_str(out_buf, &out_cursor, ":\n");
}
static void emit_jmp(const char *instr, int id) {
    buf_write_str(out_buf, &out_cursor, "    ");
    buf_write_str(out_buf, &out_cursor, instr);
    buf_write_str(out_buf, &out_cursor, " .L");
    buf_write_int(out_buf, &out_cursor, id);
    buf_write_str(out_buf, &out_cursor, "\n");
}

// ─────────────────────────────────────────
// State
// ─────────────────────────────────────────
static int label_count = 0;
static int str_count   = 0;

typedef struct { char name[64]; int offset; int array_size; char struct_type[64]; DataType dtype; int owned; } Var;
static Var var_table[256];
static int var_count  = 0;
static int stack_top  = 0;
static Var param_table[64];
static int param_count = 0;

static int new_label() { return label_count++; }

static int var_offset(const char *name) {
    for (int i = var_count - 1; i >= 0; i--)
        if (strcmp(var_table[i].name, name) == 0) return var_table[i].offset;
    for (int i = param_count - 1; i >= 0; i--)
        if (strcmp(param_table[i].name, name) == 0) return param_table[i].offset;
    fprintf(stderr, "Codegen error: undefined variable '%s'\n", name);
    exit(1);
}

static DataType var_dtype(const char *name) {
    for (int i = var_count - 1; i >= 0; i--)
        if (strcmp(var_table[i].name, name) == 0) return var_table[i].dtype;
    for (int i = param_count - 1; i >= 0; i--)
        if (strcmp(param_table[i].name, name) == 0) return param_table[i].dtype;
    return DTYPE_INT;
}

static int add_var(const char *name, DataType dtype) {
    stack_top += 8;
    var_table[var_count].offset     = stack_top;
    var_table[var_count].dtype      = dtype;
    var_table[var_count].array_size = 0;
    var_table[var_count].struct_type[0] = 0;
    strncpy(var_table[var_count].name, name, 63);
    var_count++;
    return stack_top;
}

// allocate N*8 bytes on stack for array, return offset of element [0]
static int add_var_array(const char *name, DataType dtype, int size) {
    int base   = stack_top + 8;
    stack_top += size * 8;
    var_table[var_count].offset     = base;
    var_table[var_count].dtype      = dtype;
    var_table[var_count].array_size = size;
    var_table[var_count].struct_type[0] = 0;
    strncpy(var_table[var_count].name, name, 63);
    var_count++;
    return base;
}

// allocate field_count*8 bytes for struct, record struct type name
static int add_var_struct(const char *name, const char *stype, int field_count) {
    int base   = stack_top + 8;
    stack_top += field_count * 8;
    var_table[var_count].offset     = base;
    var_table[var_count].dtype      = DTYPE_STRUCT;
    var_table[var_count].array_size = field_count;
    strncpy(var_table[var_count].struct_type, stype, 63);
    strncpy(var_table[var_count].name, name, 63);
    var_count++;
    return base;
}

static const char *var_struct_type(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(var_table[i].name, name) == 0)
            return var_table[i].struct_type;
    return "";
}

// ─────────────────────────────────────────
// Count local variables for exact stack sizing
// ─────────────────────────────────────────
static int count_vars(Node *n) {
    if (!n) return 0;
    int count = 0;
    if (n->type == NODE_ASSIGN) {
        // struct assign takes field_count slots
        if (n->right && n->right->type == NODE_STRUCT_INIT) {
            StructDef *sd = find_struct(n->right->name);
            count = sd ? sd->field_count : 1;
        } else {
            count = 1;
        }
    }
    if (n->type == NODE_ARRAY_DECL)  count = n->array_size;
    if (n->type == NODE_ARRAY_INIT)  count = 0;
    if (n->type == NODE_STRUCT_DEF)  count = 0;  // no stack space
    if (n->type == NODE_ASSIGN_MULTI) count = 2;
    if (n->type == NODE_FOR)         count = 1;
    if (n->type == NODE_FOR_IF)      count = 1;
    if (n->left)  count += count_vars(n->left);
    if (n->right) count += count_vars(n->right);
    for (int i = 0; i < n->child_count; i++)
        count += count_vars(n->children[i]);
    return count;
}

// ─────────────────────────────────────────
// Emit integer comparison
// expects: rax = left, rbx = right
// ─────────────────────────────────────────
static void emit_cmp(const char *op) {
    emitln("cmp rax, rbx");
    if      (strcmp(op, ">")  == 0) emitln("setg  al");
    else if (strcmp(op, "<")  == 0) emitln("setl  al");
    else if (strcmp(op, "==") == 0) emitln("sete  al");
    else if (strcmp(op, "!=") == 0) emitln("setne al");
    else if (strcmp(op, ">=") == 0) emitln("setge al");
    else if (strcmp(op, "<=") == 0) emitln("setle al");
    emitln("movzx rax, al");
}

// CSE cache — stores recently computed binop expressions
typedef struct {
    char lhs[64];   // left operand name
    char rhs[64];   // right operand name  
    char op[4];     // operator
    int  slot;      // r11=0, r12 already used by for, use r11
} CSEEntry;
static CSEEntry cse_cache[32];
static int      cse_count = 0;

static void cse_clear() { cse_count = 0; }

static int cse_lookup(const char *lhs, const char *op, const char *rhs) {
    for (int i = 0; i < cse_count; i++)
        if (strcmp(cse_cache[i].lhs, lhs) == 0 &&
            strcmp(cse_cache[i].op,  op)  == 0 &&
            strcmp(cse_cache[i].rhs, rhs) == 0)
            return i;
    return -1;
}

static void cse_store(const char *lhs, const char *op, const char *rhs) {
    if (cse_count >= 32) return;
    strncpy(cse_cache[cse_count].lhs, lhs, 63);
    strncpy(cse_cache[cse_count].op,  op,  3);
    strncpy(cse_cache[cse_count].rhs, rhs, 63);
    cse_count++;
}

// ─────────────────────────────────────────
// Linear Scan Register Allocator
// Based on Poletto & Sarkar 1999
// ─────────────────────────────────────────
#define MAX_REGS 2
static const char *alloc_regs[MAX_REGS] = {"r12", "r13"};

typedef struct {
    char name[64];  // variable name
    int  start;     // first use (instruction index)
    int  end;       // last use (instruction index)
    int  reg;       // assigned register index (-1 = spilled to RAM)
} LiveInterval;

static LiveInterval intervals[256];
static int          interval_count = 0;
static int          reg_free[MAX_REGS];   // 1 = free, 0 = in use
static char         reg_owner[MAX_REGS][64]; // which var owns this reg

static void regalloc_clear() {
    interval_count = 0;
    for (int i = 0; i < MAX_REGS; i++) {
        reg_free[i] = 1;
        reg_owner[i][0] = 0;
    }
}

// find which register a variable is in (-1 = not in register)
static int regalloc_find(const char *name) {
    for (int i = 0; i < MAX_REGS; i++)
        if (!reg_free[i] && strcmp(reg_owner[i], name) == 0)
            return i;
    return -1;
}

// assign a register to a variable — returns reg index or -1 if none free
static int regalloc_assign(const char *name) {
    // already assigned?
    int existing = regalloc_find(name);
    if (existing >= 0) return existing;
    // find free register
    for (int i = 0; i < MAX_REGS; i++) {
        if (reg_free[i]) {
            reg_free[i] = 0;
            strncpy(reg_owner[i], name, 63);
            return i;
        }
    }
    return -1;  // no register free — spill
}

// free a register when variable goes out of use
static void regalloc_free(const char *name) {
    for (int i = 0; i < MAX_REGS; i++) {
        if (!reg_free[i] && strcmp(reg_owner[i], name) == 0) {
            reg_free[i] = 1;
            reg_owner[i][0] = 0;
            return;
        }
    }
}

// break/continue label stack
#define MAX_LOOP_DEPTH 32
static int break_stack[MAX_LOOP_DEPTH];
static int continue_stack[MAX_LOOP_DEPTH];
static int loop_depth = 0;

static void loop_push(int brk, int cont) {
    break_stack[loop_depth]    = brk;
    continue_stack[loop_depth] = cont;
    loop_depth++;
}
static void loop_pop() { loop_depth--; }
static int  loop_break()    { return break_stack[loop_depth - 1]; }
static int  loop_continue() { return continue_stack[loop_depth - 1]; }



// forward declaration
static int node_uses_var(Node *n, const char *varname);

// returns 1 if node accesses an array using varname as index
static int block_accesses_array(Node *n, const char *varname) {
    if (!n) return 0;
    if (n->type == NODE_ARRAY_ACCESS && node_uses_var(n->left, varname)) return 1;
    if (n->type == NODE_ARRAY_ASSIGN && node_uses_var(n->left, varname)) return 1;
    if (block_accesses_array(n->left,  varname)) return 1;
    if (block_accesses_array(n->right, varname)) return 1;
    for (int i = 0; i < n->child_count; i++)
        if (block_accesses_array(n->children[i], varname)) return 1;
    return 0;
}

// returns loop range if known at compile time, -1 if not
static int get_loop_range(Node *limit, Node *start) {
    if (limit->type == NODE_NUMBER && start->type == NODE_NUMBER)
        return limit->ival - start->ival;
    return -1;
}



// ─────────────────────────────────────────
// Code generation
// ─────────────────────────────────────────
static void gen_expr(Node *n);
static void gen_stmt(Node *n);

static void gen_expr(Node *n) {
    switch (n->type) {

    // ── integer literal ──
    case NODE_NUMBER:
        emit("mov rax, ");
        buf_write_int(out_buf, &out_cursor, n->ival);
        buf_write_str(out_buf, &out_cursor, "\n");
        break;

    // ── bool literal (true=1, false=0) ──
    case NODE_BOOL:
        emit("mov rax, ");
        buf_write_int(out_buf, &out_cursor, n->ival);
        buf_write_str(out_buf, &out_cursor, "\n");
        break;

    // ── variable load ──
    case NODE_IDENT: {
        int off      = var_offset(n->name);
        DataType dt  = var_dtype(n->name);
        n->dtype     = dt;
        // check if variable lives in a register
        int reg = regalloc_find(n->name);
        if (reg >= 0 && dt == DTYPE_INT) {
            emit("mov rax, ");
            buf_write_str(out_buf, &out_cursor, alloc_regs[reg]);
            buf_write_str(out_buf, &out_cursor, "\n");
            break;
        }
        if (dt == DTYPE_FLOAT) {
            // load float into xmm0, then transfer bits to rax for uniform handling
            emit("movsd xmm0, [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emitln("movq rax, xmm0");
        } else if (dt == DTYPE_BOOL) {
            // load 1 byte, zero-extend into rax
            emitln("xor rax, rax");
            emit("mov al, byte [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
        } else {
            emit("mov rax, [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
        }
        break;
    }

    // ── string literal ──
    case NODE_STRING: {
        // record string in str_buf → str0 db "hello", 10, 0
        int sid = str_count++;
        buf_write_str(str_buf, &str_cursor, "    str");
        buf_write_int(str_buf, &str_cursor, sid);
        buf_write_str(str_buf, &str_cursor, " db \"");
        buf_write_str(str_buf, &str_cursor, n->sval);
        buf_write_str(str_buf, &str_cursor, "\", 0\n");
        // load address into rax
        emit("lea rax, [rel str");
        buf_write_int(out_buf, &out_cursor, sid);
        buf_write_str(out_buf, &out_cursor, "]\n");
        n->dtype = DTYPE_STR;
        break;
    }

    // ── array element read: nums[i] ──
    // address of nums[i] = rbp - (base + i*8)
    case NODE_ARRAY_ACCESS: {
        int base = var_offset(n->name);
        DataType dt = var_dtype(n->name);
        if (dt == DTYPE_PTR) {
            // pointer indexing: ptr[i] = *(ptr + i*8)
            emit("mov rax, [rbp-");
            buf_write_int(out_buf, &out_cursor, base);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emitln("push rax");         // save ptr
            gen_expr(n->left);          // index → rax
            emitln("shl rax, 3");       // i * 8 = i << 3     
            emitln("pop rbx");          // ptr → rbx
            emitln("add rbx, rax");     // ptr + i*8
            emitln("mov rax, [rbx]");   // load value
        } else {
            // stack array indexing
            gen_expr(n->left);
            emitln("shl rax, 3");
            emitln("neg rax");
            emit("add rax, ");
            buf_write_str(out_buf, &out_cursor, "qword -");
            buf_write_int(out_buf, &out_cursor, base);
            buf_write_str(out_buf, &out_cursor, "\n");
            emitln("add rax, rbp");
            emitln("mov rax, [rax]");
        }
        n->dtype = var_dtype(n->name);
        break;
    }

    // ── struct field read: p.field ──
    // field address = rbp - (var_base + field_offset)
    case NODE_FIELD_ACCESS: {
        int base          = var_offset(n->name);
        const char *stype = var_struct_type(n->name);
        StructDef  *sd    = find_struct(stype);
        if (!sd) {
            fprintf(stderr, "Codegen error: '%s' is not a struct\n", n->name);
            exit(1);
        }
        int foff = 0;
        DataType ftype = DTYPE_INT;
        if (!find_field(sd, n->sval, &foff, &ftype)) {
            fprintf(stderr, "Codegen error: struct '%s' has no field '%s'\n", stype, n->sval);
            exit(1);
        }
        emit("mov rax, [rbp-");
        buf_write_int(out_buf, &out_cursor, base + foff);
        buf_write_str(out_buf, &out_cursor, "]\n");
        n->dtype = ftype;
        break;
    }

    // ── binary operation ──
    case NODE_BINOP: {
        // CSE — check if both sides are simple idents and we've seen this before
        char lhs[64] = "", rhs[64] = "";
        int use_cse = 0;
        if (n->left->type  == NODE_IDENT) strncpy(lhs, n->left->name,  63);
        if (n->right->type == NODE_IDENT) strncpy(rhs, n->right->name, 63);
        if (lhs[0] && rhs[0]) {
            int hit = cse_lookup(lhs, n->op, rhs);
            if (hit >= 0) {
                // reuse cached result from r11
                emitln("mov rax, r11");
                use_cse = 1;
            }
        }
        if (!use_cse) {
            gen_expr(n->left);
            if (n->right->type == NODE_BINOP || n->right->type == NODE_FN_CALL) {
                emitln("push rax");
                gen_expr(n->right);
                emitln("mov rbx, rax");
                emitln("pop rax");
            } else {
                emitln("mov r10, rax");
                gen_expr(n->right);
                emitln("mov rbx, rax");
                emitln("mov rax, r10");
            }
            if      (strcmp(n->op, "+") == 0) emitln("add rax, rbx");
            else if (strcmp(n->op, "-") == 0) emitln("sub rax, rbx");
            else if (strcmp(n->op, "*") == 0) {
                if (n->right->type == NODE_NUMBER) {
                    int val = n->right->ival;
                    if      (val == 2)  emitln("shl rax, 1");
                    else if (val == 4)  emitln("shl rax, 2");
                    else if (val == 8)  emitln("shl rax, 3");
                    else if (val == 16) emitln("shl rax, 4");
                    else if (val == 32) emitln("shl rax, 5");
                    else if (val == 64) emitln("shl rax, 6");
                    else emitln("imul rax, rbx");
                } else {
                    emitln("imul rax, rbx");
                }
            }
            else if (strcmp(n->op, "/") == 0) { emitln("xor rdx, rdx"); emitln("idiv rbx"); }
            else emit_cmp(n->op);
            // cache this result in r11 if both sides were simple idents
            if (lhs[0] && rhs[0]) {
                emitln("mov r11, rax");
                cse_store(lhs, n->op, rhs);
            }
        }
        break;
    }

    // ── function call ──
    case NODE_FN_CALL: {
        const char *arg_regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        for (int i = 0; i < n->child_count; i++) {
            gen_expr(n->children[i]);
            emitln("push rax");
        }
        for (int i = n->child_count - 1; i >= 0; i--) {
            emit("pop ");
            buf_write_str(out_buf, &out_cursor, arg_regs[i]);
            buf_write_str(out_buf, &out_cursor, "\n");
        }
        emit("call ");
        buf_write_str(out_buf, &out_cursor, n->name);
        buf_write_str(out_buf, &out_cursor, "\n");
        break;
    }

// addr(x) — load address of variable into rax
    case NODE_ADDR: {
        int off = var_offset(n->name);
        emit("lea rax, [rbp-");
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "]\n");
        n->dtype = DTYPE_PTR;
        break;
    }

    // deref(p) — read value from address stored in variable
    case NODE_DEREF: {
        int off = var_offset(n->name);
        emit("mov rax, [rbp-");
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "]\n");
        emitln("mov rax, [rax]");
        n->dtype = DTYPE_INT;
        break;
    }

// alloc(size) — mmap syscall
    // syscall 9 = mmap(addr=0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
    case NODE_ALLOC: {
        gen_expr(n->left);              // size → rax
        emitln("mov rdi, 0");           // addr = 0 (kernel chooses)
        emitln("mov rsi, rax");         // size
        emitln("mov rdx, 3");           // PROT_READ | PROT_WRITE
        emitln("mov r10, 34");          // MAP_PRIVATE | MAP_ANONYMOUS
        emitln("mov r8, -1");           // fd = -1
        emitln("mov r9, 0");            // offset = 0
        emitln("mov rax, 9");           // syscall 9 = mmap
        emitln("syscall");              // rax = pointer to memory
        n->dtype = DTYPE_PTR;
        break;
    }

// open(filename, flags) — syscall 2
    case NODE_OPEN: {
        gen_expr(n->left);              // filename → rax
        emitln("mov rdi, rax");         // filename
        gen_expr(n->right);             // flags → rax
        emitln("mov rsi, rax");         // flags
        emitln("mov rdx, 0");           // mode = 0
        emitln("mov rax, 2");           // syscall 2 = open
        emitln("syscall");              // rax = fd
        n->dtype = DTYPE_INT;
        break;
    }

    case NODE_AND: {
        // short circuit: if left is 0, result is 0
        int lbl_false = new_label();
        int lbl_done  = new_label();
        gen_expr(n->left);
        emitln("test rax, rax");
        emit_jmp("jz", lbl_false);
        gen_expr(n->right);
        emitln("test rax, rax");
        emit_jmp("jz", lbl_false);
        emitln("mov rax, 1");
        emit_jmp("jmp", lbl_done);
        emit_label(lbl_false);
        emitln("mov rax, 0");
        emit_label(lbl_done);
        break;
    }

    case NODE_OR: {
        // short circuit: if left is 1, result is 1
        int lbl_true = new_label();
        int lbl_done = new_label();
        gen_expr(n->left);
        emitln("test rax, rax");
        emit_jmp("jnz", lbl_true);
        gen_expr(n->right);
        emitln("test rax, rax");
        emit_jmp("jnz", lbl_true);
        emitln("mov rax, 0");
        emit_jmp("jmp", lbl_done);
        emit_label(lbl_true);
        emitln("mov rax, 1");
        emit_label(lbl_done);
        break;
    }

    case NODE_NEG:
        gen_expr(n->right);
        emitln("neg rax");
        break;

    case NODE_STRLEN:
        gen_expr(n->right);             // string address → rax
        emitln("mov rdi, rax");
        emitln("call strlen");          // strlen(str) → rax
        break;

    default:
        fprintf(stderr, "Codegen error: unexpected node in expression\n");
        exit(1);
    }
}

static void emit_auto_free() {
    for (int i = 0; i < var_count; i++) {
        if (var_table[i].dtype == DTYPE_PTR && var_table[i].owned) {
            emit("mov rdi, [rbp-");
            buf_write_int(out_buf, &out_cursor, var_table[i].offset);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emitln("mov rsi, 1024");    // size — we store this later
            emitln("mov rax, 11");      // munmap
            emitln("syscall");
        }
    }
}


// returns 1 if node tree references variable name
static int node_uses_var(Node *n, const char *varname) {
    if (!n) return 0;
    if (n->type == NODE_IDENT && strcmp(n->name, varname) == 0) return 1;
    if (node_uses_var(n->left,  varname)) return 1;
    if (node_uses_var(n->right, varname)) return 1;
    for (int i = 0; i < n->child_count; i++)
        if (node_uses_var(n->children[i], varname)) return 1;
    return 0;
}

static void gen_stmt(Node *n) {
    switch (n->type) {

    case NODE_BLOCK:
        for (int i = 0; i < n->child_count; i++) gen_stmt(n->children[i]);
        break;

    // ── array declaration: let nums: int[5] ──
    // reserves N*8 bytes on stack, no initialisation
    case NODE_ARRAY_DECL:
        add_var_array(n->name, n->dtype, n->array_size);
        break;

    // ── array inline initialiser: {1, 2, 3, 4} ──
    // array must already be declared (NODE_ARRAY_DECL emitted first via block)
    // emits a store for each element value
    case NODE_ARRAY_INIT: {
        int base = var_offset(n->name);
        for (int i = 0; i < n->child_count; i++) {
            gen_expr(n->children[i]);           // value → rax
            emit("mov qword [rbp-");
            buf_write_int(out_buf, &out_cursor, base + i * 8);
            buf_write_str(out_buf, &out_cursor, "], rax\n");
        }
        break;
    }

    // ── array element write: nums[i] = val ──
    // address = rbp - (base + i*8)
    case NODE_ARRAY_ASSIGN: {
        int base = var_offset(n->name);
        DataType dt = var_dtype(n->name);
        if (dt == DTYPE_PTR) {
            // pointer indexing write: ptr[i] = val
            gen_expr(n->right);             // value → rax
            emitln("push rax");             // save value
            emit("mov rax, [rbp-");         // load ptr
            buf_write_int(out_buf, &out_cursor, base);
            buf_write_str(out_buf, &out_cursor, "]\n");
            gen_expr(n->left);              // index → rax — WRONG, clobbers ptr
            emitln("push rax");             // save index
            emit("mov rax, [rbp-");         // reload ptr
            buf_write_int(out_buf, &out_cursor, base);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emitln("pop rcx");              // index → rcx
            emitln("shl rcx, 3");           // i * 8 = i << 3
            emitln("add rax, rcx");         // ptr + i*8
            emitln("pop rbx");              // value → rbx
            emitln("mov [rax], rbx");       // store
        } else {
            // stack array write
            gen_expr(n->right);
            emitln("push rax");
            gen_expr(n->left);
            emitln("shl rax, 3");
            emitln("neg rax");
            emit("add rax, qword -");
            buf_write_int(out_buf, &out_cursor, base);
            buf_write_str(out_buf, &out_cursor, "\n");
            emitln("add rax, rbp");
            emitln("pop rbx");
            emitln("mov [rax], rbx");
        }
        break;
    }

    // ── struct definition — no code emitted, already registered in parser ──
    case NODE_STRUCT_DEF:
        break;

    // ── struct initialisation: let p = Point(10, 20) ──
    // allocates field_count slots, stores each arg into successive fields
    case NODE_ASSIGN: {
        if (n->right && n->right->type == NODE_STRUCT_INIT) {
            StructDef *sd = find_struct(n->right->name);
            if (!sd) {
                fprintf(stderr, "Codegen error: unknown struct '%s'\n", n->right->name);
                exit(1);
            }
            int base = add_var_struct(n->name, n->right->name, sd->field_count);
            for (int i = 0; i < n->right->child_count && i < sd->field_count; i++) {
                gen_expr(n->right->children[i]);
                emit("mov [rbp-");
                buf_write_int(out_buf, &out_cursor, base + sd->fields[i].offset);
                buf_write_str(out_buf, &out_cursor, "], rax\n");
            }
            break;
        }
        // regular variable assignment (int / float / bool / str)
        int off = add_var(n->name, n->dtype);
        if (n->dtype == DTYPE_FLOAT) {
            if (n->right->type == NODE_NUMBER) {
                emit("mov rax, ");
                buf_write_int(out_buf, &out_cursor, n->right->ival);
                buf_write_str(out_buf, &out_cursor, "\n");
                emitln("cvtsi2sd xmm0, rax");
            } else {
                gen_expr(n->right);
                emitln("movq xmm0, rax");
            }
            emit("movsd [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], xmm0\n");
        } else if (n->dtype == DTYPE_BOOL) {
            gen_expr(n->right);
            emit("mov byte [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], al\n");
          } else {
            gen_expr(n->right);
            emit("mov [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], rax\n");
            // mark as owned if allocated with alloc
            if (n->right->type == NODE_ALLOC)
                var_table[var_count - 1].owned = 1;
        }
        break;
    }

    // ── field assignment: p.field = val ──
    case NODE_FIELD_ASSIGN: {
        int base          = var_offset(n->name);
        const char *stype = var_struct_type(n->name);
        StructDef  *sd    = find_struct(stype);
        if (!sd) {
            fprintf(stderr, "Codegen error: '%s' is not a struct\n", n->name);
            exit(1);
        }
        int foff = 0;
        DataType ftype = DTYPE_INT;
        if (!find_field(sd, n->sval, &foff, &ftype)) {
            fprintf(stderr, "Codegen error: struct '%s' has no field '%s'\n", stype, n->sval);
            exit(1);
        }
        gen_expr(n->right);
        emit("mov [rbp-");
        buf_write_int(out_buf, &out_cursor, base + foff);
        buf_write_str(out_buf, &out_cursor, "], rax\n");
        break;
    }
    case NODE_REASSIGN: {
        gen_expr(n->right);
        int off      = var_offset(n->name);
        DataType dt  = var_dtype(n->name);
        if (dt == DTYPE_FLOAT) {
            emitln("movq xmm0, rax");
            emit("movsd [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], xmm0\n");
        } else {
            emit("mov [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], rax\n");
        }
        break;
    }

    // ── print ──
    // detects type of expression and uses correct printf format
    case NODE_PRINT: {
        gen_expr(n->right);
        // determine what type we're printing
        int is_str   = (n->right->type == NODE_STRING) ||
                       (n->right->type == NODE_IDENT &&
                        var_dtype(n->right->name) == DTYPE_STR);
        int is_float = (n->right->dtype == DTYPE_FLOAT) ||
                       (n->right->type == NODE_IDENT &&
                        var_dtype(n->right->name) == DTYPE_FLOAT);
        int is_bool  = (n->right->type == NODE_BOOL) ||
                       (n->right->type == NODE_IDENT &&
                        var_dtype(n->right->name) == DTYPE_BOOL);
        if (is_str) {
            emitln("mov rsi, rax");
            emitln("lea rdi, [rel fmts]");
            emitln("xor rax, rax");
            emitln("call printf");
        } else if (is_float) {
            // bits are in rax — move back to xmm0 for printf
            emitln("movq xmm0, rax");
            emitln("lea rdi, [rel fmtf]");
            emitln("mov rax, 1");
            emitln("call printf");
        } else if (is_bool) {
            // print "true" or "false" based on value in rax
            int lbl_true  = new_label();
            int lbl_done  = new_label();
            emitln("test rax, rax");
            emit_jmp("jnz", lbl_true);
            emitln("lea rdi, [rel str_false]");
            emit_jmp("jmp", lbl_done);
            emit_label(lbl_true);
            emitln("lea rdi, [rel str_true]");
            emit_label(lbl_done);
            emitln("xor rax, rax");
            emitln("call printf");
        } else {
            // integer
            emitln("mov rsi, rax");
            emitln("lea rdi, [rel fmt]");
            emitln("xor rax, rax");
            emitln("call printf");
        }
        break;
    }

    // ── if / elif / else ──
    case NODE_IF: {
        int lbl_end = new_label();
        int branch_labels[64];
        for (int i = 0; i < n->child_count; i++) branch_labels[i] = new_label();

        gen_expr(n->left);
        emitln("test rax, rax");
        if (n->child_count > 0) emit_jmp("jz", branch_labels[0]);
        else                    emit_jmp("jz", lbl_end);
        gen_stmt(n->right);
        emit_jmp("jmp", lbl_end);

        for (int i = 0; i < n->child_count; i++) {
            emit_label(branch_labels[i]);
            Node *branch = n->children[i];
            if (branch->type == NODE_ELIF) {
                gen_expr(branch->left);
                emitln("test rax, rax");
                if (i + 1 < n->child_count) emit_jmp("jz", branch_labels[i+1]);
                else                         emit_jmp("jz", lbl_end);
                gen_stmt(branch->right);
                emit_jmp("jmp", lbl_end);
            } else if (branch->type == NODE_ELSE) {
                gen_stmt(branch->right);
            }
        }
        emit_label(lbl_end);
        break;
    }

    // ── while ──
    case NODE_WHILE: {
        int lbl_start = new_label();
        int lbl_end   = new_label();
        loop_push(lbl_end, lbl_start);
        emit_label(lbl_start);
        gen_expr(n->left);
        emitln("test rax, rax");
        emit_jmp("jz", lbl_end);
        gen_stmt(n->right);
        emit_jmp("jmp", lbl_start);
        emit_label(lbl_end);
        loop_pop();
        break;
    }

    // ── for loop ──
    // condition at bottom (FIX 3), limit/step hoisted to r14/r15 (FIX 4)
    case NODE_FOR: {
        int lbl_body  = new_label();
        int lbl_check = new_label();

        gen_expr(n->children[0]);                   // eval start
        int off = add_var(n->name, DTYPE_INT);
        int loop_reg = regalloc_assign(n->name);
        if (loop_reg >= 0) {
            // store in register
            emit("mov ");
            buf_write_str(out_buf, &out_cursor, alloc_regs[loop_reg]);
            buf_write_str(out_buf, &out_cursor, ", rax\n");
            // also store to RAM as backup
            emit("mov [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], rax\n");
        } else {
            emit("mov [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], rax\n");
        }

        gen_expr(n->children[1]);                   // hoist limit → r14
        emitln("mov r14, rax");

        gen_expr(n->children[2]);                   // hoist step → r15
        emitln("mov r15, rax");

        int lbl_for_end = new_label();
        int lbl_increment = new_label();
        loop_push(lbl_for_end, lbl_increment);

        // loop tilingtect if we should tile this loop
        int tile_size = 64;
        int should_tile = 0;
        int loop_range = get_loop_range(n->children[1], n->children[0]);
        if (loop_range > 128 &&
            n->children[2]->type == NODE_NUMBER &&
            n->children[2]->ival == 1 &&
            block_accesses_array(n->children[3], n->name)) {
            should_tile = 1;
        }

        // loop invariant code motion
        Node *body = n->children[3];
        int hoisted[64] = {0};
        if (body->type == NODE_BLOCK) {
            for (int i = 0; i < body->child_count; i++) {
                Node *stmt = body->children[i];
                if (stmt->type == NODE_ASSIGN &&
                    !node_uses_var(stmt->right, n->name)) {
                    gen_stmt(stmt);
                    hoisted[i] = 1;
                }
            }
        }

        if (should_tile) {
            // tiled loop — outer iterates over blocks, inner over elements
            int lbl_outer_body  = new_label();
            int lbl_outer_check = new_label();
            int lbl_inner_body  = new_label();
            int lbl_inner_check = new_label();

            // outer loop: block = start to limit step tile_size
            // block var stored at off (reuse loop var slot)
            emit_jmp("jmp", lbl_outer_check);
            emit_label(lbl_outer_body);

            // inner loop: i = block to min(block+tile_size, limit)
            // compute inner limit = min(block + tile_size, r14)
            emit("mov rax, [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emit("add rax, ");
            buf_write_int(out_buf, &out_cursor, tile_size);
            buf_write_str(out_buf, &out_cursor, "\n");
            emitln("cmp rax, r14");
            emitln("cmovg rax, r14");       // min(block+tile, limit)
            emitln("push rax");             // save inner limit

            // inner loop variable — reuse a temp stack slot
            emit("mov rax, [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emitln("push rax");             // save inner start

            emit_jmp("jmp", lbl_inner_check);
            emit_label(lbl_inner_body);

            // emit body with inner i on stack
            if (body->type == NODE_BLOCK) {
                for (int i = 0; i < body->child_count; i++) {
                    if (hoisted[i]) continue;
                    gen_stmt(body->children[i]);
                }
            } else {
                gen_stmt(body);
            }

            // inner i++
            emitln("mov rax, [rsp]");
            emitln("add rax, 1");
            emitln("mov [rsp], rax");

            emit_label(lbl_inner_check);
            emitln("mov rax, [rsp]");
            emitln("cmp rax, [rsp+8]");
            emit_jmp("jl", lbl_inner_body);

            emitln("add rsp, 16");          // clean up inner limit + start

            // outer block += tile_size
            emit("mov rax, [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emit("add rax, ");
            buf_write_int(out_buf, &out_cursor, tile_size);
            buf_write_str(out_buf, &out_cursor, "\n");
            emit("mov [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], rax\n");

            emit_label(lbl_outer_check);
            emit("mov rax, [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emitln("cmp rax, r14");
            emit_jmp("jl", lbl_outer_body);

        } else {
            emit_jmp("jmp", lbl_check);
            emit_label(lbl_body);

            if (body->type == NODE_BLOCK) {
                for (int i = 0; i < body->child_count; i++) {
                    if (hoisted[i]) continue;
                    gen_stmt(body->children[i]);
                }
            } else {
                gen_stmt(body);
            }

            emit_label(lbl_increment);
            if (loop_reg >= 0) {
                emit("add ");
                buf_write_str(out_buf, &out_cursor, alloc_regs[loop_reg]);
                buf_write_str(out_buf, &out_cursor, ", r15\n");
                emit("mov rax, ");
                buf_write_str(out_buf, &out_cursor, alloc_regs[loop_reg]);
                buf_write_str(out_buf, &out_cursor, "\n");
                emit("mov [rbp-");
                buf_write_int(out_buf, &out_cursor, off);
                buf_write_str(out_buf, &out_cursor, "], rax\n");
            } else {
                emit("mov rax, [rbp-");
                buf_write_int(out_buf, &out_cursor, off);
                buf_write_str(out_buf, &out_cursor, "]\n");
                emitln("add rax, r15");
                emit("mov [rbp-");
                buf_write_int(out_buf, &out_cursor, off);
                buf_write_str(out_buf, &out_cursor, "], rax\n");
            }

            emit_label(lbl_check);
            emit("mov rax, [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emitln("cmp rax, r14");
            emit_jmp("jle", lbl_body);
        }  // end else (non-tiled)

        regalloc_free(n->name);
        emit_label(lbl_for_end);
        loop_pop();
        break;
    }
          

    // ── function definition ──
    case NODE_FN_DEF: {
        Var saved_vars[256];
        int saved_var_count = var_count;
        int saved_stack_top = stack_top;
        memcpy(saved_vars, var_table, sizeof(Var) * var_count);
        var_count = 0; stack_top = 0; param_count = 0; cse_clear(); regalloc_clear(); loop_depth = 0;

        const char *arg_regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

        // exact stack size: params + locals, aligned to 16
        int local_vars  = count_vars(n->right);
        int total_bytes = (n->child_count + local_vars) * 8;
        if (total_bytes % 16 != 0) total_bytes += 8;
        if (total_bytes == 0)      total_bytes  = 16;

        buf_write_str(out_buf, &out_cursor, "\nglobal ");
        buf_write_str(out_buf, &out_cursor, n->name);
        buf_write_str(out_buf, &out_cursor, "\n");
        emit_named_label(n->name);
        emitln("push rbp");
        emitln("mov rbp, rsp");
        emit("sub rsp, ");
        buf_write_int(out_buf, &out_cursor, total_bytes);
        buf_write_str(out_buf, &out_cursor, "\n");

        for (int i = 0; i < n->child_count; i++) {
            stack_top += 8;
            param_table[i].offset = stack_top;
            param_table[i].dtype  = n->children[i]->dtype;
            strncpy(param_table[i].name, n->children[i]->name, 63);
            param_count++;
            emit("mov [rbp-");
            buf_write_int(out_buf, &out_cursor, stack_top);
            buf_write_str(out_buf, &out_cursor, "], ");
            buf_write_str(out_buf, &out_cursor, arg_regs[i]);
            buf_write_str(out_buf, &out_cursor, "\n");
        }

        gen_stmt(n->right);
        emit_auto_free();
        emitln("xor rax, rax");
        emitln("mov rsp, rbp");
        emitln("pop rbp");
        emitln("ret");

        var_count = saved_var_count;
        stack_top = saved_stack_top;
        param_count = 0;
        memcpy(var_table, saved_vars, sizeof(Var) * var_count);
        break;
    }

    // ── return ──
    case NODE_RETURN:
        gen_expr(n->right);
        emitln("mov rsp, rbp");
        emitln("pop rbp");
        emitln("ret");
        break;

    // ── standalone function call ──
    case NODE_FN_CALL:
        gen_expr(n);
        break;

    // ── match x ... end ──
    // emits compare chain: eval subject, cmp each case, jump to matching body
    // else branch is fallthrough default
    case NODE_MATCH: {
        int lbl_end = new_label();

        // allocate a label for each case body
        int case_labels[64];
        int else_label = -1;
        for (int i = 0; i < n->child_count; i++) {
            case_labels[i] = new_label();
            if (n->children[i]->left == NULL)
                else_label = case_labels[i];
        }

        // emit compare chain — eval subject once into r13, compare each case
        gen_expr(n->left);
        emitln("mov r13, rax");      // subject stays in r13

        for (int i = 0; i < n->child_count; i++) {
            Node *c = n->children[i];
            if (c->left == NULL) continue;   // skip else here, handle at bottom
            gen_expr(c->left);               // case value → rax
            emitln("cmp r13, rax");
            emit_jmp("je", case_labels[i]);
        }

        // no case matched — jump to else if exists, else to end
        if (else_label >= 0) emit_jmp("jmp", else_label);
        else                  emit_jmp("jmp", lbl_end);

        // emit each case body
        for (int i = 0; i < n->child_count; i++) {
            Node *c = n->children[i];
            emit_label(case_labels[i]);
            gen_stmt(c->right);         // case body
            emit_jmp("jmp", lbl_end);
        }

        emit_label(lbl_end);
        break;
    }

// deref(p) = val — write value to address stored in variable
    case NODE_DEREF_ASSIGN: {
        int off = var_offset(n->name);
        gen_expr(n->right);             // value → rax
        emitln("mov rbx, rax");         // save value in rbx
        emit("mov rax, [rbp-");         // load pointer
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "]\n");
        emitln("mov [rax], rbx");       // write value to address
        break;
    }


  // free(ptr, size) — munmap syscall
    case NODE_FREE: {
        gen_expr(n->left);              // ptr → rax
        emitln("mov rdi, rax");         // addr
        gen_expr(n->right);             // size → rax
        emitln("mov rsi, rax");         // size
        emitln("mov rax, 11");          // syscall 11 = munmap
        emitln("syscall");
        break;
    }

    // read(fd, buf, size) — syscall 0
    case NODE_READ: {
        gen_expr(n->children[0]);       // fd → rax
        emitln("mov rdi, rax");         // fd
        gen_expr(n->children[1]);       // buf → rax
        emitln("mov rsi, rax");         // buf
        gen_expr(n->children[2]);       // size → rax
        emitln("mov rdx, rax");         // size
        emitln("mov rax, 0");           // syscall 0 = read
        emitln("syscall");
        break;
    }

    // write(fd, buf, size) — syscall 1
    case NODE_WRITE: {
        gen_expr(n->children[0]);       // fd → rax
        emitln("mov rdi, rax");         // fd
        gen_expr(n->children[1]);       // buf → rax
        emitln("mov rsi, rax");         // buf
        gen_expr(n->children[2]);       // size → rax
        emitln("mov rdx, rax");         // size
        emitln("mov rax, 1");           // syscall 1 = write
        emitln("syscall");
        break;
    }

    // close(fd) — syscall 3
    case NODE_CLOSE: {
        gen_expr(n->left);              // fd → rax
        emitln("mov rdi, rax");         // fd
        emitln("mov rax, 3");           // syscall 3 = close
        emitln("syscall");
        break;
    }

// return a, b — put first value in rax, second in rdx
    case NODE_RETURN_MULTI: {
        gen_expr(n->children[0]);       // first value → rax
        emitln("push rax");             // save first
        gen_expr(n->children[1]);       // second value → rax
        emitln("mov rdx, rax");         // second → rdx
        emitln("pop rax");              // first → rax
        emitln("mov rsp, rbp");
        emitln("pop rbp");
        emitln("ret");
        break;
    }

    // let lo, hi = fn() — rax has first, rdx has second
    case NODE_ASSIGN_MULTI: {
        gen_expr(n->right);             // call fn — rax=first, rdx=second
        emitln("push rdx");             // save second
        int off1 = add_var(n->name, DTYPE_INT);
        emit("mov [rbp-");
        buf_write_int(out_buf, &out_cursor, off1);
        buf_write_str(out_buf, &out_cursor, "], rax\n");
        emitln("pop rax");              // restore second
        int off2 = add_var(n->sval, DTYPE_INT);
        emit("mov [rbp-");
        buf_write_int(out_buf, &out_cursor, off2);
        buf_write_str(out_buf, &out_cursor, "], rax\n");
        break;
    }

    case NODE_DO_WHILE: {
        int lbl_start = new_label();
        int lbl_end   = new_label();
        loop_push(lbl_end, lbl_start);
        emit_label(lbl_start);
        gen_stmt(n->right);
        gen_expr(n->left);
        emitln("test rax, rax");
        emit_jmp("jnz", lbl_start);
        emit_label(lbl_end);
        loop_pop();
        break;
    }

    // for i = 0 to 100 if condition
    case NODE_FOR_IF: {
        int lbl_body  = new_label();
        int lbl_check = new_label();
        int lbl_increment = new_label();

        gen_expr(n->children[0]);                   // start
        int off = add_var(n->name, DTYPE_INT);
        int loop_reg = regalloc_assign(n->name);
        if (loop_reg >= 0) {
            emit("mov ");
            buf_write_str(out_buf, &out_cursor, alloc_regs[loop_reg]);
            buf_write_str(out_buf, &out_cursor, ", rax\n");
        }
        emit("mov [rbp-");
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "], rax\n");

        gen_expr(n->children[1]);                   // limit → r14
        emitln("mov r14, rax");
        gen_expr(n->children[2]);                   // step → r15
        emitln("mov r15, rax");

        emit_jmp("jmp", lbl_check);
        int lbl_for_if_end = new_label();
        loop_push(lbl_for_if_end, lbl_increment);
        emit_label(lbl_body);

        // check filter condition — skip body if false
        gen_expr(n->left);
        emitln("test rax, rax");
        int lbl_skip = new_label();
        emit_jmp("jz", lbl_skip);
        gen_stmt(n->children[3]);                   // body
        emit_label(lbl_skip);

        // increment 
        emit_label(lbl_increment);
        if (loop_reg >= 0) {
            emit("add ");
            buf_write_str(out_buf, &out_cursor, alloc_regs[loop_reg]);
            buf_write_str(out_buf, &out_cursor, ", r15\n");
            emit("mov rax, ");
            buf_write_str(out_buf, &out_cursor, alloc_regs[loop_reg]);
            buf_write_str(out_buf, &out_cursor, "\n");
            emit("mov [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], rax\n");
        } else {
            emit("mov rax, [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "]\n");
            emitln("add rax, r15");
            emit("mov [rbp-");
            buf_write_int(out_buf, &out_cursor, off);
            buf_write_str(out_buf, &out_cursor, "], rax\n");
        }

        emit_label(lbl_check);
        emit("mov rax, [rbp-");
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "]\n");
        emitln("cmp rax, r14");
        emit_jmp("jle", lbl_body);
        emit_label(lbl_for_if_end);
        loop_pop();
        regalloc_free(n->name);
        break;
    }

    case NODE_BREAK:
        emit_jmp("jmp", loop_break());
        break;

    case NODE_CONTINUE:
        emit_jmp("jmp", loop_continue());
        break; 

    default:
        fprintf(stderr, "Codegen error: unknown statement node\n");
        exit(1);
    }
}

// ─────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────
void generate(Node *root, const char *out_file) {
    out_cursor  = 0;
    str_cursor  = 0;
    label_count = 0;
    str_count   = 0;
    var_count   = 0;
    stack_top   = 0;
    param_count = 0;
    cse_clear();
    regalloc_clear();

    loop_depth = 0;

    // .data section — format strings
    emit_str("section .data\n");
    emit_str("    fmt      db \"%ld\", 10, 0\n");   // int format
    emit_str("    fmtf     db \"%g\",  10, 0\n");   // float format
    emit_str("    fmts     db \"%s\",  10, 0\n");   // string format
    emit_str("    str_true  db \"true\",  10, 0\n"); // bool true
    emit_str("    str_false db \"false\", 10, 0\n"); // bool false
    emit_str("\n");

    // .text section
    emit_str("section .text\n");
    emit_str("    extern printf\n");
    emit_str("    extern strlen\n");
    emit_str("    global main\n\n");

    // emit all function definitions first
    for (int i = 0; i < root->child_count; i++)
        if (root->children[i]->type == NODE_FN_DEF)
            gen_stmt(root->children[i]);

    // exact stack size for main
    int main_vars = 0;
    for (int i = 0; i < root->child_count; i++)
        if (root->children[i]->type != NODE_FN_DEF)
            main_vars += count_vars(root->children[i]);
    int main_bytes = main_vars * 8;
    if (main_bytes % 16 != 0) main_bytes += 8;
    if (main_bytes == 0)      main_bytes  = 16;

    emit_str("\nmain:\n");
    emitln("push rbp");
    emitln("mov rbp, rsp");
    emit("sub rsp, ");
    buf_write_int(out_buf, &out_cursor, main_bytes);
    buf_write_str(out_buf, &out_cursor, "\n");

    for (int i = 0; i < root->child_count; i++)
        if (root->children[i]->type != NODE_FN_DEF)
            gen_stmt(root->children[i]);

    emitln("xor rax, rax");
    emitln("mov rsp, rbp");
    emitln("pop rbp");
    emitln("ret");

    // append collected string literals into a second .data section
    if (str_cursor > 0) {
        buf_write_str(out_buf, &out_cursor, "\nsection .data\n");
        buf_write_str(out_buf, &out_cursor, str_buf);
    }

    if (buf_flush(out_buf, out_cursor, out_file) != 0) {
        fprintf(stderr, "Codegen error: failed to write output file\n");
        exit(1);
    }
}
