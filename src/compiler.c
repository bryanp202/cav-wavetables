#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

#define BREAK_MAX 256
#define CONTINUE_MAX 256

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

// Script or Function
typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
} FunctionType;

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
    // Function
    ObjFunction* function;
    FunctionType type;

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

// Lookup table parce precedence type
typedef void (*ParseFn)(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign);

// Holds functions for a token
typedef struct {
    ParseFn prefix;
    ParseFn anyfix;
    Precedence precedence;
} ParseRule;

/* 
    ----------------
    HELPER FUNCTIONS 
    ----------------
*/

// Init parser
static void initParser(Parser* parser) {
    parser->hadError = false;
    parser->panicMode = false;
}

// Init a compiler
static void initCompiler(Compiler* compiler, Parser* parser, FunctionType type) {
    // Clear fields
    compiler->function = NULL;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->breakCount = 0;
    compiler->continueCount = 0;
    // Initialize function
    compiler->type = type;
    compiler->function = newFunction();
    // Check type
    if (type != TYPE_SCRIPT) {
        compiler->function->name = copyString(parser->previous.start, parser->previous.length);
    }

    Local* local = &compiler->locals[compiler->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
}

// Report an error and set panicMode and hadError to true
static void errorAt(Parser* parser, Token* token, const char* message) {
    // Supress all other errors if in panic mode
    if (parser->panicMode) return;
    parser->panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // EMPTY
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser->hadError = true;
}

// Report error
static void error(Parser* parser, const char* message) {
    errorAt(parser, &parser->previous, message);
}

// Report error
static void errorAtCurrent(Parser* parser, const char* message) {
    errorAt(parser, &parser->current, message);
}

// Parse current token, checking for error tokens
static void advance(Parser* parser, Scanner* scanner) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = scanToken(scanner);
        if (parser->current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser, parser->current.start);
    }
}

// Consume current token, throwing error if not expected type
static void consume(Parser* parser, Scanner* scanner, TokenType type, const char* message) {
    if (parser->current.type == type) {
        advance(parser, scanner);
        return;
    }
    // Throw error
    errorAtCurrent(parser, message);
}

// Check current token matches 'type'
static bool check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

// Checks if current token is of type 'type' and advances if it is
static bool match(Parser* parser, Scanner* scanner, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser, scanner);
    return true;
}

// Checks if current token is in a range of tokentypes
// Takes advantage that similar tokens are close together
static bool matchRange(Parser* parser, Scanner* scanner, TokenType floor, TokenType ceil) {
    if (parser->current.type < floor || parser->current.type > ceil) return false;
    advance(parser, scanner);
    return true;
}

