#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/main.h"

static int cursor = 0;

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
static Token *peek() { return &tokens[cursor]; }

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
// Parse type keyword after ':' or '->'
// caller has already consumed the colon/arrow
// ─────────────────────────────────────────
static DataType parse_type_keyword() {
    Token *t = peek();
    if (t->type == TOK_TINT)   { advance(); return DTYPE_INT;   }
    if (t->type == TOK_TFLOAT) { advance(); return DTYPE_FLOAT; }
    if (t->type == TOK_TSTR)   { advance(); return DTYPE_STR;   }
    if (t->type == TOK_TPTR)   { advance(); return DTYPE_PTR;   }
    if (t->type == TOK_TBOOL)  { advance(); return DTYPE_BOOL;  }
    fprintf(stderr, "Parse error: expected type (int/float/str/ptr/bool), got '%s'\n", t->value);
    exit(1);
}

// ─────────────────────────────────────────
// Parse optional ': type' annotation
// ─────────────────────────────────────────
static DataType parse_type_annotation() {
    if (peek()->type != TOK_COLON) return DTYPE_UNKNOWN;
    advance();  // consume ':'
    return parse_type_keyword();
}

// ─────────────────────────────────────────
// Infer type from expression node
// ─────────────────────────────────────────
static DataType infer_type(Node *expr) {
    if (!expr) return DTYPE_UNKNOWN;
    if (expr->type == NODE_NUMBER) return DTYPE_INT;
    if (expr->type == NODE_STRING) return DTYPE_STR;
    if (expr->dtype != DTYPE_UNKNOWN) return expr->dtype;
    return DTYPE_INT;
}

// ─────────────────────────────────────────
// Compile-time expression evaluator
// Walks AST and reduces to a single integer
// Supports: +  -  *  /  and named comptime vars
// ─────────────────────────────────────────

// table of comptime variables (let x: int = comptime(...))
static struct { char name[64]; long value; } ct_vars[256];
static int ct_var_count = 0;

