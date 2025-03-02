#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

#define BREAK_MAX 64
#define CONTINUE_MAX 64

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_CONDITIONAL, // ?:
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY      // Primary value
} Precedence;

typedef void (*ParseFn)(bool canAssign);

// Holds functions for a token
typedef struct {
    ParseFn prefix;
    ParseFn anyfix;
    Precedence precedence;
} ParseRule;

// Local variable struct
typedef struct {
    Token name;
    int depth;
} Local;

// FlowControl struct
// For breaks/continues
typedef struct {
    int location;
    int depth;
} FlowControl;

// Keeps track of local variables
// Ideally would pass it through the functions - would allow for multithreading
typedef struct {
    // Local variable control
    Local locals[STACK_MAX];
    int localCount;
    int scopeDepth;
    // Break/Continue flow control
    FlowControl breaks[BREAK_MAX];
    int breakCount;
    FlowControl continues[CONTINUE_MAX];
    int continueCount;
} Compiler;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;

/* 
    ----------------
    HELPER FUNCTIONS 
    ----------------
*/

// Init a compiler
static void initCompiler(Compiler* compiler) {
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->breakCount = 0;
    compiler->continueCount = 0;
    current = compiler;
}

// Returns current compiler
static Chunk* currentChunk() {
    return compilingChunk;
}

// Report an error and set panicMode and hadError to true
static void errorAt(Token* token, const char* message) {
    // Supress all other errors if in panic mode
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // EMPTY
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

// Report error
static void error(const char* message) {
    errorAt(&parser.previous, message);
}

// Report error
static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

// Parse current token, checking for error tokens
static void advance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

// Consume current token, throwing error if not expected type
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    // Throw error
    errorAtCurrent(message);
}

// Check current token matches 'type'
static bool check(TokenType type) {
    return parser.current.type == type;
}

// Checks if current token is of type 'type' and advances if it is
static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// Checks if current token is in a range of tokentypes
// Takes advantage that similar tokens are close together
static bool matchRange(TokenType floor, TokenType ceil) {
    if (parser.current.type < floor || parser.current.type > ceil) return false;
    advance();
    return true;
}

// Get the number of locals in current scope
static int numLocals(int depth) {
    uint32_t n = 0;
    for (int i = current->localCount; i > 0 && current->locals[i - 1].depth >= depth; i--) {
        n++;
    }
    return n;
}

/* 
    ---------------
    Byte Code Write 
    ---------------
*/

// Emit a single byte code
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

/*
    Emit a jump with a temperary jump distance

    Returns (int) position of jump instruction
*/
static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 3;
}

/*
    Emit a backwards jump for a loop
*/
static void emitLoop(uint8_t instruction, int loopStart) {
    emitByte(instruction);

    int jumpDist = currentChunk()->count - loopStart + 2;
    if (jumpDist > UINT16_MAX) error("Loop body too large");

    emitByte((jumpDist >> 8) & 0xff);
    emitByte(jumpDist & 0xff);
}

// Emit a long type byte operation
static void emitLong(uint8_t op, uint32_t pos) {
    emitByte(op);
    emitByte((pos >> 16) & 0xff);
    emitByte((pos >> 8) & 0xff);
    emitByte(pos & 0xff);
}

// Emit a break/continue command with temp value
static int emitControlFlow(int depth) {
    // Emit jump field
    int location = emitJump(OP_JUMP);

    int n = numLocals(depth);
    // Check if NPOP field is needed
    if (n > 0) {
        currentChunk()->code[currentChunk()->count - 3] = OP_JUMP_NPOP;
        emitByte((n >> 16) & 0xff);
        emitByte((n >> 8) & 0xff);
        emitByte(n & 0xff);
    }
    return location;
}
// Emit a break command
static void emitBreak(int depth) {
    if (current->breakCount == BREAK_MAX) {
        error("Too many breaks in current loop");
        return;
    }
    // Get location of break for patch in later
    int location = emitControlFlow(depth);
    current->breaks[current->breakCount] = (FlowControl){location, current->scopeDepth};
    current->breakCount++;
}
// Emit a continue command
static void emitContinue(int depth) {
    if (current->continueCount == CONTINUE_MAX) {
        error("Too many continues in current loop");
        return;
    }
    // Get location of continue for patch in later
    int location = emitControlFlow(depth);
    current->continues[current->continueCount] = (FlowControl){location, current->scopeDepth};
    current->continueCount++;
}