// Get the number of locals in current scope
static int numLocals(Compiler* compiler, int depth) {
    uint32_t n = 0;
    for (int i = compiler->localCount; i > 0 && compiler->locals[i - 1].depth >= depth; i--) {
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
static void emitByte(Parser* parser, Chunk* chunk, uint8_t byte) {
    writeChunk(chunk, byte, parser->previous.line);
}

static void emitBytes(Parser* parser, Chunk* chunk, uint8_t byte1, uint8_t byte2) {
    emitByte(parser, chunk, byte1);
    emitByte(parser, chunk, byte2);
}

/*
    Emit a jump with a temperary jump distance

    Returns (int) position of jump instruction
*/
static int emitJump(Parser* parser, Chunk* chunk, uint8_t instruction) {
    emitByte(parser, chunk, instruction);
    emitByte(parser, chunk, 0xff);
    emitByte(parser, chunk, 0xff);
    return chunk->count - 3;
}

/*
    Emit a backwards jump for a loop
*/
static void emitLoop(Parser* parser, Chunk* chunk, uint8_t instruction, int loopStart) {
    emitByte(parser, chunk, instruction);

    int jumpDist = chunk->count - loopStart + 2;
    if (jumpDist > UINT16_MAX) error(parser, "Loop body too large");

    emitByte(parser, chunk, (jumpDist >> 8) & 0xff);
    emitByte(parser, chunk, jumpDist & 0xff);
}

// Emit a long type byte operation
static void emitLong(Parser* parser, Chunk* chunk, uint8_t op, uint32_t pos) {
    if (pos >= UINT24_COUNT) {
        error(parser, "Too large of a value to write to long operation");
        return;
    }
    emitByte(parser, chunk, op);
    emitByte(parser, chunk, (pos >> 16) & 0xff);
    emitByte(parser, chunk, (pos >> 8) & 0xff);
    emitByte(parser, chunk, pos & 0xff);
}

// Emit a break/continue command with temp value
static int emitControlFlow(Compiler* compiler, Parser* parser, int depth) {
    // Emit jump field
    int location = emitJump(parser, &compiler->function->chunk, OP_JUMP);

    int n = numLocals(compiler, depth);
    // Check if NPOP field is needed
    if (n > 0) {
        compiler->function->chunk.code[compiler->function->chunk.count - 3] = OP_JUMP_NPOP;
        emitByte(parser, &compiler->function->chunk, (n >> 16) & 0xff);
        emitByte(parser, &compiler->function->chunk, (n >> 8) & 0xff);
        emitByte(parser, &compiler->function->chunk, n & 0xff);
    }
    return location;
}
// Emit a break command
static void emitBreak(Compiler* compiler, Parser* parser, int depth) {
    if (compiler->breakCount == BREAK_MAX) {
        error(parser, "Too many breaks in current loop");
        return;
    }
    // Get location of break for patch in later
    int location = emitControlFlow(compiler, parser, depth);
    compiler->breaks[compiler->breakCount] = (FlowControl){location, compiler->scopeDepth};
    compiler->breakCount++;
}
// Emit a continue command
static void emitContinue(Compiler* compiler, Parser* parser, int depth) {
    if (compiler->continueCount == CONTINUE_MAX) {
        error(parser, "Too many continues in current loop");
        return;
    }
    // Get location of continue for patch in later
    int location = emitControlFlow(compiler, parser, depth);
    compiler->continues[compiler->continueCount] = (FlowControl){location, compiler->scopeDepth};
    compiler->continueCount++;
}

// Adds return byte code
static void emitReturn(Parser* parser, Chunk* chunk) {
    emitByte(parser, chunk, OP_NIL);
    emitByte(parser, chunk, OP_RETURN);
}

// Make a constant
static uint32_t makeConstant(Parser* parser, Chunk* chunk, Value value) {
    int constant = addConstant(chunk, value);
    if (constant >= UINT24_COUNT) {
        error(parser, "Too many unique constants in one chunk");
        return 0;
    }

    return constant;
}

// Emit a constant value
static void emitConstant(Parser* parser, Chunk* chunk, Value value) {
    uint32_t pos = makeConstant(parser, chunk, value);
    if (pos > UINT8_MAX) {
        emitLong(parser, chunk, OP_CONSTANT_LONG, pos);
    } else {
        emitBytes(parser, chunk, OP_CONSTANT, pos);
    }
}

// Fills in the temporary jump distance in a jump command
// 'location' (int) : location of the jump op byte code
static void patchJump(Parser* parser, Chunk* chunk, int location) {
    // Calculate jump distance
    int jumpDist = chunk->count - location - 3;
    // Offset of NPOP if needed
    if (chunk->code[location] == OP_JUMP_NPOP) jumpDist -= 3;
    // Check if jumpDist is too large
    if (jumpDist > UINT16_MAX) {
        error(parser, "Too much code to jump over");
    }
    // Patch jump field
    chunk->code[location + 1] = (jumpDist >> 8) & 0xff;
    chunk->code[location + 2] = jumpDist & 0xff;
}

// Fills in all temporary break command jumps
// They will all point to chunk->count
static void patchBreaks(Compiler* compiler, Parser* parser) {
    // Patch all breaks at or above current depth
    int depth = compiler->scopeDepth;
    while (compiler->breakCount > 0 && compiler->breaks[compiler->breakCount - 1].depth > depth) {
        compiler->breakCount--;
        patchJump(parser, &compiler->function->chunk, compiler->breaks[compiler->breakCount].location);
    }
}
// Fills in all temporary continue command jumps
// They will all point to chunk->count
static void patchContinues(Compiler* compiler, Parser* parser) {
    // Patch all continues
    int depth = compiler->scopeDepth;
    while (compiler->continueCount > 0 && compiler->continues[compiler->continueCount - 1].depth > depth) {
        compiler->continueCount--;
        patchJump(parser, &compiler->function->chunk, compiler->continues[compiler->continueCount].location);
    }
}

// Ends compiling stage
static ObjFunction* endCompiler(Compiler* compiler, Parser* parser) {
    emitReturn(parser, &compiler->function->chunk);
    // Get function
    ObjFunction* function = compiler->function;

    // Debug flag check
    #ifdef DEBUG_PRINT_CODE
    if (!parser->hadError) {
        disassembleChunk(&function->chunk, function->name != NULL ? function->name->chars : "<script>");
    }
    #endif

    return function;
}

// Adds new scope
static void beginScope(Compiler* compiler) {
    compiler->scopeDepth++;
}

// Removes current scope
static void endScope(Compiler* compiler, Parser* parser) {
    // Number of times to pop
    uint32_t n = numLocals(compiler, compiler->scopeDepth);
    // Update current compiler
    compiler->localCount -= n;
    compiler->scopeDepth--;
    // Emit pop op
    if (n > 1) emitLong(parser, &compiler->function->chunk, OP_POPN, n);
    else if (n == 1) emitByte(parser, &compiler->function->chunk, OP_POP);
    
}

// Forwards declares
static void expression(Compiler* compiler, Parser* parser, Scanner* scanner);
static void stackVariable(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign);
static void statement(Compiler* compiler, Parser* parser, Scanner* scanner, int loopDepth);
static void declaration(Compiler* compiler, Parser* parser, Scanner* scanner, int loopDepth);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Compiler* compiler, Parser* parser, Scanner* scanner, Precedence precedence);

/* 
    ---------------
    OPERATION TYPES 
    ---------------
*/

// Parse a ternary operator
static void ternary(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    TokenType operator_left = parser->previous.type;
    ParseRule* rule = getRule(operator_left);

    // Chech type
    switch(operator_left) {
        case TOKEN_QUESTION_MARK: {
            // Set up jump
            int thenJump = emitJump(parser, &compiler->function->chunk, OP_JUMP_IF_FALSE);
            emitByte(parser, &compiler->function->chunk, OP_POP);
            // Parse middle branch
            parsePrecedence(compiler, parser, scanner, (Precedence)(rule->precedence));

            // Jump at end of then branch
            int elseJump = emitJump(parser, &compiler->function->chunk, OP_JUMP);

            // Patch first jump
            patchJump(parser, &compiler->function->chunk, thenJump);
            emitByte(parser, &compiler->function->chunk, OP_POP);

            // Consume ':'
            consume(parser, scanner, TOKEN_COLON, "Expect ':' after '?'");
            // Parse right branch
            parsePrecedence(compiler, parser, scanner, (Precedence)(rule->precedence));

            // Patch jump over else branch
            patchJump(parser, &compiler->function->chunk, elseJump);
        }
    }
}

static void or_(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    // Check if short circuit
    int shortJump = emitJump(parser, &compiler->function->chunk, OP_JUMP_IF_TRUE);
    
    // Check right operand
    emitByte(parser, &compiler->function->chunk, OP_POP);
    parsePrecedence(compiler, parser, scanner, PREC_OR);
    patchJump(parser, &compiler->function->chunk, shortJump);
}

static void and_(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    int endJump = emitJump(parser, &compiler->function->chunk, OP_JUMP_IF_FALSE);

    emitByte(parser, &compiler->function->chunk, OP_POP);
    parsePrecedence(compiler, parser, scanner, PREC_AND);

    patchJump(parser, &compiler->function->chunk, endJump);
}

// Parse a binary operator
static void binary(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    TokenType operatorType = parser->previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence(compiler, parser, scanner, (Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:      emitByte(parser, &compiler->function->chunk, OP_NOT_EQUAL); break;
        case TOKEN_EQUAL_EQUAL:     emitByte(parser, &compiler->function->chunk, OP_EQUAL); break;
        case TOKEN_GREATER:         emitByte(parser, &compiler->function->chunk, OP_GREATER); break;
        case TOKEN_GREATER_EQUAL:   emitByte(parser, &compiler->function->chunk, OP_GREATER_EQUAL); break;
        case TOKEN_LESS:            emitByte(parser, &compiler->function->chunk, OP_LESS); break;
        case TOKEN_LESS_EQUAL:      emitByte(parser, &compiler->function->chunk, OP_LESS_EQUAL); break;
        case TOKEN_PLUS:            emitByte(parser, &compiler->function->chunk, OP_ADD); break;
        case TOKEN_MINUS:           emitByte(parser, &compiler->function->chunk, OP_SUBTRACT); break;
        case TOKEN_STAR:            emitByte(parser, &compiler->function->chunk, OP_MULTIPLY); break;
        case TOKEN_SLASH:           emitByte(parser, &compiler->function->chunk, OP_DIVIDE); break;
        case TOKEN_PERCENT:         emitByte(parser, &compiler->function->chunk, OP_MOD); break;
        default: return; // Unreachable
    }
}

// Parses a unary expression
static void unary(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    TokenType operatorType = parser->previous.type;

    // Compile the operand
    parsePrecedence(compiler, parser, scanner, PREC_UNARY);

    switch(operatorType) {
        case TOKEN_MINUS: emitByte(parser, &compiler->function->chunk, OP_NEGATE); break;
        case TOKEN_BANG: emitByte(parser, &compiler->function->chunk, OP_NOT); break;
        case TOKEN_STAR: stackVariable(compiler, parser, scanner, canAssign); break;
        default: return; // Unreachable
    }
}

// Parse the args of a call
static uint8_t argumentList(Compiler* compiler, Parser* parser, Scanner* scanner) {
    uint8_t argCount = 0;
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            expression(compiler, parser, scanner);
            if (argCount == 255) {
                error(parser, "Cannot have more than 255 arguments");
            }
            argCount++;
        } while (match(parser, scanner, TOKEN_COMMA));
    }
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expect ')' after arguments");
    return argCount;
}

