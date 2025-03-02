#ifndef cave_lines_h
#define cave_lines_h

typedef struct {
    int count;
    int capacity;
    int* lines;
} LinesArray;

void initLinesArray(LinesArray* lines);
void freeLinesArray(LinesArray* lines);
void writeLinesArray(LinesArray* lines, int line);

int getLine(LinesArray* lines, int index);

#endif