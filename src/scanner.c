#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

void initScanner(Scanner* scanner, const char* source) {
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
    scanner->str_depth = 0;
    scanner->in_str = false;
}

// Returns true if at end of source code
static bool isAtEnd(const char* current) {
    return *current == '\0';
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
static char advance(Scanner* scanner) {
    scanner->current++;
    return scanner->current[-1];
}

// Looks at current char
static char peek(Scanner* scanner) {
    return *scanner->current;
}

// Looks at next char
static char peekNext(Scanner* scanner) {
    if (isAtEnd(scanner->current)) return '\0';
    return scanner->current[1];
}

// Check if next char is expected, and if it is consume it
static bool match(Scanner* scanner, char expected) {
    if (isAtEnd(scanner->current)) return false;
    if (*scanner->current != expected) return false;
    scanner->current++;
    return true;
}

// Makes a token of type 'type'
static Token makeToken(Scanner* scanner, TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner->start;
    token.length = (int)(scanner->current - scanner->start);
    token.line = scanner->line;
    return token;
}

// Makes a token of type 'type'
// Adds one character of padding so the compiler doesnt cut off
// part of the string during interpolation
static Token makeTokenInterpolate(Scanner* scanner, TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner->start;
    token.length = (int)(scanner->current - scanner->start + 1);
    token.line = scanner->line;
    return token;
}

// Makes an error token
static Token errorToken(Scanner* scanner, const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner->line;
    return token;
}

// Skips whitespace in source code
static void skipWhitespace(Scanner* scanner) {
    for (;;) {
        char c = peek(scanner);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(scanner);
                break;
            
            // Keep track of line
            case '\n':
                scanner->line++;
                advance(scanner);
                break;
            
            // Look for comments
            case '/':
                if (peekNext(scanner) == '/') { // Single line comment
                    while (peek(scanner) != '\n' && !isAtEnd(scanner->current)) advance(scanner);
                } else if (peekNext(scanner) == '*') { // Multi line comment
                    while (!isAtEnd(scanner->current) && (peek(scanner) != '*' || peekNext(scanner) != '/')) {
                        if (peek(scanner) == '\n') scanner->line++;
                        advance(scanner);
                    }
                    // Consume "*/"
                    advance(scanner);
                    advance(scanner);
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
static TokenType checkKeyword(const char* checkStart, const char* checkCurrent, int start, int length, const char* rest, TokenType type) {
    if (checkCurrent - checkStart == start + length && memcmp(checkStart + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

// Determine correct type of identifier/keyword
static TokenType identifierType(Scanner* scanner) {
    // Check if token is a keywords
    switch (scanner->start[0]) {
        case 'a': return checkKeyword(scanner->start, scanner->current, 1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(scanner->start, scanner->current, 1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a': return checkKeyword(scanner->start, scanner->current, 2, 2, "se", TOKEN_CASE);
                    case 'l': return checkKeyword(scanner->start, scanner->current, 2, 3, "ass", TOKEN_CLASS);
                    case 'o': return checkKeyword(scanner->start, scanner->current, 2, 6, "ntinue", TOKEN_CONTINUE);
                }
            }
            break;
        case 'd':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'e': if (scanner->current - scanner->start > 2) {
                        switch (scanner->start[2]) {
                            case 'f': return checkKeyword(scanner->start, scanner->current, 3, 4, "ault", TOKEN_DEFAULT);
                            case 'l': return checkKeyword(scanner->start, scanner->current, 3, 0, "", TOKEN_DEL);
                        }
                    }
                    case 'o': return checkKeyword(scanner->start, scanner->current, 2, 0, "", TOKEN_DO);
                }
            }
        case 'e':
            if (scanner->current - scanner->start > 2 && scanner->start[1] == 'l') {
                switch (scanner->start[2]) {
                    case 'i': return checkKeyword(scanner->start, scanner->current, 3, 1, "f", TOKEN_ELIF);
                    case 's': return checkKeyword(scanner->start, scanner->current, 3, 1, "e", TOKEN_ELSE);
                }
            }
            break;
        case 'f':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a': return checkKeyword(scanner->start, scanner->current, 2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(scanner->start, scanner->current, 2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(scanner->start, scanner->current, 2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i': return checkKeyword(scanner->start, scanner->current, 1, 1, "f", TOKEN_IF);
        case 'n': return checkKeyword(scanner->start, scanner->current, 1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(scanner->start, scanner->current, 1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(scanner->start, scanner->current, 1, 4, "rint", TOKEN_PRINT);
        case 'r': return checkKeyword(scanner->start, scanner->current, 1, 5, "eturn", TOKEN_RETURN);
        case 's': 
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'w': return checkKeyword(scanner->start, scanner->current, 2, 4, "itch", TOKEN_SWITCH);
                    case 'u': return checkKeyword(scanner->start, scanner->current, 2, 3, "per", TOKEN_SUPER);
                }
            }
            break;
        case 't':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'h': return checkKeyword(scanner->start, scanner->current, 2, 2, "is", TOKEN_THIS);
                    case 'r': return checkKeyword(scanner->start, scanner->current, 2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v': return checkKeyword(scanner->start, scanner->current, 1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(scanner->start, scanner->current, 1, 4, "hile", TOKEN_WHILE);
    }
    // Fell through, must be a variable name
    return TOKEN_IDENTIFIER;
}

// Create a identifier token or keyword token from source code
static Token identifier(Scanner* scanner) {
    while (isAlpha(peek(scanner)) || isDigit(peek(scanner))) advance(scanner);
    return makeToken(scanner, identifierType(scanner));
}

// Create a number literal from source code
static Token number(Scanner* scanner) {
    while (isDigit(peek(scanner))) advance(scanner);

    // Look for decimal point
    if (peek(scanner) == '.' && isDigit(peekNext(scanner))) {
        // Consume '.'
        advance(scanner);

        while(isDigit(peek(scanner))) advance(scanner);
    }

    return makeToken(scanner, TOKEN_NUMBER);
}

// Extracts a string from source code
static Token string(Scanner* scanner) {
    while (peek(scanner) != '\"' && !isAtEnd(scanner->current) && (peek(scanner) != '$' || peekNext(scanner) != '{')) {
        if (peek(scanner) == '\n') scanner->line++;
        advance(scanner);
    }

    // Exit in_str
    scanner->in_str = false;

    // Check if string interpolation found
    if (peek(scanner) == '$' && peekNext(scanner) == '{') {
        // Go one layer deeper
        scanner->str_depth++;
        // The LAAAAAAaaazzy Way
        return makeTokenInterpolate(scanner, TOKEN_STRING);
    }
    // No string interpolation found
    // Check if at end
    if (isAtEnd(scanner->current)) return errorToken(scanner, "Unterminated string");

    // Consume '"'
    advance(scanner);

    // Make token
    return makeToken(scanner, TOKEN_STRING);
}

// Checks if right brace is string interpolation or a scope
static Token rightBrace(Scanner* scanner) {
    // Check if in str interpolation
    if (scanner->str_depth > 0) {
        // Exit layer of str
        scanner->str_depth--;
        // Check for empty string after interpolation
        if (!match(scanner, '\"')) {
            scanner->in_str = true;
        }
    }
    return makeToken(scanner, TOKEN_RIGHT_BRACE);
}

Token scanToken(Scanner* scanner) {
    // Check if scanner is in the middle of a string
    if (scanner->in_str) {
        scanner->start = scanner->current - 1;
        return string(scanner);
    }

    skipWhitespace(scanner);
    scanner->start = scanner->current;

    if(isAtEnd(scanner->current)) return makeToken(scanner, TOKEN_EOF);

    char c = advance(scanner);
    // Check if c is an identifier or Keyword
    if (isAlpha(c)) return identifier(scanner);
    // Check if c is a number literal
    if (isDigit(c)) return number(scanner);

    switch (c) {
        // Single character tokens
        case '(': return makeToken(scanner, TOKEN_LEFT_PAREN);
        case ')': return makeToken(scanner, TOKEN_RIGHT_PAREN);
        case '{': return makeToken(scanner, TOKEN_LEFT_BRACE);
        case '}': return rightBrace(scanner);
        case '[': return makeToken(scanner, TOKEN_LEFT_SQUARE);
        case ']': return makeToken(scanner, TOKEN_RIGHT_SQUARE);
        case ':': return makeToken(scanner, TOKEN_COLON);
        case ';': return makeToken(scanner, TOKEN_SEMICOLON);
        case ',': return makeToken(scanner, TOKEN_COMMA);
        case '.': return makeToken(scanner, TOKEN_DOT);
        case '?': return makeToken(scanner, TOKEN_QUESTION_MARK);

        // Single or double character tokens
        case '!': return makeToken(scanner, match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return makeToken(scanner, match(scanner, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return makeToken(scanner, match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return makeToken(scanner, match(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

        case '+': return makeToken(scanner, match(scanner, '=') ? TOKEN_PLUS_EQUAL : TOKEN_PLUS);
        case '-': return makeToken(scanner, match(scanner, '=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS);
        case '/': return makeToken(scanner, match(scanner, '=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
        case '*': return makeToken(scanner, match(scanner, '=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);
        case '%': return makeToken(scanner, match(scanner, '=') ? TOKEN_PERCENT_EQUAL : TOKEN_PERCENT);

        // Strings
        case '\"': return string(scanner);
        // String interpolation
        case '$': {
            if (scanner->str_depth > 0 && match(scanner, '{'))
                return makeToken(scanner, TOKEN_DOLLAR_BRACE);
            break;
        }
    }

    return errorToken(scanner, "Unexpected character");
}