// Parses a call expression
static void call(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    uint8_t argCount = argumentList(compiler, parser, scanner);
    emitBytes(parser, &compiler->function->chunk, OP_CALL, argCount);
}

// Parses a indexing expression
static void subindex(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    // Check if first expression was used
    if (check(parser, TOKEN_COLON)) {
        // Emit nil if left empty
        emitByte(parser, &compiler->function->chunk, OP_NIL);
    } else {
        // Parse Expression
        expression(compiler, parser, scanner);
    }

    // Check if there is a second arg
    if (match(parser, scanner, TOKEN_COLON)) {
        // Check if second expression was used
        if (check(parser, TOKEN_COLON) || check(parser, TOKEN_RIGHT_SQUARE)) {
            // Emit nil if left empty
            emitByte(parser, &compiler->function->chunk, OP_NIL);
        } else {
            // Parse Expression
            expression(compiler, parser, scanner);
        }

        // Check if there is a third argument
        if (match(parser, scanner, TOKEN_COLON)) {
            // Check if third expression was used
            if (check(parser, TOKEN_RIGHT_SQUARE)) {
                // Emit index range op without custom interval
                emitByte(parser, &compiler->function->chunk, OP_INDEX_RANGE);
            } else {
                // Parse Expression
                expression(compiler, parser, scanner);
                // Emit index range op with custom interval
                emitByte(parser, &compiler->function->chunk, OP_INDEX_RANGE_INTERVAL);
            }
        } else {
            // Only two arguments
            // Emit index range op
            emitByte(parser, &compiler->function->chunk, OP_INDEX_RANGE);
        }
    } else { // Only one arguments
        // Emit get index op
        emitByte(parser, &compiler->function->chunk, OP_INDEX);
    }

    // Consume closing ']'
    consume(parser, scanner, TOKEN_RIGHT_SQUARE, "Expect ']' after arguments");
}

// Parse a grouping expression (expression)
static void grouping(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    expression(compiler, parser, scanner);
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expect ')' after expression");
}

// Parses a number literal
static void number(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    double value = strtod(parser->previous.start, NULL);
    emitConstant(parser, &compiler->function->chunk, NUMBER_VAL(value));
}