// Adds return byte code
static void emitReturn() {
    emitByte(OP_RETURN);
}

// Make a constant
static uint32_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT24_COUNT) {
        error("Too many unique constants in one chunk");
        return 0;
    }

    return constant;
}

// Emit a constant value
static void emitConstant(Value value) {
    uint32_t pos = makeConstant(value);
    if (pos > UINT8_MAX) {
        emitLong(OP_CONSTANT_LONG, pos);
    } else {
        emitBytes(OP_CONSTANT, pos);
    }
}

// Fills in the temporary jump distance in a jump command
// 'location' (int) : location of the jump op byte code
static void patchJump(int location) {
    // Calculate jump distance
    int jumpDist = currentChunk()->count - location - 3;
    // Offset of NPOP if needed
    if (currentChunk()->code[location] == OP_JUMP_NPOP) jumpDist -= 3;
    // Check if jumpDist is too large
    if (jumpDist > UINT16_MAX) {
        error("Too much code to jump over");
    }
    // Patch jump field
    currentChunk()->code[location + 1] = (jumpDist >> 8) & 0xff;
    currentChunk()->code[location + 2] = jumpDist & 0xff;
}

// Fills in all temporary break command jumps
// They will all point to currentChunk()->count
static void patchBreaks() {
    // Patch all breaks at or above current depth
    int depth = current->scopeDepth;
    while (current->breakCount > 0 && current->breaks[current->breakCount - 1].depth > depth) {
        current->breakCount--;
        patchJump(current->breaks[current->breakCount].location);
    }
}
// Fills in all temporary continue command jumps
// They will all point to currentChunk()->count
static void patchContinues() {
    // Patch all continues
    int depth = current->scopeDepth;
    while (current->continueCount > 0 && current->continues[current->continueCount - 1].depth > depth) {
        current->continueCount--;
        patchJump(current->continues[current->continueCount].location);
    }
}

// Ends compiling stage
static void endCompiler() {
    emitReturn();

    // Debug flag check
    #ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
    #endif
}

// Adds new scope
static void beginScope() {
    current->scopeDepth++;
}

// Removes current scope
static void endScope() {
    // Number of times to pop
    uint32_t n = numLocals(current->scopeDepth);
    // Update current compiler
    current->localCount -= n;
    current->scopeDepth--;
    // Emit pop op
    if (n > 1) emitLong(OP_POPN, n);
    else if (n == 1) emitByte(OP_POP);
    
}

// Forwards declares
static void expression();
static void statement(int loopDepth);
static void declaration(int loopDepth);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

/* 
    ---------------
    OPERATION TYPES 
    ---------------
*/

// Parse a ternary operator
static void ternary(bool canAssign) {
    TokenType operator_left = parser.previous.type;
    ParseRule* rule = getRule(operator_left);

    // Chech type
    switch(operator_left) {
        case TOKEN_QUESTION_MARK: {
            // Set up jump
            int thenJump = emitJump(OP_JUMP_IF_FALSE);
            emitByte(OP_POP);
            // Parse middle branch
            parsePrecedence((Precedence)(rule->precedence));

            // Jump at end of then branch
            int elseJump = emitJump(OP_JUMP);

            // Patch first jump
            patchJump(thenJump);
            emitByte(OP_POP);

            // Consume ':'
            consume(TOKEN_COLON, "Expect ':' after '?'");
            // Parse right branch
            parsePrecedence((Precedence)(rule->precedence));

            // Patch jump over else branch
            patchJump(elseJump);
        }
    }
}

static void or_(bool canAssign) {
    // Check if short circuit
    int shortJump = emitJump(OP_JUMP_IF_TRUE);
    
    // Check right operand
    emitByte(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(shortJump);
}

static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

// Parse a binary operator
static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:      emitByte(OP_NOT_EQUAL); break;
        case TOKEN_EQUAL_EQUAL:     emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:         emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL:   emitByte(OP_GREATER_EQUAL); break;
        case TOKEN_LESS:            emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:      emitByte(OP_LESS_EQUAL); break;
        case TOKEN_PLUS:            emitByte(OP_ADD); break;
        case TOKEN_MINUS:           emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:            emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:           emitByte(OP_DIVIDE); break;
        case TOKEN_PERCENT:         emitByte(OP_MOD); break;
        default: return; // Unreachable
    }
}

