#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/main.h"

static int cursor = 0;

// ─────────────────────────────────────────
// Struct registry (shared with codegen)
// ─────────────────────────────────────────
StructDef struct_defs[MAX_STRUCTS];
int       struct_def_count = 0;

StructDef *find_struct(const char *name) {
    for (int i = 0; i < struct_def_count; i++)
        if (strcmp(struct_defs[i].name, name) == 0)
            return &struct_defs[i];
    return NULL;
}

int find_field(StructDef *sd, const char *field, int *out_offset, DataType *out_dtype) {
    for (int i = 0; i < sd->field_count; i++) {
        if (strcmp(sd->fields[i].name, field) == 0) {
            if (out_offset) *out_offset = sd->fields[i].offset;
            if (out_dtype)  *out_dtype  = sd->fields[i].dtype;
            return 1;
        }
    }
    return 0;
}

// ─────────────────────────────────────────
// Node allocator
// ─────────────────────────────────────────
static Node node_pool[MAX_NODES];
static int  node_count = 0;

Node *new_node(NodeType type) {
    if (node_count >= MAX_NODES) {
        fprintf(stderr, "Parser error: too many nodes\n");
        exit(1);
    }
    Node *n = &node_pool[node_count++];
    memset(n, 0, sizeof(Node));
    n->type = type;
    return n;
}

// ─────────────────────────────────────────
// Token helpers
// ─────────────────────────────────────────
static Token *peek()  { return &tokens[cursor]; }
static Token *peek2() { return &tokens[cursor + 1]; }  // one ahead

static Token *advance() {
    Token *t = &tokens[cursor];
    if (t->type != TOK_EOF) cursor++;
    return t;
}

static Token *expect(TokenType type, const char *msg) {
    if (peek()->type != type) {
        fprintf(stderr, "Parse error: expected %s, got '%s'\n", msg, peek()->value);
        exit(1);
    }
    return advance();
}

// ─────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────
static Node *parse_statement();
static Node *parse_expression();
static Node *parse_comparison();
static Node *parse_term();
static Node *parse_factor();
static Node *parse_block_body();
static Node *parse_block();

// ─────────────────────────────────────────
// Parse type keyword (after colon/arrow consumed)
// ─────────────────────────────────────────
static DataType parse_type_keyword() {
    Token *t = peek();
    if (t->type == TOK_TINT)   { advance(); return DTYPE_INT;   }
    if (t->type == TOK_TFLOAT) { advance(); return DTYPE_FLOAT; }
    if (t->type == TOK_TSTR)   { advance(); return DTYPE_STR;   }
    if (t->type == TOK_TPTR)   { advance(); return DTYPE_PTR;   }
    if (t->type == TOK_TBOOL)  { advance(); return DTYPE_BOOL;  }
    // struct type — ident that matches a registered struct
    if (t->type == TOK_IDENT && find_struct(t->value)) {
        advance();
        return DTYPE_STRUCT;
    }
    fprintf(stderr, "Parse error: expected type, got '%s'\n", t->value);
    exit(1);
}

static DataType parse_type_annotation() {
    if (peek()->type != TOK_COLON) return DTYPE_UNKNOWN;
    advance();
    return parse_type_keyword();
}

// ─────────────────────────────────────────
// Infer type from expression node
// ─────────────────────────────────────────
static DataType infer_type(Node *expr) {
    if (!expr) return DTYPE_UNKNOWN;
    if (expr->type == NODE_NUMBER)      return DTYPE_INT;
    if (expr->type == NODE_STRING)      return DTYPE_STR;
    if (expr->type == NODE_BOOL)        return DTYPE_BOOL;
    if (expr->type == NODE_STRUCT_INIT) return DTYPE_STRUCT;
    if (expr->dtype != DTYPE_UNKNOWN)   return expr->dtype;
    return DTYPE_INT;
}

// ─────────────────────────────────────────
// Compile-time evaluator
// ─────────────────────────────────────────
static struct { char name[64]; long value; } ct_vars[256];
static int ct_var_count = 0;

