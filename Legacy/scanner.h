#ifndef cave_scanner_h
#define cave_scanner_h

// Defines the type of token
typedef enum {
    // Single character tokens
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_SQUARE, TOKEN_RIGHT_SQUARE,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_SEMICOLON,
    TOKEN_COLON, TOKEN_QUESTION_MARK,

    // One or two character tokens
    // Assignment Tokens
    TOKEN_EQUAL, TOKEN_MINUS_EQUAL,
    TOKEN_PLUS_EQUAL, TOKEN_SLASH_EQUAL,
    TOKEN_STAR_EQUAL, TOKEN_PERCENT_EQUAL,
    TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS, // NOT USED

    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    // Arithmatic
    TOKEN_MINUS, 
    TOKEN_PLUS, 
    TOKEN_SLASH, 
    TOKEN_STAR, 
    TOKEN_PERCENT,
    // String Interpolation
    TOKEN_DOLLAR_BRACE,

    // Literals
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

    // Keywords
    TOKEN_AND, TOKEN_BREAK, TOKEN_CASE, TOKEN_CLASS, TOKEN_CONTINUE, TOKEN_DEFAULT, TOKEN_DO,
    TOKEN_ELIF, TOKEN_ELSE, TOKEN_FALSE, TOKEN_FOR, TOKEN_FUN, TOKEN_IF,
    TOKEN_NIL, TOKEN_OR, TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_SWITCH,
    TOKEN_THIS, TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,

    TOKEN_ERROR, TOKEN_EOF
} TokenType;

// Stores token
typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

void initScanner(const char* source);
Token scanToken();

#endif