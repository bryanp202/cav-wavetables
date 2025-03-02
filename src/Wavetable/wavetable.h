#ifndef wavetable_wavetable_h
#define wavetable_wavetable_h

#include <stdbool.h>
#include <complex.h>

#define WAVETABLE_MAX_FRAMES 256
#define WAVETABLE_FRAME_LEN 2048

typedef struct {
    // Table Characteristics
    const char* title;
    int num_frames;
    int sample_rate;
    int sample_size; // In bits //
    int num_channels;
    long total_samples;
    int* randf; // Array of length WAVETABLE_MAX_FRAMES filled with random integer values
    int* randi; // Array of length WAVETABLE_FRAME_LEN filled with random integer values
    // Main buffer
    double* main_time;
    double _Complex* main_freq;
    bool main_time_mode;
    // Aux1 buffer
    double* aux1_time;
    double _Complex* aux1_freq;
    bool aux1_time_mode;
} Wavetable;

typedef enum {
    BUFFER_MAIN,
    BUFFER_AUX1,
    BUFFER_MAX,
} BufferType;

void initWavetable(Wavetable* table, const char* title, int frames, int sampleRate, int sampleSize, int channels, int* randf, int* randi);
void freeWavetable(Wavetable* table);
bool importWav(Wavetable* table, BufferType buffer, const char* path);
bool exportWav(Wavetable* table, BufferType buffer, const char* path, int sample_size, int num_frames);
void normalizeByFrame(Wavetable* table, BufferType buffer, int minFrame, int maxFrame);

// Outside manip
double* getTimeBuffer(Wavetable* table, BufferType buffer);
double _Complex* getFreqBuffer(Wavetable* table, BufferType buffer);
void setTimeMode(Wavetable* table, BufferType buffer, bool time_mode);


#endif