static void ct_set(const char *name, long val) {
    // update if exists
    for (int i = 0; i < ct_var_count; i++)
        if (strcmp(ct_vars[i].name, name) == 0) { ct_vars[i].value = val; return; }
    // add new
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
            fprintf(stderr, "comptime error: unsupported node type %d\n", n->type);
            exit(1);
    }
}


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

    // ── let x: type = expr  /  let x = expr  /  let nums: int[5] ──
    if (t->type == TOK_LET) {
        advance();
        Token *name       = expect(TOK_IDENT, "variable name");
        DataType declared = parse_type_annotation();   // optional ': type'

        // ── array declaration: let nums: int[5]  OR  let nums: int[4] = {1,2,3,4} ──
        if (peek()->type == TOK_LBRACKET) {
            advance();  // consume '['
            Token *sz  = expect(TOK_NUMBER, "array size");
            int    asz = atoi(sz->value);
            expect(TOK_RBRACKET, "]");

            Node *n       = new_node(NODE_ARRAY_DECL);
            strncpy(n->name, name->value, 63);
            n->dtype      = (declared != DTYPE_UNKNOWN) ? declared : DTYPE_INT;
            n->array_size = asz;

            // optional initialiser: = {1, 2, 3, 4}
            if (peek()->type == TOK_EQ) {
                advance();  // consume '='
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
                // return both nodes wrapped: decl first, then init
                // we use a block node to carry both
                Node *blk = new_node(NODE_BLOCK);
                blk->children[blk->child_count++] = n;
                blk->children[blk->child_count++] = init;
                return blk;
            }
            return n;
        }

        // ── regular variable ──
        expect(TOK_EQ, "=");
        Node *n = new_node(NODE_ASSIGN);
        strncpy(n->name, name->value, 63);
        n->right = parse_comparison();

        DataType inferred = infer_type(n->right);
        if (declared != DTYPE_UNKNOWN) {
            int coerce_ok = (declared == DTYPE_FLOAT && inferred == DTYPE_INT) ||
                            (declared == DTYPE_BOOL  && inferred == DTYPE_INT);
            if (!coerce_ok && inferred != DTYPE_UNKNOWN && declared != inferred) {
                fprintf(stderr, "Type error: '%s' declared as type %d but value is type %d\n",
                        name->value, declared, inferred);
                exit(1);
            }
            n->dtype = declared;
        } else {
            n->dtype = (inferred != DTYPE_UNKNOWN) ? inferred : DTYPE_INT;
        }
        n->right->dtype = n->dtype;

        // if right side was a comptime (now folded to NODE_NUMBER),
        // register this variable so other comptime exprs can use it
        if (n->right->type == NODE_NUMBER)
            ct_set(n->name, (long)n->right->ival);

        return n;
    }

    // ── return expr ──
    if (t->type == TOK_RETURN) {
        advance();
        Node *n  = new_node(NODE_RETURN);
        n->right = parse_expression();
        return n;
    }

    // ── print(expr) ──
    if (t->type == TOK_PRINT) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *n  = new_node(NODE_PRINT);
        n->right = parse_expression();
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

    // ── while condition ... end ──
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
        Node *n  = new_node(NODE_FOR);
        Token *var = expect(TOK_IDENT, "loop variable");
        strncpy(n->name, var->value, 63);
        n->dtype = DTYPE_INT;
        expect(TOK_EQ, "=");
        n->children[0] = parse_expression();    // start
        expect(TOK_TO, "to");
        n->children[1] = parse_expression();    // limit
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
        n->children[3] = parse_block();
        n->child_count = 4;
        return n;
    }

    // ── match x ... end ──
    // cases: literal -> stmt  OR  else -> stmt
    // NODE_MATCH.left = subject expression
    // NODE_MATCH.children[] = NODE_MATCH_CASE nodes
    //   NODE_MATCH_CASE.left  = value expr (NULL for else)
    //   NODE_MATCH_CASE.right = body statement
    if (t->type == TOK_MATCH) {
        advance();
        Node *n  = new_node(NODE_MATCH);
        n->left  = parse_expression();   // subject: match x

        while (peek()->type != TOK_END && peek()->type != TOK_EOF) {
            Node *c = new_node(NODE_MATCH_CASE);

            if (peek()->type == TOK_ELSE) {
                advance();               // consume 'else'
                c->left = NULL;          // NULL = else branch
            } else {
                c->left = parse_expression();  // case value
            }

            expect(TOK_ARROW, "->");     // consume '->'
            c->right = parse_statement();// case body (single statement)
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
        n->dtype = DTYPE_INT;   // default return type

        expect(TOK_LPAREN, "(");
        while (peek()->type != TOK_RPAREN) {
            Token *param = expect(TOK_IDENT, "parameter name");
            Node *p  = new_node(NODE_IDENT);
            strncpy(p->name, param->value, 63);
            p->dtype = parse_type_annotation();   // optional ': type'
            if (p->dtype == DTYPE_UNKNOWN) p->dtype = DTYPE_INT;
            n->children[n->child_count++] = p;
            if (peek()->type == TOK_COMMA) advance();
        }
        expect(TOK_RPAREN, ")");

        // optional '-> returntype'
        if (peek()->type == TOK_ARROW) {
            advance();                  // consume '->'
            n->dtype = parse_type_keyword();   // read type directly (no colon)
        }

        n->right = parse_block();
        return n;
    }

    // ── ident(...) / ident[i] = expr / ident = expr ──
    if (t->type == TOK_IDENT) {
        char name[64];
        strncpy(name, t->value, 63);
        advance();

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

        // ── array element assignment: nums[i] = val ──
        if (peek()->type == TOK_LBRACKET) {
            advance();  // consume '['
            Node *n = new_node(NODE_ARRAY_ASSIGN);
            strncpy(n->name, name, 63);
            n->left  = parse_expression();   // index
            expect(TOK_RBRACKET, "]");
            expect(TOK_EQ, "=");
            n->right = parse_expression();   // value
            return n;
        }

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
// Comparison: expr (> < == != >= <=) expr
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
        return n;
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
// Factor: number | string | ident | fn_call | (expr)
// ─────────────────────────────────────────
static Node *parse_factor() {
    Token *t = peek();

    // ── comptime(expr) — evaluate at compile time, inline as constant ──
    if (t->type == TOK_COMPTIME) {
        advance();
        expect(TOK_LPAREN, "(");
        Node *inner = parse_expression();
        expect(TOK_RPAREN, ")");
        long val = eval_comptime(inner);   // evaluate NOW in compiler
        Node *n  = new_node(NODE_NUMBER);  // replace with constant
        n->ival  = (int)val;
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

    // ── bool literals ──
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

        // ── array element read: nums[i] in expression ──
        if (peek()->type == TOK_LBRACKET) {
            advance();  // consume '['
            Node *n = new_node(NODE_ARRAY_ACCESS);
            strncpy(n->name, name, 63);
            n->left = parse_expression();   // index
            expect(TOK_RBRACKET, "]");
            return n;
        }

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