// Parses a unary expression
static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    // Compile the operand
    parsePrecedence(PREC_UNARY);

    switch(operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_BANG: emitByte(OP_NOT); break;
        default: return; // Unreachable
    }
}

// Parse a grouping expression (expression)
static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}

// Parses a number literal
static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

// Parses a string interpolation statement
static void interpolation() {
    // Parse an expression, but do not allow assignment
    
}

// Parses a string
static void string(bool canAssign) {
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
    
    // Check for string interpolation
    if (match(TOKEN_DOLLAR_BRACE)) {
        parsePrecedence(PREC_CONDITIONAL);
        consume(TOKEN_RIGHT_BRACE, "Expect '}' after '?{' string interpolation");
        emitByte(OP_INTERPOLATE_STR);
        if (match(TOKEN_STRING)) {
            string(canAssign);
            emitByte(OP_INTERPOLATE_STR);
        }
    }
}

// Parses a literal value
static void literal(bool canAssign) {
    switch(parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return; // Unreachable
    }
}

// Make a new constant that stores the identifiers name
static uint32_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

// Check if two variable identifiers are the same
static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// Try to locate a local
static uint32_t resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Cannot read local variable in its own initializer");
            }
            return i;
        }
    }

    return -1;
}

// Add a local variable
static void addLocal(Token name) {
    if (current->localCount == STACK_MAX) {
        error("Too many local variables in function");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
}

// Make a local variable
static void declareVariable() {
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;

    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope");
        }
    }

    addLocal(*name);
}

// Assignment helper
static void assignVarWithOpLong(OpCode getOp, OpCode setOp, OpCode op, uint32_t arg) {
    emitLong(getOp + 1, arg);
    expression();
    emitByte(op);
    emitLong(setOp + 1, arg);
}
// Assignment helper
static void assignVarWithOp(OpCode getOp, OpCode setOp, OpCode op, uint32_t arg) {
    emitBytes(getOp, (uint8_t)arg);
    expression();
    emitByte(op);
    emitBytes(setOp, (uint8_t)arg);
}

// Access or assign a variable
static void namedVariable(Token name, bool canAssign) {
    // Select correct get and set operators for global vs local
    uint8_t getOp, setOp;
    uint32_t arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (arg < UINT8_MAX) { // ********************* COULD LOOK INTO USING MATCHRANGE FOR POTENTIAL OPTIMIZATION *************************
        if (canAssign) { // Byte Case
            if (match(TOKEN_EQUAL)) {
                expression();
                emitBytes(setOp, (uint8_t)arg);
            } else if (match(TOKEN_PLUS_EQUAL)) assignVarWithOp(getOp, setOp, OP_ADD, arg);
            else if (match(TOKEN_MINUS_EQUAL)) assignVarWithOp(getOp, setOp, OP_SUBTRACT, arg);
            else if (match(TOKEN_STAR_EQUAL)) assignVarWithOp(getOp, setOp, OP_MULTIPLY, arg);
            else if (match(TOKEN_SLASH_EQUAL)) assignVarWithOp(getOp, setOp, OP_DIVIDE, arg);
            else if (match(TOKEN_PERCENT_EQUAL)) assignVarWithOp(getOp, setOp, OP_MOD, arg);
            else emitBytes(getOp, (uint8_t)arg);
        } else {
            emitBytes(getOp, (uint8_t)arg);
        }
    } else { // Long Case
        if (canAssign) {
            if (match(TOKEN_EQUAL)) { 
                expression();
                emitLong(setOp + 1, arg);
            } else if (match(TOKEN_PLUS_EQUAL)) assignVarWithOpLong(getOp, setOp, OP_ADD, arg);
            else if (match(TOKEN_MINUS_EQUAL)) assignVarWithOpLong(getOp, setOp, OP_SUBTRACT, arg);
            else if (match(TOKEN_STAR_EQUAL)) assignVarWithOpLong(getOp, setOp, OP_MULTIPLY, arg);
            else if (match(TOKEN_SLASH_EQUAL)) assignVarWithOpLong(getOp, setOp, OP_DIVIDE, arg);
            else if (match(TOKEN_PERCENT_EQUAL)) assignVarWithOpLong(getOp, setOp, OP_MOD, arg);
            else emitLong(getOp + 1, arg);
        } else {
            emitLong(getOp + 1, arg);
        }
    }
}

