#ifndef MAIN_H
#define MAIN_H

// ─────────────────────────────────────────
// TOKEN TYPES
// ─────────────────────────────────────────
typedef enum {
    // literals
    TOK_NUMBER,
    TOK_IDENT,
    TOK_STRING,

    // keywords
    TOK_LET,
    TOK_FN,
    TOK_RETURN,
    TOK_IF,
    TOK_ELIF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_TO,
    TOK_STEP,
    TOK_END,
    TOK_PRINT,
    TOK_TRUE,      // true
    TOK_FALSE,     // false

    // operators
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQ,        // =
    TOK_EQEQ,     // ==
    TOK_NEQ,      // !=
    TOK_GT,        // >
    TOK_LT,        // <
    TOK_GTE,      // >=
    TOK_LTE,      // <=

    // delimiters
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_COLON,     // :
    TOK_ARROW,     // ->

    // type keywords
    TOK_TINT,      // int
    TOK_TFLOAT,    // float
    TOK_TSTR,      // str
    TOK_TPTR,      // ptr
    TOK_TBOOL,     // bool

    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char      value[64];
} Token;

// ─────────────────────────────────────────
// DATA TYPES
// ─────────────────────────────────────────
typedef enum {
    DTYPE_UNKNOWN = 0,  // not yet resolved / infer
    DTYPE_INT,          // 64-bit integer
    DTYPE_FLOAT,        // 64-bit double
    DTYPE_STR,          // string pointer
    DTYPE_PTR,          // raw pointer
    DTYPE_BOOL,         // boolean (1 byte, true=1 false=0)
} DataType;

// ─────────────────────────────────────────
// AST NODE TYPES
// ─────────────────────────────────────────
typedef enum {
    NODE_NUMBER,
    NODE_BOOL,     // true / false literal
    NODE_IDENT,
    NODE_STRING,
    NODE_BINOP,
    NODE_ASSIGN,    // let x = expr
    NODE_REASSIGN,  // x = expr  (no let)
    NODE_PRINT,
    NODE_IF,        // if / elif chain + optional else
    NODE_WHILE,
    NODE_FOR,       // for i = start to end step n
    NODE_FN_DEF,
    NODE_FN_CALL,
    NODE_RETURN,
    NODE_BLOCK,
    NODE_ELIF,      // single elif branch (cond + body)
    NODE_ELSE,      // else body
} NodeType;

// forward declare
typedef struct Node Node;

struct Node {
    NodeType type;

    char     name[64];      // ident name, fn name, param name
    int      ival;          // number value / bool value (0 or 1)
    char     op[3];         // operator: +  -  *  /  >  <  =  !=  >=  <=
    char     sval[256];     // string literal value
    DataType dtype;         // resolved data type

    Node    *left;          // binary op left, condition
    Node    *right;         // binary op right, assign value, if-body

    // for blocks, fn bodies, fn args, elif chains
    // NODE_FOR: children[0]=start [1]=limit [2]=step [3]=body
    Node    *children[64];
    int      child_count;
};

// ─────────────────────────────────────────
// LIMITS
// ─────────────────────────────────────────
#define MAX_TOKENS  4096
#define MAX_NODES   4096

// ─────────────────────────────────────────
// GLOBAL TOKEN ARRAY (filled by lexer)
// ─────────────────────────────────────────
extern Token tokens[MAX_TOKENS];
extern int   token_count;

// ─────────────────────────────────────────
// FUNCTION DECLARATIONS
// ─────────────────────────────────────────
void  tokenize(const char *src);
Node *parse(void);
void  generate(Node *root, const char *out_file);
Node *new_node(NodeType type);

#endif