static void ct_set(const char *name, long val) {
    for (int i = 0; i < ct_var_count; i++)
        if (strcmp(ct_vars[i].name, name) == 0) { ct_vars[i].value = val; return; }
    strncpy(ct_vars[ct_var_count].name, name, 63);
    ct_vars[ct_var_count].value = val;
    ct_var_count++;
}

static long ct_get(const char *name) {
    for (int i = 0; i < ct_var_count; i++)
        if (strcmp(ct_vars[i].name, name) == 0) return ct_vars[i].value;
    fprintf(stderr, "comptime error: unknown variable '%s'\n", name);
    exit(1);
}

static long eval_comptime(Node *n) {
    if (!n) { fprintf(stderr, "comptime error: null node\n"); exit(1); }
    switch (n->type) {
        case NODE_NUMBER: return (long)n->ival;
        case NODE_IDENT:  return ct_get(n->name);
        case NODE_BINOP: {
            long l = eval_comptime(n->left);
            long r = eval_comptime(n->right);
            if (strcmp(n->op, "+") == 0) return l + r;
            if (strcmp(n->op, "-") == 0) return l - r;
            if (strcmp(n->op, "*") == 0) return l * r;
            if (strcmp(n->op, "/") == 0) {
                if (r == 0) { fprintf(stderr, "comptime error: division by zero\n"); exit(1); }
                return l / r;
            }
            fprintf(stderr, "comptime error: unsupported op '%s'\n", n->op);
            exit(1);
        }
        default:
            fprintf(stderr, "comptime error: cannot evaluate node type %d at compile time\n", n->type);
            exit(1);
    }
}

// ─────────────────────────────────────────
// Parse block body — stops at elif/else/end
// ─────────────────────────────────────────
static Node *parse_block_body() {
    Node *block = new_node(NODE_BLOCK);
    while (peek()->type != TOK_END  &&
           peek()->type != TOK_ELIF &&
           peek()->type != TOK_ELSE &&
           peek()->type != TOK_EOF) {
        block->children[block->child_count++] = parse_statement();
    }
    return block;
}

// ─────────────────────────────────────────
// Parse full block — consumes 'end'
// ─────────────────────────────────────────
static Node *parse_block() {
    Node *block = new_node(NODE_BLOCK);
    while (peek()->type != TOK_END && peek()->type != TOK_EOF) {
        block->children[block->child_count++] = parse_statement();
    }
    expect(TOK_END, "end");
    return block;
}