// Parses a variable call
static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

/* 
    ---------------
    GRAMMAR EXECUTION 
    ---------------
*/

// Grammar rules of cave
ParseRule rules[] = {
	[TOKEN_LEFT_PAREN]      = {grouping,    NULL,        PREC_NONE},
	[TOKEN_RIGHT_PAREN]     = {NULL,        NULL,        PREC_NONE},
	[TOKEN_LEFT_BRACE]      = {NULL,        NULL,        PREC_NONE},
	[TOKEN_RIGHT_BRACE]     = {NULL,        NULL,        PREC_NONE},
    [TOKEN_LEFT_SQUARE]     = {NULL,        NULL,        PREC_NONE},
    [TOKEN_RIGHT_SQUARE]    = {NULL,        NULL,        PREC_NONE},
	[TOKEN_COMMA]           = {NULL,        NULL,        PREC_NONE},
	[TOKEN_DOT]             = {NULL,        NULL,        PREC_NONE},
	[TOKEN_SEMICOLON]       = {NULL,        NULL,        PREC_NONE},
    [TOKEN_COLON]           = {NULL,        NULL,        PREC_NONE},
	[TOKEN_QUESTION_MARK]   = {NULL,        ternary,     PREC_CONDITIONAL},
	[TOKEN_BANG]            = {unary,       NULL,        PREC_NONE},
	[TOKEN_BANG_EQUAL]      = {NULL,        binary,      PREC_EQUALITY},
	[TOKEN_EQUAL]           = {NULL,        NULL,        PREC_NONE},
	[TOKEN_EQUAL_EQUAL]     = {NULL,        binary,      PREC_EQUALITY},
	[TOKEN_GREATER]         = {NULL,        binary,      PREC_COMPARISON},
	[TOKEN_GREATER_EQUAL]   = {NULL,        binary,      PREC_COMPARISON},
	[TOKEN_LESS]            = {NULL,        binary,      PREC_COMPARISON},
	[TOKEN_LESS_EQUAL]      = {NULL,        binary,      PREC_COMPARISON},
	[TOKEN_MINUS]           = {unary,       binary,      PREC_TERM},
	[TOKEN_MINUS_EQUAL]     = {NULL,        NULL,        PREC_NONE},
	[TOKEN_PLUS]            = {NULL,        binary,      PREC_TERM},
	[TOKEN_PLUS_EQUAL]      = {NULL,        NULL,        PREC_NONE},
	[TOKEN_SLASH]           = {NULL,        binary,      PREC_FACTOR},
	[TOKEN_SLASH_EQUAL]     = {NULL,        NULL,        PREC_NONE},
	[TOKEN_STAR]            = {NULL,        binary,      PREC_FACTOR},
	[TOKEN_STAR_EQUAL]      = {NULL,        NULL,        PREC_NONE},
	[TOKEN_PERCENT]         = {NULL,        binary,      PREC_FACTOR},
	[TOKEN_PERCENT_EQUAL]   = {NULL,        NULL,        PREC_NONE},
	[TOKEN_IDENTIFIER]      = {variable,    NULL,        PREC_NONE},
	[TOKEN_STRING]          = {string,      NULL,        PREC_NONE},
	[TOKEN_NUMBER]          = {number,      NULL,        PREC_NONE},
	[TOKEN_AND]             = {NULL,        and_,        PREC_AND},
	[TOKEN_BREAK]           = {NULL,        NULL,        PREC_NONE},
    [TOKEN_CASE]            = {NULL,        NULL,        PREC_NONE},
	[TOKEN_CLASS]           = {NULL,        NULL,        PREC_NONE},
	[TOKEN_CONTINUE]        = {NULL,        NULL,        PREC_NONE},
	[TOKEN_ELIF]            = {NULL,        NULL,        PREC_NONE},
	[TOKEN_ELSE]            = {NULL,        NULL,        PREC_NONE},
	[TOKEN_FALSE]           = {literal,     NULL,        PREC_NONE},
	[TOKEN_FOR]             = {NULL,        NULL,        PREC_NONE},
	[TOKEN_FUN]             = {NULL,        NULL,        PREC_NONE},
	[TOKEN_IF]              = {NULL,        NULL,        PREC_NONE},
	[TOKEN_NIL]             = {literal,     NULL,        PREC_NONE},
	[TOKEN_OR]              = {NULL,        or_,         PREC_OR},
	[TOKEN_PRINT]           = {NULL,        NULL,        PREC_NONE},
	[TOKEN_RETURN]          = {NULL,        NULL,        PREC_NONE},
	[TOKEN_SUPER]           = {NULL,        NULL,        PREC_NONE},
    [TOKEN_SWITCH]          = {NULL,        NULL,        PREC_NONE},
	[TOKEN_THIS]            = {NULL,        NULL,        PREC_NONE},
	[TOKEN_TRUE]            = {literal,     NULL,        PREC_NONE},
	[TOKEN_VAR]             = {NULL,        NULL,        PREC_NONE},
	[TOKEN_WHILE]           = {NULL,        NULL,        PREC_NONE},
	[TOKEN_ERROR]           = {NULL,        NULL,        PREC_NONE},
	[TOKEN_EOF]             = {NULL,        NULL,        PREC_NONE},
};

