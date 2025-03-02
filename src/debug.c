#include <stdio.h>

#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

// Print a instruction with a quantity in it
static int byteInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1];
    printf("%-24s %4d\n", name, constant);
    return offset + 2;
}
// Print a instruction with a quantity in it, but longer
static int longInstruction(const char* name, Chunk* chunk, int offset) {
    uint32_t constant = chunk->code[offset + 1] << 16 | chunk->code[offset + 2] << 8 | chunk->code[offset + 3];
    printf("%-24s %4d\n", name, constant);
    return offset + 4;
}

// Print a jump instructions
// Prints instruction number it jumps to
static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
    uint16_t constant = chunk->code[offset + 1] << 8 | chunk->code[offset + 2];
    printf("%-24s %4d\n", name, offset + constant * sign + 3);
    return offset + 3;
}
// Prints instruction number it jumps to and amount of pops
static int jumpNpopInstruction(const char* name, Chunk* chunk, int offset) {
    uint16_t constant = chunk->code[offset + 1] << 8 | chunk->code[offset + 2];
    uint32_t n = chunk->code[offset + 3] << 16 | chunk->code[offset + 4] << 8 | chunk->code[offset + 5];
    printf("%-24s %4d %d\n", name, offset + constant + 6, n);
    return offset + 6;
}

// Get a constant from current chunks constant list
static int constantInstruction(const char* name, Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1];
    printf("%-24s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 2;
}
static int longConstantInstruction(const char* name, Chunk* chunk, int offset) {
    uint32_t constant = chunk->code[offset + 1] << 16 | chunk->code[offset + 2] << 8 | chunk->code[offset + 3];
    printf("%-24s %4d '", name, constant);
    printValue(chunk->constants.values[constant]);
    printf("'\n");
    return offset + 4;
}

static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);

    if (offset > 0 && getLine(&chunk->lines, offset) == getLine(&chunk->lines, offset - 1)) {
        printf("   | ");
    } else {
        printf("%4d ", getLine(&chunk->lines, offset));
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_EXTRACT:
            return simpleInstruction("OP_EXTRACT", offset);
        case OP_CALL:
            return byteInstruction("OP_CALL", chunk, offset);
        case OP_INDEX:
            return simpleInstruction("OP_INDEX", offset);
        case OP_INDEX_RANGE:
            return simpleInstruction("OP_INDEX_RANGE", offset);
        case OP_INDEX_RANGE_INTERVAL:
            return simpleInstruction("OP_INDEX_RANGE_INTERVAL", offset);
        case OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", chunk, offset);
        case OP_CONSTANT_LONG:
            return longConstantInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);
        case OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL:
            return simpleInstruction("OP_NOT_EQUAL", offset);
        case OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OP_GREATER_EQUAL:
            return simpleInstruction("OP_GREATER_EQUAL", offset);
        case OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OP_LESS_EQUAL:
            return simpleInstruction("OP_LESS_EQUAL", offset);
        case OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OP_SUBTRACT:
            return simpleInstruction("OP_SUBTRACT", offset);
        case OP_MULTIPLY:
            return simpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return simpleInstruction("OP_DIVIDE", offset);
        case OP_MOD:
            return simpleInstruction("OP_MOD", offset);
        case OP_INTERPOLATE_STR:
            return simpleInstruction("OP_INTERPOLATE_STR", offset);
        case OP_NEGATE:
            return simpleInstruction("OP_NEGATE", offset);
        case OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OP_POPN:
            return longInstruction("OP_POPN", chunk, offset);
        case OP_GET_GLOBAL:
            return constantInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_GET_GLOBAL_LONG:
            return longConstantInstruction("OP_GET_GLOBAL_LONG", chunk, offset);
        case OP_GET_GLOBAL_STACK:
            return simpleInstruction("OP_GET_GLOBAL_STACK", offset);
        case OP_GET_GLOBAL_STACK_POPLESS:
            return simpleInstruction("OP_GET_GLOBAL_STACK_POPLESS", offset);
        case OP_GET_LOCAL:
            return byteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_GET_LOCAL_LONG:
            return longInstruction("OP_GET_LOCAL_LONG", chunk, offset); // Not used
        case OP_DEFINE_GLOBAL:
            return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL_LONG:
            return longConstantInstruction("OP_DEFINE_GLOBAL_LONG", chunk, offset);
        case OP_DEFINE_GLOBAL_STACK:
            return simpleInstruction("OP_DEFINE_GLOBAL_STACK", offset);
        case OP_SET_GLOBAL:
            return constantInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL_LONG:
            return longConstantInstruction("OP_SET_GLOBAL_LONG", chunk, offset);
        case OP_SET_GLOBAL_STACK:
            return simpleInstruction("OP_SET_GLOBAL_STACK", offset);
        case OP_SET_LOCAL:
            return byteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_SET_LOCAL_LONG:
            return longInstruction("OP_SET_LOCAL_LONG", chunk, offset); // Not used
        case OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_JUMP_IF_TRUE:
            return jumpInstruction("OP_JUMP_IF_TRUE", 1, chunk, offset);
        case OP_JUMP_NPOP:
            return jumpNpopInstruction("OP_JUMP_NPOP", chunk, offset);
        case OP_LOOP:
            return jumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_LOOP_IF_TRUE:
            return jumpInstruction("OP_LOOP_IF_TRUE", -1, chunk, offset);
        case OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