// ─────────────────────────────────────────
// Parse single statement
// ─────────────────────────────────────────
static Node *parse_statement() {
    Token *t = peek();

    // ── struct Name ... end ──
    if (t->type == TOK_STRUCT) {
        advance();
        Token *name = expect(TOK_IDENT, "struct name");

        // register struct definition
        StructDef *sd = &struct_defs[struct_def_count++];
        memset(sd, 0, sizeof(StructDef));
        strncpy(sd->name, name->value, 63);

        Node *n = new_node(NODE_STRUCT_DEF);
        strncpy(n->name, name->value, 63);

        // parse fields: name: type
        int offset = 0;
        while (peek()->type != TOK_END && peek()->type != TOK_EOF) {
            Token *fname = expect(TOK_IDENT, "field name");
            expect(TOK_COLON, ":");
            DataType ftype = parse_type_keyword();

            // register field in struct def
            FieldDef *fd = &sd->fields[sd->field_count];
            strncpy(fd->name, fname->value, 63);
            fd->dtype  = ftype;
            fd->offset = offset;
            sd->field_count++;
            offset += 8;

            // also as child node for codegen
            Node *field = new_node(NODE_IDENT);
            strncpy(field->name, fname->value, 63);
            field->dtype = ftype;
            field->ival  = offset - 8;   // store offset in ival
            n->children[n->child_count++] = field;
        }
        sd->total_size = offset;
        expect(TOK_END, "end");
        return n;
    }

// read(fd, buf, size)
    if (t->type == TOK_READ) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n = new_node(NODE_READ);
        n->children[0] = parse_expression();   // fd
        expect(TOK_COMMA, ",");
        n->children[1] = parse_expression();   // buf
        expect(TOK_COMMA, ",");
        n->children[2] = parse_expression();   // size
        n->child_count = 3;
        expect(TOK_RPAREN, ")");
        return n;
    }

    // write(fd, buf, size)
    if (t->type == TOK_WRITE) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n = new_node(NODE_WRITE);
        n->children[0] = parse_expression();   // fd
        expect(TOK_COMMA, ",");
        n->children[1] = parse_expression();   // buf
        expect(TOK_COMMA, ",");
        n->children[2] = parse_expression();   // size
        n->child_count = 3;
        expect(TOK_RPAREN, ")");
        return n;
    }

    // close(fd)
    if (t->type == TOK_CLOSE) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n = new_node(NODE_CLOSE);
        n->left = parse_expression();   // fd
        expect(TOK_RPAREN, ")");
        return n;
    }

    if (t->type == TOK_BREAK) {
        advance();
        return new_node(NODE_BREAK);
    }
    if (t->type == TOK_CONTINUE) {
        advance();
        return new_node(NODE_CONTINUE);
    }

    // do ... while condition
    if (t->type == TOK_DO) {
        advance();
        Node *n  = new_node(NODE_DO_WHILE);
        // parse body until 'while' — not 'end'
        Node *body = new_node(NODE_BLOCK);
        while (peek()->type != TOK_WHILE && peek()->type != TOK_EOF)
            body->children[body->child_count++] = parse_statement();
        n->right = body;
        expect(TOK_WHILE, "while");
        n->left  = parse_comparison();
        return n;
    }


    // ── let x: type = expr  /  let x = expr  /  let nums: int[5] ──
    if (t->type == TOK_LET) {
        advance();
        Token *name       = expect(TOK_IDENT, "variable name");
        DataType declared = parse_type_annotation();

        // ── array declaration: let nums: int[5] = {1,2,3} ──
        if (peek()->type == TOK_LBRACKET) {
            advance();
            Token *sz  = expect(TOK_NUMBER, "array size");
            int    asz = atoi(sz->value);
            expect(TOK_RBRACKET, "]");

            Node *n       = new_node(NODE_ARRAY_DECL);
            strncpy(n->name, name->value, 63);
            n->dtype      = (declared != DTYPE_UNKNOWN) ? declared : DTYPE_INT;
            n->array_size = asz;

            if (peek()->type == TOK_EQ) {
                advance();
                expect(TOK_LBRACE, "{");
                Node *init       = new_node(NODE_ARRAY_INIT);
                strncpy(init->name, name->value, 63);
                init->dtype      = n->dtype;
                init->array_size = asz;
                while (peek()->type != TOK_RBRACE && peek()->type != TOK_EOF) {
                    init->children[init->child_count++] = parse_expression();
                    if (peek()->type == TOK_COMMA) advance();
                }
                expect(TOK_RBRACE, "}");
                Node *blk = new_node(NODE_BLOCK);
                blk->children[blk->child_count++] = n;
                blk->children[blk->child_count++] = init;
                return blk;
            }
            return n;
        }

        if (peek()->type == TOK_COMMA) {
            advance();
            Token *name2 = expect(TOK_IDENT, "second variable name");
            expect(TOK_EQ, "=");
            Node *n = new_node(NODE_ASSIGN_MULTI);
            strncpy(n->name, name->value, 63);
            strncpy(n->sval, name2->value, 63);
            n->right = parse_expression();
            return n;
        }
        expect(TOK_EQ, "=");
        Node *n = new_node(NODE_ASSIGN);
        strncpy(n->name, name->value, 63);
        n->right = parse_comparison();
        DataType inferred = infer_type(n->right);

        if (declared != DTYPE_UNKNOWN) {
            int coerce_ok = (declared == DTYPE_FLOAT  && inferred == DTYPE_INT) ||
                            (declared == DTYPE_BOOL   && inferred == DTYPE_INT) ||
                            (declared == DTYPE_STRUCT && inferred == DTYPE_STRUCT);
            if (!coerce_ok && inferred != DTYPE_UNKNOWN && declared != inferred) {
                fprintf(stderr, "Type error: '%s' declared as type %d but value is type %d\n",
                        name->value, declared, inferred);
                exit(1);
            }
            n->dtype = declared;
        } else {
            n->dtype = (inferred != DTYPE_UNKNOWN) ? inferred : DTYPE_INT;
        }

        // for struct assignments, carry the struct type name in sval
        if (n->right->type == NODE_STRUCT_INIT)
            strncpy(n->sval, n->right->name, 63);

        n->right->dtype = n->dtype;

        // register comptime variable for cross-reference
        if (n->right->type == NODE_NUMBER)
            ct_set(n->name, (long)n->right->ival);

        return n;
    }
