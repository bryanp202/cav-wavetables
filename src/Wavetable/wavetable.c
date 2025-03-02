#include <stdio.h>
#include <stdlib.h>

#include "fft.h"
#include "wav.h"
#include "wavetable.h"

//--------------------------------------HELPER FUNCTIONS--------------------------------------//

/*
Checks if the provided buffer is in frequency mode
Converts it to frequency mode if needed
*/
static void check_freq_mode(bool* isTimeMode, int numFrames, double* in, double _Complex* out) {
    if (*isTimeMode) {
        for (int frame = 0; frame < numFrames; frame++)
            fft_2048_by2(in + frame * WAVETABLE_FRAME_LEN, out + frame * WAVETABLE_FRAME_LEN);
        *isTimeMode = false;
    }
    
}

/*
Checks if the provided buffer is in time mode
Converts it to time mode if needed
*/
static void check_time_mode(bool* isTimeMode, int numFrames, double _Complex* in, double* out) {
    if (!(*isTimeMode)) {
        for (int frame = 0; frame < numFrames; frame++)
            ifft_2048_by2(in + frame * WAVETABLE_FRAME_LEN, out + frame * WAVETABLE_FRAME_LEN);
        *isTimeMode = true;
    }
}


/*
Returns the local abs max value of a frame
*/
static double get_frame_max(double* frame) {
    double max = 0.0;
    for (int i = 0; i < WAVETABLE_FRAME_LEN; i++) {
        if (max < frame[i])
            max = frame[i];
        else if (max < -frame[i])
            max = -frame[i];
    }
    if (max == 0) {
        return 1;
    }
    return max;
}

/*
Returns the abs max value of a buffer
*/
static double get_buffer_max(long int totalSamples, double* buffer) {
    double max = 0.0;
    for (int i = 0; i < totalSamples; i++) {
        if (max < buffer[i])
            max = buffer[i];
        else if (max < -buffer[i])
            max = -buffer[i];
    }
    if (max == 0) {
        return 1;
    }
    return max;
}

/*
Rescales a buffer by a scalar value
*/
static void rescale_buffer(long int totalSamples, double factor, double* buffer) {
    for (int i = 0; i < totalSamples; i++) {
        buffer[i] *= factor; 
    }
}

/*
Rescales a frame by a scalar value
*/
static void rescale_frame(double factor, double* frame) {
    for (int i = 0; i < WAVETABLE_FRAME_LEN; i++) {
        frame[i] *= factor;
    }
}

/*
Normalize a targeted buffer to be in range -1 to 1
*/
static void normalize_to_one(long int totalSamples, double* buffer) {
    // Get max value
    double max = get_buffer_max(totalSamples, buffer);
    // Rescale
    rescale_buffer(totalSamples, 1/max, buffer);
}

//--------------------------------------WAVETABLE FUNCTIONS--------------------------------------//

/*
Initializes a wavetable
*/
void initWavetable(Wavetable* table, const char* title, int frames, int sampleRate, int sampleSize, int channels, int* randf, int* randi) {
    // Initiate characteristics
    table->title = title;
    table->num_frames = frames;
    table->sample_rate = sampleRate;
    table->sample_size = sampleSize;
    table->num_channels = channels;
    table->total_samples = frames * WAVETABLE_FRAME_LEN * channels;
    table->randf = randf;
    table->randi = randi;
    // Initiate buffers
    // Main
    table->main_time = (double*)calloc(frames * WAVETABLE_FRAME_LEN * channels, sizeof(double));
    table->main_freq = (double _Complex*)calloc(frames * WAVETABLE_FRAME_LEN * channels, sizeof(double _Complex));
    table->main_time_mode = true;
    // Aux1
    table->aux1_time = (double*)calloc(frames * WAVETABLE_FRAME_LEN * channels, sizeof(double));
    table->aux1_freq = (double _Complex*)calloc(frames * WAVETABLE_FRAME_LEN * channels, sizeof(double _Complex));
    table->aux1_time_mode = true;
}

/*
Frees a wavetable
*/
void freeWavetable(Wavetable* table) {
    table->title = NULL;
    table->num_frames = 0;
    table->sample_size = 0;
    table->num_channels = 0;
    table->total_samples = 0;
    table->main_time_mode = false;
    table->aux1_time_mode = false;
    free(table->randf);
    free(table->randi);
    free(table->main_time);
    free(table->main_freq);
    free(table->aux1_time);
    free(table->aux1_freq);
}