// Defines operator precedence
static void parsePrecedence(Precedence precedence) {
    advance();
    // Prefix operators
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expected an expression");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    // All other operators
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn anyfixRule = getRule(parser.previous.type)->anyfix;
        anyfixRule(canAssign);
    }

    if (canAssign && (matchRange(TOKEN_EQUAL, TOKEN_PERCENT_EQUAL))) {
        error("Invalid assignment target");
    }
}

// Parse a variable identifier
static uint32_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

// Initialize a local
static void markInitialized() {
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

// Adds a variable
static void defineVariable(uint32_t global) {
    // Check if local
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    if (global > UINT8_MAX) 
        emitLong(OP_DEFINE_GLOBAL_LONG, global);
    else
        emitBytes(OP_DEFINE_GLOBAL, global);
}

// Get rule of token type
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

// Parses an expression
static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

/* --- STATEMENTS --- */
// Block statement
static void block(int loopDepth) {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration(loopDepth);
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block");
}

// Init a variable
static void varDeclaraction() {
    uint32_t global = parseVariable("Expected Variable name");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    // Consume ';'
    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration");

    defineVariable(global);
}

static void ifStatement(int loopDepth) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect '(' after condition");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement(loopDepth);

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    // Look for else or elif
    if (match(TOKEN_ELSE)) statement(loopDepth);
    else if (match(TOKEN_ELIF)) ifStatement(loopDepth);
    
    patchJump(elseJump);
}

static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression");
    emitByte(OP_POP);
}

// Print statement, not a function
static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after value");
    emitByte(OP_PRINT);
}

// Flow Control Statements //
// Emit a temporary break statement to be patched at next loop/switch end
static void breakStatement(int loopDepth) {
    // Check if in loop
    if (loopDepth != -1) {
        emitBreak(loopDepth);
        consume(TOKEN_SEMICOLON, "Expect ';' after break statement");
    } else {
        // Throw error
        error("must be in a loop or switch");
    }
}
// Emit a temporary switch statement to be patched at next loop/switch end
static void continueStatement(int loopDepth) {
    // Check if in loop
    if (loopDepth != -1) {
        emitContinue(loopDepth);
        consume(TOKEN_SEMICOLON, "Expect ';' after break statement");
    } else {
        // Throw error
        error("must be in a loop");
    }
}

// While loop
static void whileStatement() {
    int loopStart = currentChunk()->count;
    // Get condition
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition");

    // Set up exit loop jump
    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    // Pop conditon
    emitByte(OP_POP);

    // Loop body with current scope depth + 1
    // Local scope to isolate body
    beginScope();
    statement(current->scopeDepth);
    endScope();;

    // Patch continue statements
    patchContinues();
    // Emit "loop"/jump to the condition
    emitLoop(OP_LOOP, loopStart);
    
    // Outside of loop
    patchJump(exitJump);
    emitByte(OP_POP);
    // Patch break statements
    patchBreaks();
}