// deref(p) = val — write through pointer
    if (t->type == TOK_DEREF) {
        advance();
        expect(TOK_LPAREN, "(");
        Token *var = expect(TOK_IDENT, "variable name");
        expect(TOK_RPAREN, ")");
        expect(TOK_EQ, "=");
        Node *n = new_node(NODE_DEREF_ASSIGN);
        strncpy(n->name, var->value, 63);
        n->right = parse_expression();
        return n;
    }

// free(ptr, size) — munmap syscall
    if (t->type == TOK_FREE) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n = new_node(NODE_FREE);
        n->left  = parse_expression();   // ptr
        expect(TOK_COMMA, ",");
        n->right = parse_expression();   // size
        expect(TOK_RPAREN, ")");
        return n;
    }


    // ── return expr ──
    if (t->type == TOK_RETURN) {
        advance();
        Node *first = parse_expression();
        if (peek()->type == TOK_COMMA) {
            advance();
            Node *n = new_node(NODE_RETURN_MULTI);
            n->children[0] = first;
            n->children[1] = parse_expression();
            n->child_count = 2;
            return n;
        }
        Node *n  = new_node(NODE_RETURN);
        n->right = first;
        return n;
    }

    // ── print(expr) ──
    if (t->type == TOK_PRINT) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n  = new_node(NODE_PRINT);
        n->right = parse_comparison();
        expect(TOK_RPAREN, ")");
        return n;
    }

    // ── if / elif / else ──
    if (t->type == TOK_IF) {
        advance();
        Node *n  = new_node(NODE_IF);
        n->left  = parse_comparison();
        n->right = parse_block_body();
        while (peek()->type == TOK_ELIF) {
            advance();
            Node *elif_node  = new_node(NODE_ELIF);
            elif_node->left  = parse_comparison();
            elif_node->right = parse_block_body();
            n->children[n->child_count++] = elif_node;
        }
        if (peek()->type == TOK_ELSE) {
            advance();
            Node *else_node  = new_node(NODE_ELSE);
            else_node->right = parse_block_body();
            n->children[n->child_count++] = else_node;
        }
        expect(TOK_END, "end");
        return n;
    }

    // ── while ──
    if (t->type == TOK_WHILE) {
        advance();
        Node *n  = new_node(NODE_WHILE);
        n->left  = parse_comparison();
        n->right = parse_block();
        return n;
    }

    // ── for i = start to limit [step n] ... end ──
    if (t->type == TOK_FOR) {
        advance();
        Node *n    = new_node(NODE_FOR);
        Token *var = expect(TOK_IDENT, "loop variable");
        strncpy(n->name, var->value, 63);
        n->dtype = DTYPE_INT;
        expect(TOK_EQ, "=");
        n->children[0] = parse_expression();
        expect(TOK_TO, "to");
        n->children[1] = parse_expression();
        if (peek()->type == TOK_STEP) {
            advance();
            n->children[2] = parse_expression();
        } else {
            Node *one  = new_node(NODE_NUMBER);
            one->ival  = 1;
            one->dtype = DTYPE_INT;
            n->children[2] = one;
        }
        n->child_count = 3;
        // optional if condition: for i = 0 to 100 if i % 2 == 0
        if (peek()->type == TOK_WHERE) {
            advance();
            Node *fi = new_node(NODE_FOR_IF);
            fi->children[0] = n->children[0];
            fi->children[1] = n->children[1];
            fi->children[2] = n->children[2];
            strncpy(fi->name, n->name, 63);
            fi->left = parse_comparison();
            fi->children[3] = parse_block();
            fi->child_count = 4;
            return fi;
        }
        n->children[3] = parse_block();
        n->child_count = 4;
        return n;
    }

    // ── match x ... end ──
    if (t->type == TOK_MATCH) {
        advance();
        Node *n = new_node(NODE_MATCH);
        n->left = parse_expression();
        while (peek()->type != TOK_END && peek()->type != TOK_EOF) {
            Node *c = new_node(NODE_MATCH_CASE);
            if (peek()->type == TOK_ELSE) {
                advance();
                c->left = NULL;
            } else {
                c->left = parse_expression();
            }
            expect(TOK_ARROW, "->");
            c->right = parse_statement();
            n->children[n->child_count++] = c;
        }
        expect(TOK_END, "end");
        return n;
    }

    // ── fn name(a: int, b: int) -> int ... end ──
    if (t->type == TOK_FN) {
        advance();
        Token *name = expect(TOK_IDENT, "function name");
        Node *n = new_node(NODE_FN_DEF);
        strncpy(n->name, name->value, 63);
        n->dtype = DTYPE_INT;

        expect(TOK_LPAREN, "(");
        while (peek()->type != TOK_RPAREN) {
            Token *param = expect(TOK_IDENT, "parameter name");
            Node *p  = new_node(NODE_IDENT);
            strncpy(p->name, param->value, 63);
            p->dtype = parse_type_annotation();
            if (p->dtype == DTYPE_UNKNOWN) p->dtype = DTYPE_INT;
            n->children[n->child_count++] = p;
            if (peek()->type == TOK_COMMA) advance();
        }
        expect(TOK_RPAREN, ")");

       if (peek()->type == TOK_ARROW) {
            advance();
            n->dtype = parse_type_keyword();
            // skip second return type if present — e.g. -> int, int
            if (peek()->type == TOK_COMMA) {
                advance();
                parse_type_keyword();   // consume second type, ignore for now
            }
        }
        n->right = parse_block();
        return n;
    }

    // ── ident-based statements ──
    if (t->type == TOK_IDENT) {
        char name[64];
        strncpy(name, t->value, 63);
        advance();

        // function call statement
        if (peek()->type == TOK_LPAREN) {
            advance();
            Node *n = new_node(NODE_FN_CALL);
            strncpy(n->name, name, 63);
            while (peek()->type != TOK_RPAREN) {
                n->children[n->child_count++] = parse_expression();
                if (peek()->type == TOK_COMMA) advance();
            }
            expect(TOK_RPAREN, ")");
            return n;
        }

        // array element assignment: nums[i] = val
        if (peek()->type == TOK_LBRACKET) {
            advance();
            Node *n = new_node(NODE_ARRAY_ASSIGN);
            strncpy(n->name, name, 63);
            n->left  = parse_expression();
            expect(TOK_RBRACKET, "]");
            expect(TOK_EQ, "=");
            n->right = parse_expression();
            return n;
        }

        // field assignment: p.field = val
        if (peek()->type == TOK_DOT) {
            advance();  // consume '.'
            Token *field = expect(TOK_IDENT, "field name");
            expect(TOK_EQ, "=");
            Node *n = new_node(NODE_FIELD_ASSIGN);
            strncpy(n->name,  name,          63);   // struct var name
            strncpy(n->sval,  field->value,  63);   // field name
            n->right = parse_expression();
            return n;
        }

        // plain reassignment: x = val
        if (peek()->type == TOK_EQ) {
            advance();
            Node *n  = new_node(NODE_REASSIGN);
            strncpy(n->name, name, 63);
            n->right = parse_expression();
            return n;
        }

        fprintf(stderr, "Parse error: unexpected token '%s' after identifier\n", peek()->value);
        exit(1);
    }

    fprintf(stderr, "Parse error: unexpected token '%s'\n", t->value);
    exit(1);
}

