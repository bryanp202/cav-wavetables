#include "common.h"
#include "lines.h"
#include "memory.h"

void initLinesArray(LinesArray* lines) {
    lines->count = 0;
    lines->capacity = 0;
    lines->lines = NULL;
}

void freeLinesArray(LinesArray* lines) {
    FREE_ARRAY(int, lines->lines, lines->capacity);
    // Zero out the fields, leaving array in empty state
    initLinesArray(lines);
}

// Count,LineNum | Count,LineNum
void writeLinesArray(LinesArray* lines, int line) {
    // If line == the most recent line then increment and return
    
    if (lines->count > 0 && lines->lines[lines->count - 1] == line) {
        lines->lines[lines->count - 2]++;
        return;
    }

    // Check if LinesArray needs to be resized
    if (lines->capacity < lines->count + 2) {
        int oldCapacity = lines->capacity;
        lines->capacity = GROW_CAPACITY(oldCapacity);
        lines->lines = GROW_ARRAY(int, lines->lines, oldCapacity, lines->capacity);
    }

    // Else add at end
    // with new line and count
    lines->lines[lines->count++] = 1;
    lines->lines[lines->count++] = line;
}

// Returns line stored for bytecode at 'index'
int getLine(LinesArray* lines, int index) {
    for (int lineArrayPos = 0; lineArrayPos < lines->capacity; lineArrayPos += 2) {
        index -= lines->lines[lineArrayPos];
        if (index < 0) {
            return lines->lines[lineArrayPos + 1];
        }
    }

    // Index does not exist
    return -1;
}