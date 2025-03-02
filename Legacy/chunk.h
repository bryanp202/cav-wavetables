#ifndef cave_chunk_h
#define cave_chunk_h

#include "common.h"
#include "value.h"
#include "lines.h"

// Enum of operation types
/*

Intentional Design choices:
    ~ The long version of a opcode is always 1 after the normal version
        - OP_CONSTANT + 1 == OP_CONSTANT_LONG

*/
typedef enum {
    OP_ADD, // ADD two values
    OP_CONDITIONAL, // a ? b : c, if a is true, return b, else return c
    OP_CONSTANT, // 2 Bytes: opcode, constant index
    OP_CONSTANT_LONG, // 4 bytes: opcode, constant index part1, part2, part3 (big endian)
    OP_DEFINE_GLOBAL, // Defines a global variable
    OP_DEFINE_GLOBAL_LONG, // Define a 4 byte global variable
    OP_DIVIDE, // Divide two vales
    OP_EQUAL, // Compare and return true if both values are the same
    OP_NOT_EQUAL, // Compare and return true if both values are not the same
    OP_FALSE, // Add a false boolean value
    OP_GET_GLOBAL, // Get a global var value
    OP_GET_GLOBAL_LONG, // Get a 4 byte global var 
    OP_GET_LOCAL, // Get a local 2bytes
    OP_GET_LOCAL_LONG, // Get a local 4bytes *** NOT IMPLEMENTED ***
    OP_GREATER, // Compare and return true if left is greater than right
    OP_GREATER_EQUAL, // Compare and return true if left is greater than or equal to 
    OP_INTERPOLATE_STR, // Interpolate a string and a value
    OP_LESS, // Compare and return true if left is less than right
    OP_LESS_EQUAL, // // Compare and return true if left is less than or equal to right
    OP_MOD, // Mod two values
    OP_MULTIPLY, // Multiply two values
    OP_NOT, // Not a 
    OP_NEGATE, // Negates a Value
    OP_NIL, // Add a nil value
    OP_POP, // Pops a value of the stack, 1 byte
    OP_POPN, // Pop n values from the stack, 4 bytes
    OP_PRINT, // Print a value out
    OP_JUMP, // Jump a set distance no matter what, 3 bytes
    OP_JUMP_IF_FALSE, // Jump a set distance if value on top of stack is false, 3 bytes
    OP_JUMP_IF_TRUE,  // Jump a set distance if value on top of stack is true, 3 bytes
    OP_JUMP_NPOP, // Jump a set distance no matter what, and pop n values, 6 bytes
    OP_LOOP, // Jump a set distance backwards, 3 bytes
    OP_LOOP_IF_TRUE, // Jump a set distance backwards if value on top of stack is true, 3 bytes
    OP_RETURN, // 1 Byte: opcode
    OP_SET_GLOBAL, // 2 Bytes, set a global variable
    OP_SET_GLOBAL_LONG, // 4 bytes, set a global variable
    OP_SET_LOCAL, // Set a local 2bytes
    OP_SET_LOCAL_LONG, // Set a local 4bytes *** NOT IMPLEMENTED ***
    OP_SUBTRACT, // Subtract two values
    OP_TRUE, // Add a true boolean value
} OpCode;

// Dynamic array
typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    LinesArray lines; // Stores the source line number of every operation
    ValueArray constants;
} Chunk;

// Init dynamic array
void initChunk(Chunk* chunk);
// Deletes a chunk
void freeChunk(Chunk* chunk);
// Write to chunk
void writeChunk(Chunk* chunk, uint8_t byte, int line);
// Write constant to chunk
uint32_t addConstant(Chunk* chunk, Value value);

#endif