// ─────────────────────────────────────────
// Comparison
// ─────────────────────────────────────────
static Node *parse_comparison() {
    Node *left = parse_expression();
    Token *t   = peek();
    if (t->type == TOK_GT   || t->type == TOK_LT  ||
        t->type == TOK_EQEQ || t->type == TOK_NEQ ||
        t->type == TOK_GTE  || t->type == TOK_LTE) {
        advance();
        Node *n  = new_node(NODE_BINOP);
        strncpy(n->op, t->value, 2);
        n->left  = left;
        n->right = parse_expression();
        left = n;
    }
    // logical and / or
    while (peek()->type == TOK_AND || peek()->type == TOK_OR) {
        Token *op = advance();
        Node *n = new_node(op->type == TOK_AND ? NODE_AND : NODE_OR);
        n->left  = left;
        n->right = parse_comparison();
        left = n;
    }
    return left;
}

// ─────────────────────────────────────────
// Expression: term ((+ -) term)*
// ─────────────────────────────────────────
static Node *parse_expression() {
    Node *left = parse_term();
    while (peek()->type == TOK_PLUS || peek()->type == TOK_MINUS) {
        Token *op = advance();
        Node  *n  = new_node(NODE_BINOP);
        strncpy(n->op, op->value, 2);
        n->left  = left;
        n->right = parse_term();
        left     = n;
    }
    return left;
}

