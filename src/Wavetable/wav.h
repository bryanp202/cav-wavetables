#ifndef wavetable_wav_h
#define wavetable_wav_h

#include <stdbool.h>

bool writeWav(const char* path, int numChannels, int sampleRate, int sampleSize, long int numSamples, double* data);
bool readWav(const char* path, int numChannels, long int numSamples, double* data);

#endif