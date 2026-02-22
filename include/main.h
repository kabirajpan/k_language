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
    TOK_DO,       // do


    TOK_TO,
    TOK_STEP,
    TOK_END,
    TOK_PRINT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_MATCH,
    TOK_COMPTIME,
    TOK_STRUCT,     // struct
    TOK_ADDR,       // addr
    TOK_DEREF,      // deref
    TOK_ALLOC,      // alloc
    TOK_FREE,       // free
                    
    TOK_OPEN,
    TOK_READ,
    TOK_WRITE,
    TOK_CLOSE,

    // operators
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQ,         // =
    TOK_EQEQ,      // ==
    TOK_NEQ,       // !=
    TOK_GT,         // >
    TOK_LT,         // <
    TOK_GTE,       // >=
    TOK_LTE,       // <=

    // delimiters
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_COLON,      // :
    TOK_ARROW,      // ->
    TOK_LBRACKET,   // [
    TOK_RBRACKET,   // ]
    TOK_LBRACE,     // {
    TOK_RBRACE,     // }
    TOK_DOT,        // .

    // type keywords
    TOK_TINT,
    TOK_TFLOAT,
    TOK_TSTR,
    TOK_TPTR,
    TOK_TBOOL,
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
    DTYPE_UNKNOWN = 0,
    DTYPE_INT,
    DTYPE_FLOAT,
    DTYPE_STR,
    DTYPE_PTR,
    DTYPE_BOOL,
    DTYPE_STRUCT,   // user-defined struct type
} DataType;

// ─────────────────────────────────────────
// STRUCT REGISTRY
// shared between parser and codegen
// ─────────────────────────────────────────
#define MAX_FIELDS  32
#define MAX_STRUCTS 64

typedef struct {
    char     name[64];
    DataType dtype;
    int      offset;    // byte offset from struct base (field 0 = 0, field 1 = 8, ...)
} FieldDef;

typedef struct {
    char     name[64];          // struct type name e.g. "Point"
    FieldDef fields[MAX_FIELDS];
    int      field_count;
    int      total_size;        // total bytes = field_count * 8
} StructDef;

extern StructDef struct_defs[MAX_STRUCTS];
extern int       struct_def_count;

// lookup helpers
StructDef *find_struct(const char *name);
int        find_field(StructDef *sd, const char *field, int *out_offset, DataType *out_dtype);

// ─────────────────────────────────────────
// AST NODE TYPES
// ─────────────────────────────────────────
typedef enum {
    NODE_NUMBER,
    NODE_BOOL,
    NODE_IDENT,
    NODE_STRING,
    NODE_BINOP,
    NODE_ASSIGN,        // let x = expr
    NODE_ARRAY_DECL,    // let nums: int[5]
    NODE_ARRAY_ASSIGN,  // nums[i] = expr
    NODE_ARRAY_ACCESS,  // nums[i] in expression
    NODE_ARRAY_INIT,    // {1, 2, 3}
    NODE_REASSIGN,      // x = expr
    NODE_PRINT,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_FN_DEF,
    NODE_FN_CALL,
    NODE_RETURN,
    NODE_BLOCK,
    NODE_ELIF,
    NODE_ELSE,
    NODE_MATCH,
    NODE_MATCH_CASE,
    NODE_COMPTIME,
    NODE_STRUCT_DEF,    // struct Name ... end
    NODE_STRUCT_INIT,   // Point(10, 20)
    NODE_FIELD_ACCESS,  // p.x  (in expression)
    NODE_FIELD_ASSIGN,  // p.x = val (statement)
    NODE_ADDR,          // addr(x)        — take address
    NODE_DEREF,         // deref(p)       — read through pointer
    NODE_DEREF_ASSIGN,  // deref(p) = val — write through pointer
    NODE_RETURN_MULTI,  // return a, b
    NODE_ASSIGN_MULTI,  // let lo, hi = fn()
    NODE_ALLOC,     // alloc(size)     — mmap syscall
    NODE_FREE,      // free(ptr, size) — munmap syscall
    NODE_OPEN,
    NODE_READ,
    NODE_WRITE,
    NODE_CLOSE,

    NODE_FOR_IF,  // for i = 0 to 100 if condition
    NODE_DO_WHILE // do ... while condition
} NodeType;

typedef struct Node Node;

struct Node {
    NodeType type;

    char     name[64];      // ident/fn/struct/field name
    int      ival;          // number / bool value
    int      array_size;    // array size for ARRAY_DECL
    char     op[3];         // operator
    char     sval[256];     // string value / field name for field access
    DataType dtype;         // resolved data type

    Node    *left;
    Node    *right;

    Node    *children[64];
    int      child_count;
};

// ─────────────────────────────────────────
// LIMITS
// ─────────────────────────────────────────
#define MAX_TOKENS  4096
#define MAX_NODES   4096

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