// ─────────────────────────────────────────
// Term: factor ((* /) factor)*
// ─────────────────────────────────────────
static Node *parse_term() {
    Node *left = parse_factor();
    while (peek()->type == TOK_STAR || peek()->type == TOK_SLASH) {
        Token *op = advance();
        Node  *n  = new_node(NODE_BINOP);
        strncpy(n->op, op->value, 2);
        n->left  = left;
        n->right = parse_factor();
        left     = n;
    }
    return left;
}

// ─────────────────────────────────────────
// Factor: number | string | bool | comptime | ident | fn_call | array_access | field_access | (expr)
// ─────────────────────────────────────────
static Node *parse_factor() {
    Token *t = peek();

    // comptime(expr)
    if (t->type == TOK_COMPTIME) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *inner = parse_expression();
        expect(TOK_RPAREN, ")");
        long val = eval_comptime(inner);
        Node *n  = new_node(NODE_NUMBER);
        n->ival  = (int)val;
        n->dtype = DTYPE_INT;
        return n;
    }


// open(filename, flags) — returns fd
    if (t->type == TOK_OPEN) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n = new_node(NODE_OPEN);
        n->left  = parse_expression();   // filename
        expect(TOK_COMMA, ",");
        n->right = parse_expression();   // flags
        expect(TOK_RPAREN, ")");
        n->dtype = DTYPE_INT;
        return n;
    }
// addr(x) — take address of variable
    if (t->type == TOK_ADDR) {
        advance();
        expect(TOK_LPAREN, "(");
        Token *var = expect(TOK_IDENT, "variable name");
        expect(TOK_RPAREN, ")");
        Node *n = new_node(NODE_ADDR);
        strncpy(n->name, var->value, 63);
        n->dtype = DTYPE_PTR;
        return n;
    }

    // deref(p) — read through pointer
    if (t->type == TOK_DEREF) {
        advance();
        expect(TOK_LPAREN, "(");
        Token *var = expect(TOK_IDENT, "variable name");
        expect(TOK_RPAREN, ")");
        Node *n = new_node(NODE_DEREF);
        strncpy(n->name, var->value, 63);
        n->dtype = DTYPE_INT;
        return n;
    }