// Parses a string
static void string(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    emitConstant(parser, &compiler->function->chunk, OBJ_VAL(copyString(parser->previous.start + 1, parser->previous.length - 2)));
    
    // Check for string interpolation
    if (match(parser, scanner, TOKEN_DOLLAR_BRACE)) {
        parsePrecedence(compiler, parser, scanner, PREC_CONDITIONAL);
        consume(parser, scanner, TOKEN_RIGHT_BRACE, "Expect '}' after '${' string interpolation");
        emitByte(parser, &compiler->function->chunk, OP_INTERPOLATE_STR);
        if (match(parser, scanner, TOKEN_STRING)) {
            string(compiler, parser, scanner, canAssign);
            emitByte(parser, &compiler->function->chunk, OP_INTERPOLATE_STR);
        }
    }
}

// Parses a literal value
static void literal(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    switch(parser->previous.type) {
        case TOKEN_FALSE: emitByte(parser, &compiler->function->chunk, OP_FALSE); break;
        case TOKEN_NIL: emitByte(parser, &compiler->function->chunk, OP_NIL); break;
        case TOKEN_TRUE: emitByte(parser, &compiler->function->chunk, OP_TRUE); break;
        default: return; // Unreachable
    }
}

// Make a new constant that stores the identifiers name
static uint32_t identifierConstant(Parser* parser, Chunk* chunk, Token* name) {
    return makeConstant(parser, chunk, OBJ_VAL(copyString(name->start, name->length)));
}

// Check if two variable identifiers are the same
static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// Try to locate a local
static uint32_t resolveLocal(Compiler* compiler, Parser* parser, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error(parser, "Cannot read local variable in its own initializer");
            }
            return i;
        }
    }

    return -1;
}

// Add a local variable
static void addLocal(Compiler* compiler, Parser* parser, Token name) {
    if (compiler->localCount == STACK_MAX) {
        error(parser, "Too many local variables in function");
        return;
    }

    Local* local = &compiler->locals[compiler->localCount++];
    local->name = name;
    local->depth = -1;
}

// Make a local variable
static void declareVariable(Compiler* compiler, Parser* parser) {
    if (compiler->scopeDepth == 0) return;

    Token* name = &parser->previous;

    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (local->depth != -1 && local->depth < compiler->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error(parser, "Already a variable with this name in this scope");
        }
    }

    addLocal(compiler, parser, *name);
}

// Assignment helper
static void assignVarWithOpLong(Compiler* compiler, Parser* parser, Scanner* scanner, OpCode getOp, OpCode setOp, OpCode op, uint32_t arg) {
    emitLong(parser, &compiler->function->chunk, getOp + 1, arg);
    expression(compiler, parser, scanner);
    emitByte(parser, &compiler->function->chunk, op);
    emitLong(parser, &compiler->function->chunk, setOp + 1, arg);
}
// Assignment helper
static void assignVarWithOp(Compiler* compiler, Parser* parser, Scanner* scanner, OpCode getOp, OpCode setOp, OpCode op, uint32_t arg) {
    emitBytes(parser, &compiler->function->chunk, getOp, (uint8_t)arg);
    expression(compiler, parser, scanner);
    emitByte(parser, &compiler->function->chunk, op);
    emitBytes(parser, &compiler->function->chunk, setOp, (uint8_t)arg);
}

