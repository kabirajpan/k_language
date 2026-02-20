#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/main.h"

Token tokens[MAX_TOKENS];
int   token_count = 0;

static void add_token(TokenType type, const char *value) {
    if (token_count >= MAX_TOKENS) {
        fprintf(stderr, "Lexer error: too many tokens\n");
        exit(1);
    }
    tokens[token_count].type = type;
    strncpy(tokens[token_count].value, value, 63);
    tokens[token_count].value[63] = 0;
    token_count++;
}

static int is_keyword(const char *s, TokenType *out) {
    if (strcmp(s, "let")    == 0) { *out = TOK_LET;    return 1; }
    if (strcmp(s, "fn")     == 0) { *out = TOK_FN;     return 1; }
    if (strcmp(s, "return") == 0) { *out = TOK_RETURN; return 1; }
    if (strcmp(s, "if")     == 0) { *out = TOK_IF;     return 1; }
    if (strcmp(s, "elif")   == 0) { *out = TOK_ELIF;   return 1; }
    if (strcmp(s, "else")   == 0) { *out = TOK_ELSE;   return 1; }
    if (strcmp(s, "while")  == 0) { *out = TOK_WHILE;  return 1; }
    if (strcmp(s, "for")    == 0) { *out = TOK_FOR;    return 1; }
    if (strcmp(s, "to")     == 0) { *out = TOK_TO;     return 1; }
    if (strcmp(s, "step")   == 0) { *out = TOK_STEP;   return 1; }
    if (strcmp(s, "end")    == 0) { *out = TOK_END;    return 1; }
    if (strcmp(s, "print")  == 0) { *out = TOK_PRINT;  return 1; }
    if (strcmp(s, "int")    == 0) { *out = TOK_TINT;   return 1; }
    if (strcmp(s, "float")  == 0) { *out = TOK_TFLOAT; return 1; }
    if (strcmp(s, "str")    == 0) { *out = TOK_TSTR;   return 1; }
    if (strcmp(s, "ptr")    == 0) { *out = TOK_TPTR;   return 1; }
    if (strcmp(s, "bool")   == 0) { *out = TOK_TBOOL;  return 1; }
    if (strcmp(s, "true")   == 0) { *out = TOK_TRUE;   return 1; }
    if (strcmp(s, "false")  == 0) { *out = TOK_FALSE;  return 1; }
    return 0;
}

void tokenize(const char *src) {
    int i = 0;
    int len = strlen(src);
    token_count = 0;

    while (i < len) {
        char c = src[i];

        // skip whitespace and newlines
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            i++;
            continue;
        }

        // skip comments (#)
        if (c == '#') {
            while (i < len && src[i] != '\n') i++;
            continue;
        }

        // number
        if (isdigit(c)) {
            char buf[64];
            int  j = 0;
            while (i < len && isdigit(src[i]))
                buf[j++] = src[i++];
            buf[j] = 0;
            add_token(TOK_NUMBER, buf);
            continue;
        }

        // identifier or keyword
        if (isalpha(c) || c == '_') {
            char buf[64];
            int  j = 0;
            while (i < len && (isalnum(src[i]) || src[i] == '_'))
                buf[j++] = src[i++];
            buf[j] = 0;
            TokenType kw;
            if (is_keyword(buf, &kw))
                add_token(kw, buf);
            else
                add_token(TOK_IDENT, buf);
            continue;
        }

        // string literal
        if (c == '"') {
            char buf[256];
            int  j = 0;
            i++;    // skip opening quote
            while (i < len && src[i] != '"' && j < 255) {
                buf[j++] = src[i++];
            }
            if (i < len) i++;   // skip closing quote
            buf[j] = 0;
            add_token(TOK_STRING, buf);
            continue;
        }

        // two-char operators
        if (i + 1 < len) {
            char next = src[i+1];
            if (c == '=' && next == '=') { add_token(TOK_EQEQ, "=="); i += 2; continue; }
            if (c == '!' && next == '=') { add_token(TOK_NEQ,  "!="); i += 2; continue; }
            if (c == '>' && next == '=') { add_token(TOK_GTE,  ">="); i += 2; continue; }
            if (c == '<' && next == '=') { add_token(TOK_LTE,  "<="); i += 2; continue; }
            if (c == '-' && next == '>') { add_token(TOK_ARROW, "->"); i += 2; continue; }
        }

        // single-char operators and delimiters
        switch (c) {
            case '=': add_token(TOK_EQ,     "=");  break;
            case '+': add_token(TOK_PLUS,   "+");  break;
            case '-': add_token(TOK_MINUS,  "-");  break;
            case '*': add_token(TOK_STAR,   "*");  break;
            case '/': add_token(TOK_SLASH,  "/");  break;
            case '>': add_token(TOK_GT,     ">");  break;
            case '<': add_token(TOK_LT,     "<");  break;
            case '(': add_token(TOK_LPAREN, "(");  break;
            case ')': add_token(TOK_RPAREN, ")");  break;
            case ',': add_token(TOK_COMMA,  ",");  break;
            case ':': add_token(TOK_COLON,  ":");  break;
            default:
                fprintf(stderr, "Lexer error: unknown character '%c'\n", c);
                exit(1);
        }
        i++;
    }

    add_token(TOK_EOF, "");
}
