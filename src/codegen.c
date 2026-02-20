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

typedef struct { char name[64]; int offset; int array_size; char struct_type[64]; DataType dtype; } Var;
static Var var_table[256];
static int var_count  = 0;
static int stack_top  = 0;
static Var param_table[64];
static int param_count = 0;

static int new_label() { return label_count++; }

static int var_offset(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(var_table[i].name, name) == 0) return var_table[i].offset;
    for (int i = 0; i < param_count; i++)
        if (strcmp(param_table[i].name, name) == 0) return param_table[i].offset;
    fprintf(stderr, "Codegen error: undefined variable '%s'\n", name);
    exit(1);
}

static DataType var_dtype(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(var_table[i].name, name) == 0) return var_table[i].dtype;
    for (int i = 0; i < param_count; i++)
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
    if (n->type == NODE_FOR)         count = 1;
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
        buf_write_str(str_buf, &str_cursor, "\", 10, 0\n");
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
        gen_expr(n->left);
        emitln("imul rax, 8");
        emitln("neg rax");
        emit("add rax, ");
        buf_write_str(out_buf, &out_cursor, "qword -");
        buf_write_int(out_buf, &out_cursor, base);
        buf_write_str(out_buf, &out_cursor, "\n");
        emitln("add rax, rbp");
        emitln("mov rax, [rax]");
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
    // FIX: use r10 for simple right-hand sides to avoid push/pop
    // fall back to push/pop for nested expressions to avoid r10 clobbering
    case NODE_BINOP:
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
        else if (strcmp(n->op, "*") == 0) emitln("imul rax, rbx");
        else if (strcmp(n->op, "/") == 0) { emitln("xor rdx, rdx"); emitln("idiv rbx"); }
        else                              emit_cmp(n->op);
        break;

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

    default:
        fprintf(stderr, "Codegen error: unexpected node in expression\n");
        exit(1);
    }
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
        gen_expr(n->right);               // value → rax
        emitln("push rax");               // save value
        gen_expr(n->left);                // index → rax
        emitln("imul rax, 8");            // i * 8
        emitln("neg rax");                // -(i*8)
        emit("add rax, qword -");
        buf_write_int(out_buf, &out_cursor, base);
        buf_write_str(out_buf, &out_cursor, "\n");
        emitln("add rax, rbp");           // rax = address of nums[i]
        emitln("pop rbx");                // restore value into rbx
        emitln("mov [rax], rbx");         // store value
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
            // rax has string pointer — pass as rdi directly
            emitln("mov rdi, rax");
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
        emit_label(lbl_start);
        gen_expr(n->left);
        emitln("test rax, rax");
        emit_jmp("jz", lbl_end);
        gen_stmt(n->right);
        emit_jmp("jmp", lbl_start);
        emit_label(lbl_end);
        break;
    }

    // ── for loop ──
    // condition at bottom (FIX 3), limit/step hoisted to r14/r15 (FIX 4)
    case NODE_FOR: {
        int lbl_body  = new_label();
        int lbl_check = new_label();

        gen_expr(n->children[0]);                   // eval start
        int off = add_var(n->name, DTYPE_INT);
        emit("mov [rbp-");
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "], rax\n");

        gen_expr(n->children[1]);                   // hoist limit → r14
        emitln("mov r14, rax");

        gen_expr(n->children[2]);                   // hoist step → r15
        emitln("mov r15, rax");

        emit_jmp("jmp", lbl_check);                 // enter at condition

        emit_label(lbl_body);
        gen_stmt(n->children[3]);                   // body

        emit("mov rax, [rbp-");                     // i = i + step
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "]\n");
        emitln("add rax, r15");
        emit("mov [rbp-");
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "], rax\n");

        emit_label(lbl_check);                      // condition at bottom
        emit("mov rax, [rbp-");
        buf_write_int(out_buf, &out_cursor, off);
        buf_write_str(out_buf, &out_cursor, "]\n");
        emitln("cmp rax, r14");
        emit_jmp("jle", lbl_body);
        break;
    }

    // ── function definition ──
    case NODE_FN_DEF: {
        Var saved_vars[256];
        int saved_var_count = var_count;
        int saved_stack_top = stack_top;
        memcpy(saved_vars, var_table, sizeof(Var) * var_count);
        var_count = 0; stack_top = 0; param_count = 0;

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

    // .data section — format strings
    emit_str("section .data\n");
    emit_str("    fmt      db \"%ld\", 10, 0\n");   // int format
    emit_str("    fmtf     db \"%g\",  10, 0\n");   // float format
    emit_str("    str_true  db \"true\",  10, 0\n"); // bool true
    emit_str("    str_false db \"false\", 10, 0\n"); // bool false
    emit_str("\n");

    // .text section
    emit_str("section .text\n");
    emit_str("    extern printf\n");
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
