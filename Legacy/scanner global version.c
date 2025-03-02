#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    const char* start;
    const char* current;
    int line;
    int str_depth;
    bool in_str;
} Scanner;

Scanner scanner;

void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
    scanner.str_depth = 0;
    scanner.in_str = false;
}

// Returns true if at end of source code
static bool isAtEnd() {
    return *scanner.current == '\0';
}

// Returns true if char c is a letter
static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
            c == '_';
}

// Returns true if char c is a number
static bool isDigit(char c) {
    return c >= '0' && c <= '9';    
}

// Consumes current character
static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

// Looks at current char
static char peek() {
    return *scanner.current;
}

// Looks at next char
static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

// Check if next char is expected, and if it is consume it
static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

// Makes a token of type 'type'
static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

// Makes a token of type 'type'
// Adds one character of padding so the compiler doesnt cut off
// part of the string during interpolation
static Token makeTokenInterpolate(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start + 1);
    token.line = scanner.line;
    return token;
}

// Makes an error token
static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

// Skips whitespace in source code
static void skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            
            // Keep track of line
            case '\n':
                scanner.line++;
                advance();
                break;
            
            // Look for comments
            case '/':
                if (peekNext() == '/') { // Single line comment
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else if (peekNext() == '*') { // Multi line comment
                    while (!isAtEnd() && (peek() != '*' || peekNext() != '/')) {
                        if (peek() == '\n') scanner.line++;
                        advance();
                    }
                    // Consume "*/"
                    advance();
                    advance();
                } else {
                    return;
                }
                break;

            // Not white space, break
            default:
                return;
        }
    }
}

// Checks for keyword
static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    if (scanner.current - scanner.start == start + length && memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

// Determine correct type of identifier/keyword
static TokenType identifierType() {
    // Check if token is a keywords
    switch (scanner.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 2, "se", TOKEN_CASE);
                    case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                    case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
                }
            }
            break;
        case 'd':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'e': return checkKeyword(2, 5, "fault", TOKEN_DEFAULT);
                    case 'o': return checkKeyword(2, 0, "", TOKEN_DO);
                }
            }
        case 'e':
            if (scanner.current - scanner.start > 2 && scanner.start[1] == 'l') {
                switch (scanner.start[2]) {
                    case 'i': return checkKeyword(3, 1, "f", TOKEN_ELIF);
                    case 's': return checkKeyword(3, 1, "e", TOKEN_ELSE);
                }
            }
            break;
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': 
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'w': return checkKeyword(2, 4, "itch", TOKEN_SWITCH);
                    case 'u': return checkKeyword(2, 3, "per", TOKEN_SUPER);
                }
            }
            break;
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    // Fell through, must be a variable name
    return TOKEN_IDENTIFIER;
}

// Create a identifier token or keyword token from source code
static Token identifier() {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

// Create a number literal from source code
static Token number() {
    while (isDigit(peek())) advance();

    // Look for decimal point
    if (peek() == '.' && isDigit(peekNext())) {
        // Consume '.'
        advance();

        while(isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}

// Extracts a string from source code
static Token string() {
    while (peek() != '\"' && !isAtEnd() && (peek() != '$' || peekNext() != '{')) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    // Exit string mode
    scanner.in_str = false;

    // Check if string interpolation found
    if (peek() == '$' && peekNext() == '{') {
        // Go one layer deeper
        scanner.str_depth++;
        // The LAAAAAAaaazzy Way
        return makeTokenInterpolate(TOKEN_STRING);
    }
    // No string interpolation found
    // Check if at end
    if (isAtEnd()) return errorToken("Unterminated string");

    // Consume '"'
    advance();
    // Check if it needs to exit in_str mode
    if (scanner.in_str) {
        // Exit one layer of string
        scanner.str_depth--;
    }
    // Make token
    return makeToken(TOKEN_STRING);
}

// Checks if right brace is string interpolation or a scope
static Token rightBrace() {
    // Check if in str interpolation
    if (scanner.str_depth > 0) {
        // Check for empty string after interpolation
        if (match('\"')) {
            scanner.str_depth--;
        }
        // Exit interpolation portion normally
        else scanner.in_str = true;
    }
    return makeToken(TOKEN_RIGHT_BRACE);
}

Token scanToken() {
    // Check if scanner is in the middle of a string
    if (scanner.in_str) {
        scanner.start = scanner.current - 1;
        return string();
    }

    skipWhitespace();
    scanner.start = scanner.current;

    if(isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    // Check if c is an identifier or Keyword
    if (isAlpha(c)) return identifier();
    // Check if c is a number literal
    if (isDigit(c)) return number();

    switch (c) {
        // Single character tokens
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return rightBrace();
        case '[': return makeToken(TOKEN_LEFT_SQUARE);
        case ']': return makeToken(TOKEN_RIGHT_SQUARE);
        case ':': return makeToken(TOKEN_COLON);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '?': return makeToken(TOKEN_QUESTION_MARK);

        // Single or double character tokens
        case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

        case '+': return makeToken(match('=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
        case '-': return makeToken(match('=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS);
        case '/': return makeToken(match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
        case '*': return makeToken(match('=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);
        case '%': return makeToken(match('=') ? TOKEN_PERCENT_EQUAL : TOKEN_PERCENT);

        // Strings
        case '\"': return string();
        // String interpolation
        case '$': {
            if (scanner.str_depth > 0 && match('{'))
                return makeToken(TOKEN_DOLLAR_BRACE);
            break;
        }
    }

    return errorToken("Unexpected character");
}