/*
Import a .wav file into a targeted buffer
Imports from file at 'path'
*/
bool importWav(Wavetable* table, BufferType buffer, const char* path) {
    switch(buffer) {
        case BUFFER_MAIN: {
            table->main_time_mode = true;
            return readWav(path, table->num_channels, table->num_frames * WAVETABLE_FRAME_LEN, table->main_time);
        }
        case BUFFER_AUX1: {
            table->aux1_time_mode = true;
            return readWav(path, table->num_channels, table->num_frames * WAVETABLE_FRAME_LEN, table->aux1_time);
        }
    }
}

/*
Exports a targeted buffer from a wavetable to a .wav file
Exports to a file at 'path'
*/
bool exportWav(Wavetable* table, BufferType buffer, const char* path, int sample_size, int num_frames) {
    switch(buffer) {
        case BUFFER_MAIN: {
            check_time_mode(&table->main_time_mode, table->num_frames, table->main_freq, table->main_time);
            normalize_to_one(table->total_samples, table->main_time);
            return writeWav(path, table->num_channels, table->sample_rate, sample_size, num_frames * WAVETABLE_FRAME_LEN, table->main_time);
        }
        case BUFFER_AUX1: {
            check_time_mode(&table->aux1_time_mode, table->num_frames, table->aux1_freq, table->aux1_time);
            normalize_to_one(table->total_samples, table->aux1_time);
            return writeWav(path, table->num_channels, table->sample_rate, sample_size, num_frames * WAVETABLE_FRAME_LEN, table->aux1_time);
        }
    }
}

/*
Normalize frames based on each frames local max
Only targets a certain buffer and frame range [minFrame,maxFrame)
*/
void normalizeByFrame(Wavetable* table, BufferType buffer, int minFrame, int maxFrame) {
    // Set Time Mode
    setTimeMode(table, buffer, true);
    // Get target buffer
    double* frame_buffer = getTimeBuffer(table, buffer);

    // Loop through each frame in range
    for (int frame = minFrame; frame < maxFrame; frame++) {
        // Rescale each frame in range
        const double max = get_frame_max(frame_buffer);
        rescale_frame(1/max, frame_buffer);
        frame_buffer += WAVETABLE_FRAME_LEN;
    }
}

/* Outside mode toggling */
void setTimeMode(Wavetable* table, BufferType buffer, bool time_mode) {
    // Get buffers
    double* time_buffer;
    _Complex double* freq_buffer;
    bool* time_mode_pointer;

    switch (buffer) {
        case BUFFER_MAIN: {
            time_buffer = table->main_time;
            freq_buffer = table->main_freq;
            time_mode_pointer = &table->main_time_mode;
            break;
        }
        case BUFFER_AUX1: {
            time_buffer = table->aux1_time;
            freq_buffer = table->aux1_freq;
            time_mode_pointer = &table->aux1_time_mode;
            break;
        }
    }

    // Check if setting to time mode or freq mode
    if (time_mode) {
        // Check if not in time mode
        if (!*time_mode_pointer) {
            // Set to time mode
            for (int frame = 0; frame < table->num_frames; frame++)
                ifft_2048_by2(freq_buffer + frame * WAVETABLE_FRAME_LEN, time_buffer + frame * WAVETABLE_FRAME_LEN);
            // Normalize
            normalize_to_one(table->total_samples, time_buffer);
            *time_mode_pointer = true;
        }
    } else { // Set to freq mode
        // Check if not in freq mode
        if (*time_mode_pointer) {
            for (int frame = 0; frame < table->num_frames; frame++)
                fft_2048_by2(time_buffer + frame * WAVETABLE_FRAME_LEN, freq_buffer + frame * WAVETABLE_FRAME_LEN);
            *time_mode_pointer = false;
        }
    }
}

/* Wavetable Buffer editing */
double* getTimeBuffer(Wavetable* table, BufferType buffer) {
    switch (buffer) {
        case BUFFER_MAIN:
            return table->main_time;
        case BUFFER_AUX1:
            return table->aux1_time;
    }
}

/* Wavetable Buffer editing */
_Complex double* getFreqBuffer(Wavetable* table, BufferType buffer) {
    switch (buffer) {
        case BUFFER_MAIN:
            return table->main_freq;
        case BUFFER_AUX1:
            return table->aux1_freq;
    }
}