// Access or assign a variable
static void namedVariable(Compiler* compiler, Parser* parser, Scanner* scanner, Token name, bool canAssign) {
    // Select correct get and set operators for global vs local
    uint8_t getOp, setOp;
    uint32_t arg = resolveLocal(compiler, parser, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else {
        arg = identifierConstant(parser, &compiler->function->chunk, &name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    // Check argument size and use appropriate byteop
    if (arg < UINT8_MAX) { // ********************* COULD LOOK INTO USING MATCHRANGE FOR POTENTIAL OPTIMIZATION *************************
        if (canAssign) { // Byte Case
            if (match(parser, scanner, TOKEN_EQUAL)) {
                expression(compiler, parser, scanner);
                emitBytes(parser, &compiler->function->chunk, setOp, (uint8_t)arg);
            } else if (match(parser, scanner, TOKEN_PLUS_EQUAL)) assignVarWithOp(compiler, parser, scanner, getOp, setOp, OP_ADD, arg);
            else if (match(parser, scanner, TOKEN_MINUS_EQUAL)) assignVarWithOp(compiler, parser, scanner, getOp, setOp, OP_SUBTRACT, arg);
            else if (match(parser, scanner, TOKEN_STAR_EQUAL)) assignVarWithOp(compiler, parser, scanner, getOp, setOp, OP_MULTIPLY, arg);
            else if (match(parser, scanner, TOKEN_SLASH_EQUAL)) assignVarWithOp(compiler, parser, scanner, getOp, setOp, OP_DIVIDE, arg);
            else if (match(parser, scanner, TOKEN_PERCENT_EQUAL)) assignVarWithOp(compiler, parser, scanner, getOp, setOp, OP_MOD, arg);
            else emitBytes(parser, &compiler->function->chunk, getOp, (uint8_t)arg);
        } else {
            emitBytes(parser, &compiler->function->chunk, getOp, (uint8_t)arg);
        }
    } else { // Long Case
        if (canAssign) {
            if (match(parser, scanner, TOKEN_EQUAL)) { 
                expression(compiler, parser, scanner);
                emitLong(parser, &compiler->function->chunk, setOp + 1, arg);
            } else if (match(parser, scanner, TOKEN_PLUS_EQUAL)) assignVarWithOpLong(compiler, parser, scanner, getOp, setOp, OP_ADD, arg);
            else if (match(parser, scanner, TOKEN_MINUS_EQUAL)) assignVarWithOpLong(compiler, parser, scanner, getOp, setOp, OP_SUBTRACT, arg);
            else if (match(parser, scanner, TOKEN_STAR_EQUAL)) assignVarWithOpLong(compiler, parser, scanner, getOp, setOp, OP_MULTIPLY, arg);
            else if (match(parser, scanner, TOKEN_SLASH_EQUAL)) assignVarWithOpLong(compiler, parser, scanner, getOp, setOp, OP_DIVIDE, arg);
            else if (match(parser, scanner, TOKEN_PERCENT_EQUAL)) assignVarWithOpLong(compiler, parser, scanner, getOp, setOp, OP_MOD, arg);
            else emitLong(parser, &compiler->function->chunk, getOp + 1, arg);
        } else {
            emitLong(parser, &compiler->function->chunk, getOp + 1, arg);
        }
    }
}

// Parses a variable call
static void variable(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    namedVariable(compiler, parser, scanner, parser->previous, canAssign);
}

// Assignment helper
static void assignVarWithOpStack(Compiler* compiler, Parser* parser, Scanner* scanner, OpCode op) {
    emitByte(parser, &compiler->function->chunk, OP_GET_GLOBAL_STACK_POPLESS);
    expression(compiler, parser, scanner);
    emitByte(parser, &compiler->function->chunk, op);
    emitByte(parser, &compiler->function->chunk, OP_SET_GLOBAL_STACK);
}
// Access or assign a variable with a value on the stack
static void stackVariable(Compiler* compiler, Parser* parser, Scanner* scanner, bool canAssign) {
    if (canAssign) {
        if (match(parser, scanner, TOKEN_EQUAL)) {
            expression(compiler, parser, scanner);
            emitByte(parser, &compiler->function->chunk, OP_SET_GLOBAL_STACK);
        } else if (match(parser, scanner, TOKEN_PLUS_EQUAL)) assignVarWithOpStack(compiler, parser, scanner, OP_ADD);
        else if (match(parser, scanner, TOKEN_MINUS_EQUAL)) assignVarWithOpStack(compiler, parser, scanner, OP_SUBTRACT);
        else if (match(parser, scanner, TOKEN_STAR_EQUAL)) assignVarWithOpStack(compiler, parser, scanner, OP_MULTIPLY);
        else if (match(parser, scanner, TOKEN_SLASH_EQUAL)) assignVarWithOpStack(compiler, parser, scanner, OP_DIVIDE);
        else if (match(parser, scanner, TOKEN_PERCENT_EQUAL)) assignVarWithOpStack(compiler, parser, scanner, OP_MOD);
        else emitByte(parser, &compiler->function->chunk, OP_GET_GLOBAL_STACK);
    } else {
        emitByte(parser, &compiler->function->chunk, OP_GET_GLOBAL_STACK);
    }
}

/* 
    ---------------
    GRAMMAR EXECUTION 
    ---------------
*/

// Grammar rules of cave
ParseRule rules[] = {
	[TOKEN_LEFT_PAREN]      = {grouping,    call,        PREC_CALL},
	[TOKEN_RIGHT_PAREN]     = {NULL,        NULL,        PREC_NONE},
	[TOKEN_LEFT_BRACE]      = {NULL,        NULL,        PREC_NONE},
	[TOKEN_RIGHT_BRACE]     = {NULL,        NULL,        PREC_NONE},
    [TOKEN_LEFT_SQUARE]     = {NULL,        subindex,    PREC_CALL},
    [TOKEN_RIGHT_SQUARE]    = {NULL,        NULL,        PREC_NONE},
	[TOKEN_COMMA]           = {NULL,        NULL,        PREC_NONE},
    [TOKEN_DEL]             = {NULL,        NULL,        PREC_NONE},
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
	[TOKEN_STAR]            = {unary,       binary,      PREC_FACTOR},
	[TOKEN_STAR_EQUAL]      = {NULL,        NULL,        PREC_NONE},
	[TOKEN_PERCENT]         = {NULL,        binary,      PREC_FACTOR},
	[TOKEN_PERCENT_EQUAL]   = {NULL,        NULL,        PREC_NONE},
	[TOKEN_IDENTIFIER]      = {variable,    NULL,        PREC_NONE},
	[TOKEN_STRING]          = {string,      NULL,        PREC_NONE},
	[TOKEN_NUMBER]          = {number,      NULL,        PREC_NONE},
	[TOKEN_AND]             = {NULL,        and_,        PREC_AND},
	[TOKEN_BREAK]           = {NULL,        NULL,        PREC_NONE},
    [TOKEN_CASE]            = {NULL,        NULL,        PREC_NONE},
    [TOKEN_DEFAULT]         = {NULL,        NULL,        PREC_NONE},
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
static void parsePrecedence(Compiler* compiler, Parser* parser, Scanner* scanner, Precedence precedence) {
    advance(parser, scanner);
    // Prefix operators
    ParseFn prefixRule = getRule(parser->previous.type)->prefix;
    if (prefixRule == NULL) {
        error(parser, "Expected an expression");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(compiler, parser, scanner, canAssign);

    // All other operators
    while (precedence <= getRule(parser->current.type)->precedence) {
        advance(parser, scanner);
        ParseFn anyfixRule = getRule(parser->previous.type)->anyfix;
        anyfixRule(compiler, parser, scanner, canAssign);
    }

    if (canAssign && (matchRange(parser, scanner, TOKEN_EQUAL, TOKEN_PERCENT_EQUAL))) {
        error(parser, "Invalid assignment target");
    }
}

// Parse a variable identifier
static uint32_t parseVariable(Compiler* compiler, Parser* parser, Scanner* scanner, const char* errorMessage) {
    consume(parser, scanner, TOKEN_IDENTIFIER, errorMessage);

    declareVariable(compiler, parser);
    if (compiler->scopeDepth > 0) return 0;

    return identifierConstant(parser, &compiler->function->chunk, &parser->previous);
}

// Initialize a local
static void markInitialized(Compiler* compiler) {
    if (compiler->scopeDepth == 0) return;
    compiler->locals[compiler->localCount - 1].depth = compiler->scopeDepth;
}

// Adds a variable
static void defineVariable(Compiler* compiler, Parser* parser, uint32_t global) {
    // Check if local
    if (compiler->scopeDepth > 0) {
        markInitialized(compiler);
        return;
    }

    if (global > UINT8_MAX) 
        emitLong(parser, &compiler->function->chunk, OP_DEFINE_GLOBAL_LONG, global);
    else
        emitBytes(parser, &compiler->function->chunk, OP_DEFINE_GLOBAL, global);
}

// Get rule of token type
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

// Parses an expression
static void expression(Compiler* compiler, Parser* parser, Scanner* scanner) {
    parsePrecedence(compiler, parser, scanner, PREC_ASSIGNMENT);
}

/* --- STATEMENTS --- */
// Block statement
static void block(Compiler* compiler, Parser* parser, Scanner* scanner, int loopDepth) {
    // Enter local scope
    beginScope(compiler);

    // Block body
    while (!check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
        declaration(compiler, parser, scanner, loopDepth);
    }
    consume(parser, scanner, TOKEN_RIGHT_BRACE, "Expect '}' after block");

    // End scope
    endScope(compiler, parser);
}

// Create a function
static void function(Compiler* compiler, Parser* parser, Scanner* scanner, FunctionType type) {
    Compiler funCompiler;
    initCompiler(&funCompiler, parser, type);
    beginScope(&funCompiler);
    
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expect '(' after function name");

    // Get params
    if (!check(parser, TOKEN_RIGHT_PAREN)) {
        do {
            funCompiler.function->arity++;
            if (funCompiler.function->arity > 255) {
                errorAtCurrent(parser, "Can't have more than 255 parameters");
            }
            uint8_t constant = parseVariable(&funCompiler, parser, scanner, "Expect parameter name");
            defineVariable(&funCompiler, parser, constant);
        } while (match(parser, scanner, TOKEN_COMMA));
    }

    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expect ')' after parameters");
    consume(parser, scanner, TOKEN_LEFT_BRACE, "Expect '{' before function body");
    block(&funCompiler, parser, scanner, -1);

    ObjFunction* function = endCompiler(&funCompiler, parser);
    emitBytes(parser, &compiler->function->chunk, OP_CONSTANT, makeConstant(parser, &compiler->function->chunk, OBJ_VAL(function)));
}

// Init a function
static void funDeclaration(Compiler* compiler, Parser* parser, Scanner* scanner) {
    uint8_t global = parseVariable(compiler, parser, scanner, "Expected function name");
    markInitialized(compiler);
    function(compiler, parser, scanner, TYPE_FUNCTION);
    defineVariable(compiler, parser, global);
}

// Init a variable
static void varDeclaration(Compiler* compiler, Parser* parser, Scanner* scanner) {
    // Check if using stack "pointer"
    if (match(parser, scanner, TOKEN_STAR)) {
        // Parse expression
        parsePrecedence(compiler, parser, scanner, PREC_CONDITIONAL);
        
        // Parse equal or push nil
        if (match(parser, scanner, TOKEN_EQUAL)) {
        expression(compiler, parser, scanner);
        } else {
            emitByte(parser, &compiler->function->chunk, OP_NIL);
        }

        // Consume ';'
        consume(parser, scanner, TOKEN_SEMICOLON, "Expected ';' after variable declaration");

        // Write define op code
        emitByte(parser, &compiler->function->chunk, OP_DEFINE_GLOBAL_STACK);
    } else {
        uint32_t global = parseVariable(compiler, parser, scanner, "Expected Variable name");

        if (match(parser, scanner, TOKEN_EQUAL)) {
            expression(compiler, parser, scanner);
        } else {
            emitByte(parser, &compiler->function->chunk, OP_NIL);
        }
        // Consume ';'
        consume(parser, scanner, TOKEN_SEMICOLON, "Expected ';' after variable declaration");

        defineVariable(compiler, parser, global);
    }
}

static void ifStatement(Compiler* compiler, Parser* parser, Scanner* scanner, int loopDepth) {
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expect '(' after 'if'");
    expression(compiler, parser, scanner);
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expect '(' after condition");

    int thenJump = emitJump(parser, &compiler->function->chunk, OP_JUMP_IF_FALSE);
    emitByte(parser, &compiler->function->chunk, OP_POP);
    statement(compiler, parser, scanner, loopDepth);

    int elseJump = emitJump(parser, &compiler->function->chunk, OP_JUMP);

    patchJump(parser, &compiler->function->chunk, thenJump);
    emitByte(parser, &compiler->function->chunk, OP_POP);

    // Look for else or elif
    if (match(parser, scanner, TOKEN_ELSE)) statement(compiler, parser, scanner, loopDepth);
    else if (match(parser, scanner, TOKEN_ELIF)) ifStatement(compiler, parser, scanner, loopDepth);
    
    patchJump(parser, &compiler->function->chunk, elseJump);
}

static void expressionStatement(Compiler* compiler, Parser* parser, Scanner* scanner) {
    expression(compiler, parser, scanner);
    consume(parser, scanner, TOKEN_SEMICOLON, "Expect ';' after expression");
    emitByte(parser, &compiler->function->chunk, OP_POP);
}

// Print statement, not a function
static void printStatement(Compiler* compiler, Parser* parser, Scanner* scanner) {
    expression(compiler, parser, scanner);
    consume(parser, scanner, TOKEN_SEMICOLON, "Expected ';' after value");
    emitByte(parser, &compiler->function->chunk, OP_PRINT);
}

// Return statement
static void returnStatement(Compiler* compiler, Parser* parser, Scanner* scanner) {
    // Check if in a function or not
    if (compiler->type == TYPE_SCRIPT) {
        error(parser, "Cannot return from tope-level code");
    }

    // Check if empty return
    if (match(parser, scanner, TOKEN_SEMICOLON)) {
        emitReturn(parser, &compiler->function->chunk);
    } else {
        // Return expression
        expression(compiler, parser, scanner);
        consume(parser, scanner, TOKEN_SEMICOLON, "Expect ';' after return value");
        emitByte(parser, &compiler->function->chunk, OP_RETURN);
    }
}

// Flow Control Statements //
// Emit a temporary break statement to be patched at next loop/switch end
static void breakStatement(Compiler* compiler, Parser* parser, Scanner* scanner, int loopDepth) {
    // Check if in loop
    if (loopDepth != -1) {
        emitBreak(compiler, parser, loopDepth);
        consume(parser, scanner, TOKEN_SEMICOLON, "Expect ';' after break statement");
    } else {
        // Throw error
        error(parser, "must be in a loop or switch");
    }
}
// Emit a temporary switch statement to be patched at next loop/switch end
static void continueStatement(Compiler* compiler, Parser* parser, Scanner* scanner, int loopDepth) {
    // Check if in loop
    if (loopDepth != -1) {
        emitContinue(compiler, parser, loopDepth);
        consume(parser, scanner, TOKEN_SEMICOLON, "Expect ';' after break statement");
    } else {
        // Throw error
        error(parser, "must be in a loop");
    }
}

// While loop
static void whileStatement(Compiler* compiler, Parser* parser, Scanner* scanner) {
    int loopStart = compiler->function->chunk.count;
    // Get condition
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expect '(' after 'while'");
    expression(compiler, parser, scanner);
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expect ')' after condition");

    // Set up exit loop jump
    int exitJump = emitJump(parser, &compiler->function->chunk, OP_JUMP_IF_FALSE);
    // Pop conditon
    emitByte(parser, &compiler->function->chunk, OP_POP);

    // Loop body with current scope depth + 1
    // Local scope to isolate body
    beginScope(compiler);
    statement(compiler, parser, scanner, compiler->scopeDepth);
    endScope(compiler, parser);

    // Patch continue statements
    patchContinues(compiler, parser);
    // Emit "loop"/jump to the condition
    emitLoop(parser, &compiler->function->chunk, OP_LOOP, loopStart);
    
    // Outside of loop
    patchJump(parser, &compiler->function->chunk, exitJump);
    emitByte(parser, &compiler->function->chunk, OP_POP);
    // Patch break statements
    patchBreaks(compiler, parser);
}

// Do while loop
static void doStatement(Compiler* compiler, Parser* parser, Scanner* scanner) {
    int loopStart = compiler->function->chunk.count;

    // Compile body, give scopeDepth for break/continue 
    // Local scope to isolate body
    beginScope(compiler);
    statement(compiler, parser, scanner, compiler->scopeDepth);
    endScope(compiler, parser);
    // Patch continue statements
    patchContinues(compiler, parser);

    // Check for 'while'
    consume(parser, scanner, TOKEN_WHILE, "Expect 'while' after do loop body");
    // Compile conditon
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expect '(' after 'while'");
    expression(compiler, parser, scanner);
    consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expect ')' after condition");
    consume(parser, scanner, TOKEN_SEMICOLON, "Expect ';' after condition");

    // Emit "loop"/jump to the start of the body, checks condition
    emitLoop(parser, &compiler->function->chunk, OP_LOOP_IF_TRUE, loopStart);

    // Outside of loop
    patchBreaks(compiler, parser);
}

// For loop
static void forStatement(Compiler* compiler, Parser* parser, Scanner* scanner) {
    // Set up scope for the loop
    beginScope(compiler);

    // Setup initializer if there is one
    consume(parser, scanner, TOKEN_LEFT_PAREN, "Expect '(' after 'for'");
    if (match(parser, scanner, TOKEN_SEMICOLON)) {
        // No initializer
    } else if (match(parser, scanner, TOKEN_VAR)) {
        varDeclaration(compiler, parser, scanner);
    } else {
        expressionStatement(compiler, parser, scanner);
    }

    // Get loop start
    int loopStart = compiler->function->chunk.count;

    // Get condition if there is one
    int exitJump = -1;
    if (!match(parser, scanner, TOKEN_SEMICOLON)) {
        // Get condition
        expression(compiler, parser, scanner);
        consume(parser, scanner, TOKEN_SEMICOLON, "Expect ';' after loop  condition");

        // Jump out of the loop if the condition is false
        exitJump = emitJump(parser, &compiler->function->chunk, OP_JUMP_IF_FALSE);
        emitByte(parser, &compiler->function->chunk, OP_POP);
    }

    // Check for increment clause
    if (!match(parser, scanner, TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(parser, &compiler->function->chunk, OP_JUMP);
        int incrementStart = compiler->function->chunk.count;
        expression(compiler, parser, scanner);
        emitByte(parser, &compiler->function->chunk, OP_POP);
        consume(parser, scanner, TOKEN_RIGHT_PAREN, "Expect ')' after for clause");

        emitLoop(parser, &compiler->function->chunk, OP_LOOP, loopStart);
        loopStart = incrementStart;
        patchJump(parser, &compiler->function->chunk, bodyJump);
    }

    // Loop body, pass it scope depth for break/continue statements
    // Use scope to seperate break statements (in the case the use types for(...) break; for some reason)
    beginScope(compiler);
    statement(compiler, parser, scanner, compiler->scopeDepth);
    endScope(compiler, parser);

    // Continues point to loop back
    patchContinues(compiler, parser);
    // Emit loop backwards
    emitLoop(parser, &compiler->function->chunk, OP_LOOP, loopStart);

    // Create exit jump if there is a condition
    if (exitJump != -1) {
        patchJump(parser, &compiler->function->chunk, exitJump);
        emitByte(parser, &compiler->function->chunk, OP_POP);
    }
    // Outside of loop, breaks point here
    patchBreaks(compiler, parser);
    // End the scope for the loop
    endScope(compiler, parser);
}

// Synch the parser to the end or start of an expression so it better report errors
static void synchronize(Parser* parser, Scanner* scanner) {
    parser->panicMode = false;

    // Look for end of a statement
    while (parser->current.type != TOKEN_EOF) {
        if (parser->previous.type == TOKEN_SEMICOLON) return;
        switch (parser->current.type) {
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
        advance(parser, scanner);
    }
}

// Parses a statement
static void statement(Compiler* compiler, Parser* parser, Scanner* scanner, int loopDepth) {
    if (match(parser, scanner, TOKEN_PRINT)) {
        printStatement(compiler, parser, scanner);
    } else if (match(parser, scanner, TOKEN_LEFT_BRACE)) {
            block(compiler, parser, scanner, loopDepth);
    } else if (match(parser, scanner, TOKEN_IF)) {
        ifStatement(compiler, parser, scanner, loopDepth);
    } else if (match(parser, scanner, TOKEN_RETURN)) {
        returnStatement(compiler, parser, scanner);
    } else if (match(parser, scanner, TOKEN_WHILE)) {
        whileStatement(compiler, parser, scanner);
    } else if (match(parser, scanner, TOKEN_FOR)) {
        forStatement(compiler, parser, scanner);
    } else if (match(parser, scanner, TOKEN_DO)) {
        doStatement(compiler, parser, scanner);
    } else if (match(parser, scanner, TOKEN_BREAK)) {
        breakStatement(compiler, parser, scanner, loopDepth);
    } else if (match(parser, scanner, TOKEN_CONTINUE)) {
        continueStatement(compiler, parser, scanner, loopDepth);
    } else {
        expressionStatement(compiler, parser, scanner);
    }
}

// Parses a declaration
static void declaration(Compiler* compiler, Parser* parser, Scanner* scanner, int loopDepth) {
    if (match(parser, scanner, TOKEN_FUN)) {
        funDeclaration(compiler, parser, scanner);
    } else if (match(parser, scanner, TOKEN_VAR)) {
        varDeclaration(compiler, parser, scanner);
    } else {
        statement(compiler, parser, scanner, loopDepth);
    }

    if (parser->panicMode) synchronize(parser, scanner);
}

/* 
    ---------------
    COMPILE MAIN 
    ---------------
*/

ObjFunction* compile(const char* source) {
    Scanner scanner;
    initScanner(&scanner, source);
    Parser parser;
    initParser(&parser);
    Compiler scriptCompiler;
    initCompiler(&scriptCompiler, &parser, TYPE_SCRIPT);
    
    advance(&parser, &scanner);

    // Compile until end of file
    // -1 to signify not currently in a loop
    while (!match(&parser, &scanner, TOKEN_EOF)) {
        declaration(&scriptCompiler, &parser, &scanner, -1);
    }

    consume(&parser, &scanner, TOKEN_EOF, "Expect end of file");
    ObjFunction* function = endCompiler(&scriptCompiler, &parser);
    return parser.hadError ? NULL : function;
}

/*

    EXPERIMENTAL

*/
// Initialize a compiler with some locals already
void initRuntimeCompiler(Compiler* compiler, Parser* parser) {
    // Clear fields
    compiler->function = NULL;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->breakCount = 0;
    compiler->continueCount = 0;
    // Initialize function
    compiler->type = TYPE_SCRIPT;
    compiler->function = newFunction();
    compiler->function->arity = 2;
 
    // Put self as local
    Local* local = &compiler->locals[compiler->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
    // Put frame as local
    local = &compiler->locals[compiler->localCount++];
    local->depth = 0;
    local->name.start = "frame";
    local->name.length = 5;
    // Put index as local
    local = &compiler->locals[compiler->localCount++];
    local->depth = 0;
    local->name.start = "index";
    local->name.length = 5;
}

ObjFunction* runtimeCompile(const char* source) {
    Scanner scanner;
    initScanner(&scanner, source);
    Parser parser;
    initParser(&parser);
    Compiler runtimeCompiler;
    initRuntimeCompiler(&runtimeCompiler, &parser);

    advance(&parser, &scanner);

    expression(&runtimeCompiler, &parser, &scanner);
    emitByte(&parser, &runtimeCompiler.function->chunk, OP_EXTRACT);

    consume(&parser, &scanner, TOKEN_EOF, "Expect end of file");
    ObjFunction* function = endCompiler(&runtimeCompiler, &parser);
    return parser.hadError ? NULL : function;
}

#undef BREAK_MAX
#undef CONTINUE_MAX