#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "vm.h"

// From least sig on right
// CHECK IF TERNARY 0,1,2,3 is faster than a BITARRAY *********************************************
#define FOUR_TYPE_ID() \
    ((IS_OBJ(vm.stackTop[-1]) * 3 | (IS_NUMBER(vm.stackTop[-1]) << 1 | IS_BOOL(vm.stackTop[-1]))) << 2) | \
    (IS_OBJ(vm.stackTop[-2]) * 3 | IS_NUMBER(vm.stackTop[-2]) << 1 | IS_BOOL(vm.stackTop[-2]))
// End of FOUR_TYPE_ID

VM vm;

static void resetStack() {
    vm.stackTop = vm.stack;
}

// Raise a runtime error
static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vm.ip - vm.chunk->code - 1;
    int line = getLine(&vm.chunk->lines, instruction);
    fprintf(stderr, "[line %d] in script\n", line);
    resetStack();
}

void initVM() {
    resetStack();
    vm.objects = NULL;
    initTable(&vm.globals);
    initTable(&vm.strings);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
}

// Push new value onto the stack
void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

// Pop value off the stack
Value pop() {
    vm.stackTop--;
    return *vm.stackTop;
}

// Look at value 'distance' from the top of the stack
static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

// Cave's false logic
// False if value == 0 || or value == false || or value == nil
static bool isFalse(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value)) || ((IS_NUMBER(value) && !AS_NUMBER(value))) || ((IS_STRING(value) && AS_STRING(value)->length == 0));
}
// Cave's true logic
// True if value != 0 || or value == true || value != ''
static bool isTrue(Value value) {
    return (IS_BOOL(value) && AS_BOOL(value)) || (IS_NUMBER(value) && AS_NUMBER(value)) || ((IS_STRING(value) && AS_STRING(value)->length != 0));
}

// Concatenate two strings
static void concatenate() {
    ObjString* b = AS_STRING(pop());
    ObjString* a = AS_STRING(pop());

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(OBJ_VAL(result));
}

// Multiply a string
static void multiplyStringA(int times) {
    ObjString* a = AS_STRING(pop());

    if (times == 1) {
        push(OBJ_VAL(a));
    } else {
        int length = a->length * (times > 0 ? times : 0);
        char* chars = ALLOCATE(char, length + 1);
        for (int offset = 0; offset < length; offset += a->length)
            memcpy(chars + offset, a->chars, a->length);
        chars[length] = '\0';

        ObjString* result = takeString(chars, length);
        push(OBJ_VAL(result));
    }
}

// Multiply b string
static void multiplyStringB(int times) {
    ObjString* b = AS_STRING(pop());
    vm.stackTop--;

    if (times == 1) {
        push(OBJ_VAL(b));
    } else {
        int length = b->length * times;
        char* chars = ALLOCATE(char, length + 1);
        for (int offset = 0; offset < length; offset += b->length)
            memcpy(chars + offset, b->chars, b->length);
        chars[length] = '\0';

        ObjString* result = takeString(chars, length);
        push(OBJ_VAL(result));
    }
}

// Add turns the top value on the stack into a string
static void stringify() {
    Value b = vm.stackTop[-1];

    // Convert b into 
    if (IS_NUMBER(b)) {
        #define VALUE_B_LENGTH 25
        // Turn double into string
        char* buffer = ALLOCATE(char, VALUE_B_LENGTH);
        int len = snprintf(buffer, VALUE_B_LENGTH - 1, "%g", AS_NUMBER(b));
        buffer[len] = '\0';

        // Extract string
        vm.stackTop[-1] = OBJ_VAL(takeString(buffer, len));
        #undef VALUE_B_LENGTH

    } else if (IS_BOOL(b)) {
        // Get proper bool string
        if (isFalse(b))
            vm.stackTop[-1] = OBJ_VAL(copyString("false", 5));
        else
            vm.stackTop[-1] = OBJ_VAL(copyString("true", 4));
    } else if (IS_NIL(b)) {
        // Turn int "nil"
        vm.stackTop[-1] = OBJ_VAL(copyString("nil", 3));
    }
}

