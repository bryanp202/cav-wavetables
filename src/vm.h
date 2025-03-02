#ifndef cave_vm_h
#define cave_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#include "Wavetable/wavetable.h"

// Recursion max depth is 64 calls
#define FRAMES_MAX 256
// Stack size is 16kb
#define STACK_MAX 16384


typedef struct {
    ObjFunction* function;
    uint8_t* ip;
    Value* slots;
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    Value stack[STACK_MAX];
    Value* stackTop;
    Table globals;
    Table strings;
    Obj* objects;

    // Wavetable stuff
    Wavetable wavetable;
    Value output;
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
// Stack funcs
void push(Value value);
Value pop();

#endif