// Do while loop
static void doStatement() {
    int loopStart = currentChunk()->count;

    // Compile body, give scopeDepth for break/continue 
    // Local scope to isolate body
    beginScope();
    statement(current->scopeDepth);
    endScope();
    // Patch continue statements
    patchContinues();

    // Check for 'while'
    consume(TOKEN_WHILE, "Expect 'while' after do loop body");
    // Compile conditon
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition");
    consume(TOKEN_SEMICOLON, "Expect ';' after condition");

    // Emit "loop"/jump to the start of the body, checks condition
    emitLoop(OP_LOOP_IF_TRUE, loopStart);

    // Outside of loop
    patchBreaks();
}

// For loop
static void forStatement() {
    // Set up scope for the loop
    beginScope();

    // Setup initializer if there is one
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'");
    if (match(TOKEN_SEMICOLON)) {
        // No initializer
    } else if (match(TOKEN_VAR)) {
        varDeclaraction();
    } else {
        expressionStatement();
    }

    // Get loop start
    int loopStart = currentChunk()->count;

    // Get condition if there is one
    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        // Get condition
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop  condition");

        // Jump out of the loop if the condition is false
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    // Check for increment clause
    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clause");

        emitLoop(OP_LOOP, loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    // Loop body, pass it scope depth for break/continue statements
    // Use scope to seperate break statements (in the case the use types for(...) break; for some reason)
    beginScope();
    statement(current->scopeDepth);
    endScope();

    // Continues point to loop back
    patchContinues();
    // Emit loop backwards
    emitLoop(OP_LOOP, loopStart);

    // Create exit jump if there is a condition
    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }
    // Outside of loop, breaks point here
    patchBreaks();
    // End the scope for the loop
    endScope();
}

// Switch statement
// Can take all data types
// switch(var) {
//  case 'a':
//      break;
//}
static void switchStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'");
    // Condition
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'switch'");
    consume(TOKEN_LEFT_BRACE, "Expect '{' after switch statement");

    Chunk switchBody;
    Chunk* mainChunk;
    initChunk(&switchBody);
    // Go through cases
    while (true) {
        if (match(TOKEN_CASE)) { // Regular case
            //
        } else if (match(TOKEN_DEFAULT)) { // Default fallthrough
            //
        } else {
            // No more cases or default
            break;
        }
    }

    // Closing '}'
    consume(TOKEN_RIGHT_BRACE, "Expect '}' at end of switch statement");
}

// Synch the parser to the end or start of an expression so it better report errors
static void synchronize() {
    parser.panicMode = false;

    // Look for end of a statement
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_SWITCH:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            
            default:
                ; // Do nothing
        }
        advance();
    }
}

// Parses a declaration
static void declaration(int loopDepth) {
    if (match(TOKEN_VAR)) {
        varDeclaraction();
    } else {
        statement(loopDepth);
    }

    if (parser.panicMode) synchronize();
}

// Parses a statement
static void statement(int loopDepth) {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
            beginScope();
            block(loopDepth);
            endScope();
    } else if (match(TOKEN_IF)) {
        ifStatement(loopDepth);
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_DO)) {
        doStatement();
    } else if (match(TOKEN_BREAK)) {
        breakStatement(loopDepth);
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement(loopDepth);
    } else if (match(TOKEN_SWITCH)) {
        switchStatement();
    } else {
        expressionStatement();
    }
}

/* 
    ---------------
    COMPILE MAIN 
    ---------------
*/

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler);
    compilingChunk = chunk;

    // Init parser
    parser.hadError = false;
    parser.panicMode = false;
    
    advance();
    
    // Compile until end of file
    // -1 to signify not currently in a loop
    while (!match(TOKEN_EOF)) {
        declaration(-1);
    }

    consume(TOKEN_EOF, "Expect end of expression");
    endCompiler();
    return !parser.hadError;
}

#undef BREAK_MAX
#undef CONTINUE_MAX