// Define a global variable
static void defGlobal(ObjString* name) {
    tableSet(&vm.globals, name, peek(0));
    pop();
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_SHORT() (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_LONG() (vm.ip += 3, ((vm.ip[-3]) << 16 | (vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() (vm.chunk->constants.values[READ_LONG()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_STRING_LONG() AS_STRING(READ_CONSTANT_LONG())
#define BINARY_OP(valueType, op) \
    do { \
        if (!(IS_NUMBER(peek(0)) || IS_BOOL(peek(0))) || !(IS_NUMBER(peek(1)) || IS_BOOL(peek(1)))) { \
            runtimeError("Operands must be numbers or bools"); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        int option = IS_NUMBER(peek(0)) << 1 | IS_NUMBER(peek(1)); \
        switch(option) { \
            case 0: \
                vm.stackTop -= 2; \
                push(BOOL_VAL(isTrue(BOOL_VAL(AS_BOOL(vm.stackTop[0]) op AS_BOOL(vm.stackTop[1]))))); \
                break; \
            case 1: \
                vm.stackTop -= 2; \
                push(valueType(AS_NUMBER(vm.stackTop[0]) op AS_BOOL(vm.stackTop[1]))); \
                break; \
            case 2: \
                vm.stackTop -= 2; \
                push(valueType(AS_BOOL(vm.stackTop[0]) op AS_NUMBER(vm.stackTop[1]))); \
                break; \
            case 3: \
                vm.stackTop -= 2; \
                push(valueType(AS_NUMBER(vm.stackTop[0]) op AS_NUMBER(vm.stackTop[1]))); \
                break; \
        } \
    } while (false);

    // Debug run tracing
    for (;;) {
    #ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
            for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
                printf("[ ");
                printValue(*slot);
                printf(" ]");
            }
        printf("\n");
        disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
    #endif

        uint8_t instruction;
        // Interpret current instruction and increment
        switch (instruction = READ_BYTE()) {
            // Equalities
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(!valuesEqual(a, b)));
                break;
            }

            // Comparison
            case OP_GREATER:        BINARY_OP(BOOL_VAL, >); break;
            case OP_GREATER_EQUAL:  BINARY_OP(BOOL_VAL, >=); break;
            case OP_LESS:           BINARY_OP(BOOL_VAL, <); break;
            case OP_LESS_EQUAL:     BINARY_OP(BOOL_VAL, <=); break;

            // Binary Arithmatic Operations
            case OP_ADD: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        case 5:
                            vm.stackTop -= 2;
                            push(BOOL_VAL(isTrue(BOOL_VAL(AS_BOOL(vm.stackTop[0]) + AS_BOOL(vm.stackTop[1])))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) + AS_BOOL(vm.stackTop[1])));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) + AS_BOOL(vm.stackTop[1])));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) + AS_NUMBER(vm.stackTop[1])));
                            break;
                        case 15:
                            concatenate();
                            break;
                        /* Errors */
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot add nil");
                            return INTERPRET_RUNTIME_ERROR;
                        case 7:
                        case 11:
                        case 13:
                        case 14:
                            runtimeError("Can only concat two strings");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            // Subtraction
            case OP_SUBTRACT: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        case 5:
                            vm.stackTop -= 2;
                            push(BOOL_VAL(isTrue(BOOL_VAL(AS_BOOL(vm.stackTop[0]) - AS_BOOL(vm.stackTop[1])))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) - AS_BOOL(vm.stackTop[1])));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) - AS_BOOL(vm.stackTop[1])));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) - AS_NUMBER(vm.stackTop[1])));
                            break;
                        /* Errors */
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot subtract nil");
                            return INTERPRET_RUNTIME_ERROR;
                        case 7:
                        case 11:
                        case 13:
                        case 14:
                        case 15:
                            runtimeError("Cannot subtract strings");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            // Multiplication
            case OP_MULTIPLY: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        // Basic number and bool combos
                        case 5:
                            vm.stackTop -= 2;
                            push(BOOL_VAL(isTrue(BOOL_VAL(AS_BOOL(vm.stackTop[0]) * AS_BOOL(vm.stackTop[1])))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) * AS_BOOL(vm.stackTop[1])));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) * AS_BOOL(vm.stackTop[1])));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) * AS_NUMBER(vm.stackTop[1])));
                            break;
                        case 7:
                            multiplyStringA(AS_BOOL(pop()));
                            break;
                        case 11:
                            multiplyStringA((int)AS_NUMBER(pop()));
                            break;
                        case 13:
                            multiplyStringB(AS_BOOL(vm.stackTop[-2]));
                            break;
                        case 14:
                            multiplyStringB((int)AS_NUMBER(vm.stackTop[-2]));
                            break;
                        /* Errors */
                        case 15: // Str * str
                            runtimeError("Can only multiply string by a number or bool");
                            return INTERPRET_RUNTIME_ERROR;
                        // Nil combos
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot multiply by nil");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            // Division
            case OP_DIVIDE: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        case 5:
                            vm.stackTop -= 2;
                            // FIX DIVISION BY FALSE
                            push(BOOL_VAL(isTrue(BOOL_VAL(AS_BOOL(vm.stackTop[0]) / 1))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) / AS_BOOL(vm.stackTop[1])));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) / AS_BOOL(vm.stackTop[1])));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(AS_NUMBER(vm.stackTop[0]) / AS_NUMBER(vm.stackTop[1])));
                            break;
                        /* Errors */
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot divide by nil");
                            return INTERPRET_RUNTIME_ERROR;
                        case 7:
                        case 11:
                        case 13:
                        case 14:
                        case 15:
                            runtimeError("Cannot divide strings");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            // Mod Operation
            case OP_MOD: {
                    int option = FOUR_TYPE_ID();
                    switch(option) {
                        case 5:
                            vm.stackTop -= 2;
                            // FIX DIVISION BY FALSE
                            push(BOOL_VAL(isTrue(BOOL_VAL(AS_BOOL(vm.stackTop[0]) % 1))));
                            break;
                        case 6:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[0]), AS_BOOL(vm.stackTop[1]))));
                            break;
                        case 9:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[0]), AS_BOOL(vm.stackTop[1]))));
                            break;
                        case 10:
                            vm.stackTop -= 2;
                            push(NUMBER_VAL(fmod(AS_NUMBER(vm.stackTop[0]), AS_NUMBER(vm.stackTop[1]))));
                            break;
                        /* Errors */
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 8:
                        case 12:
                            runtimeError("Cannot mod by nil");
                            return INTERPRET_RUNTIME_ERROR;
                        case 7:
                        case 11:
                        case 13:
                        case 14:
                        case 15:
                            runtimeError("Cannot mod strings");
                            return INTERPRET_RUNTIME_ERROR;
                    }
                }
                break;
            
            // Str Interpolation
            case OP_INTERPOLATE_STR: {
                // If both strings concatenate
                if (!IS_STRING(vm.stackTop[-1])) 
                    stringify();
                concatenate();
                break;
            }

            // Not Value
            case OP_NOT:
                push(BOOL_VAL(isFalse(pop())));
                break;
            // Negate Value
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number");
                    return INTERPRET_RUNTIME_ERROR;
                }
                *(vm.stackTop-1) = NUMBER_VAL(-AS_NUMBER(*(vm.stackTop-1)));
                break;

            // Constant
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            // Constant long
            case OP_CONSTANT_LONG: {
                Value constant = READ_CONSTANT_LONG();
                push(constant);
                break;
            }

            // User literals
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(1)); break;
            case OP_FALSE: push(BOOL_VAL(0)); break;

            // Variables //
            // Globals
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_GET_GLOBAL_LONG: {
                ObjString* name = READ_STRING_LONG();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if  (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_GLOBAL_LONG: {
                ObjString* name = READ_STRING_LONG();
                if  (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_DEFINE_GLOBAL: defGlobal(READ_STRING()); break;
            case OP_DEFINE_GLOBAL_LONG: defGlobal(READ_STRING_LONG()); break;

            // Locals
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vm.stack[slot]);
                break;
            }
            case OP_GET_LOCAL_LONG: {
                uint32_t slot = READ_LONG();
                push(vm.stack[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm.stack[slot] = peek(0);
                break;
            }
            case OP_SET_LOCAL_LONG: {
                uint32_t slot = READ_LONG();
                vm.stack[slot] = peek(0);
                break;
            }

            
            // Pop onces
            case OP_POP: pop(); break;
            // Pop n times
            case OP_POPN: vm.stackTop -= READ_LONG(); break;

            //// Statements ////
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }

            //// Control Flow ////
            case OP_JUMP_IF_FALSE: {
                uint16_t loc = READ_SHORT();
                if (isFalse(peek(0))) vm.ip += loc;
                break;
            }
            case OP_JUMP_IF_TRUE: {
                uint16_t loc = READ_SHORT();
                if (isTrue(peek(0))) vm.ip += loc;
                break;
            }
            case OP_JUMP: {
                vm.ip += READ_SHORT();
                break;
            }
            case OP_JUMP_NPOP: {
                int jump = READ_SHORT();
                vm.stackTop -= READ_LONG();
                vm.ip += jump;
                break;
            }
            case OP_LOOP: {
                vm.ip -= READ_SHORT();
                break;
            }
            case OP_LOOP_IF_TRUE: {
                uint16_t loc = READ_SHORT();
                if (isTrue(peek(0))) vm.ip -= loc;
                // Pop condition check value from the stack
                pop();
                break;
            }
            
            // Returns value from func
            case OP_RETURN: {
                // Exit interpreter
                return INTERPRET_OK;
            }

            default: {
                runtimeError("Unrecognized bytecode");
                return INTERPRET_RUNTIME_ERROR;
            }
        }
    }
#undef READ_LONG
#undef READ_SHORT
#undef READ_BYTE
#undef READ_CONSTANT_LONG
#undef READ_CONSTANT
#undef READ_STRING_LONG
#undef READ_STRING
#undef BINARY_OP
}

// Interpret a chunk
InterpretResult interpret(const char* source) {
    Chunk chunk;
    initChunk(&chunk);

    if (!compile(source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;

    InterpretResult result = run();

    freeChunk(&chunk);
    return result;
}

#undef FOUR_TYPE_ID