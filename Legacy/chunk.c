#include <stdlib.h>

#include "chunk.h"
#include "memory.h"

/* Initializes a chunk */
void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    initLinesArray(&chunk->lines);
    initValueArray(&chunk->constants);
}

/* Frees up a chunk */
void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    freeLinesArray(&chunk->lines);
    freeValueArray(&chunk->constants);
    // Zero out the fields, leaving chunk in empty state
    initChunk(chunk);
}

/* Writes a byte to a chunk */
void writeChunk(Chunk* chunk, uint8_t byte, int line) {
    // Check if chunk needs to be resized
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    writeLinesArray(&chunk->lines, line);
    chunk->count++;
}

/* Writes constant to chunk */
uint32_t addConstant(Chunk* chunk, Value value) {
    return writeValueArray(&chunk->constants, value);
}