// alloc(size) — mmap syscall
    if (t->type == TOK_ALLOC) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n = new_node(NODE_ALLOC);
        n->left = parse_expression();   // size
        expect(TOK_RPAREN, ")");
        n->dtype = DTYPE_PTR;
        return n;
    }

    // negative number literal: -5
    if (t->type == TOK_MINUS) {
        advance();
        Token *num = peek();
        if (num->type == TOK_NUMBER) {
            advance();
            Node *n  = new_node(NODE_NUMBER);
            n->ival  = -atoi(num->value);
            n->dtype = DTYPE_INT;
            return n;
        }
        // negative expression: -(expr)
        Node *n   = new_node(NODE_NEG);
        n->right  = parse_factor();
        n->dtype  = DTYPE_INT;
        return n;
    }

    if (t->type == TOK_STRLEN) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n  = new_node(NODE_STRLEN);
        n->right = parse_expression();
        expect(TOK_RPAREN, ")");
        n->dtype = DTYPE_INT;
        return n;
    }

    if (t->type == TOK_NUMBER) {
        advance();
        Node *n  = new_node(NODE_NUMBER);
        n->ival  = atoi(t->value);
        n->dtype = DTYPE_INT;
        return n;
    }

    if (t->type == TOK_TRUE || t->type == TOK_FALSE) {
        advance();
        Node *n  = new_node(NODE_BOOL);
        n->ival  = (t->type == TOK_TRUE) ? 1 : 0;
        n->dtype = DTYPE_BOOL;
        return n;
    }

    if (t->type == TOK_STRING) {
        advance();
        Node *n = new_node(NODE_STRING);
        strncpy(n->sval, t->value, 255);
        n->dtype = DTYPE_STR;
        return n;
    }

    if (t->type == TOK_IDENT) {
        char name[64];
        strncpy(name, t->value, 63);
        advance();

        // function call or struct constructor: Name(...)
        if (peek()->type == TOK_LPAREN) {
            advance();
            // is this a struct constructor?
            StructDef *sd = find_struct(name);
            if (sd) {
                Node *n = new_node(NODE_STRUCT_INIT);
                strncpy(n->name, name, 63);   // struct type name
                n->dtype = DTYPE_STRUCT;
                while (peek()->type != TOK_RPAREN && peek()->type != TOK_EOF) {
                    n->children[n->child_count++] = parse_expression();
                    if (peek()->type == TOK_COMMA) advance();
                }
                expect(TOK_RPAREN, ")");
                return n;
            }
            // regular function call
            Node *n = new_node(NODE_FN_CALL);
            strncpy(n->name, name, 63);
            while (peek()->type != TOK_RPAREN && peek()->type != TOK_EOF) {
                n->children[n->child_count++] = parse_expression();
                if (peek()->type == TOK_COMMA) advance();
            }
            expect(TOK_RPAREN, ")");
            return n;
        }

        // array access: nums[i]
        if (peek()->type == TOK_LBRACKET) {
            advance();
            Node *n = new_node(NODE_ARRAY_ACCESS);
            strncpy(n->name, name, 63);
            n->left = parse_expression();
            expect(TOK_RBRACKET, "]");
            return n;
        }

        // field access: p.field
        if (peek()->type == TOK_DOT) {
            advance();  // consume '.'
            Token *field = expect(TOK_IDENT, "field name");
            Node *n = new_node(NODE_FIELD_ACCESS);
            strncpy(n->name, name,         63);  // struct var name
            strncpy(n->sval, field->value, 63);  // field name
            return n;
        }

        // plain identifier
        Node *n = new_node(NODE_IDENT);
        strncpy(n->name, name, 63);
        return n;
    }

    if (t->type == TOK_LPAREN) {
        advance();
        Node *n = parse_expression();
        expect(TOK_RPAREN, ")");
        return n;
    }

    fprintf(stderr, "Parse error: unexpected token '%s' in expression\n", t->value);
    exit(1);
}

// ─────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────
Node *parse(void) {
    cursor     = 0;
    node_count = 0;
    Node *root = new_node(NODE_BLOCK);
    while (peek()->type != TOK_EOF) {
        root->children[root->child_count++] = parse_statement();
    }
    return root;
}
