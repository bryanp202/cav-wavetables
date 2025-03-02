#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wav.h"

typedef struct {
    char chunk_id[4]; // "RIFF"
    uint32_t file_length; // Total length, in bytes
    char file_type[4]; // "WAVE"
    char format_chunk[4]; // "fmt\(null)"
    uint32_t format_length; // 16
    uint16_t audio_format; // 1
    uint16_t num_channels; // 1-2 usually
    uint32_t sample_rate; // 44100 standard
    uint32_t bytes_per_second; // sample_rate * sample_size * channels
    uint16_t bytes_per_block; // sample_size * channels
    uint16_t sample_size;  // In bytes
    char data_chunk[4]; // "data"
    uint32_t data_length; // length of data chunk in bits
} HeaderFields;

union Header {
    char raw[sizeof(HeaderFields)];
    HeaderFields fields;
};

/* Exports a .wav file using the input params */
bool writeWav(const char* path, int numChannels, int sampleRate, int sampleSize, long int numSamples, double* data) {
    // Check if sampleSize is valid
    if (sampleSize != 8 && sampleSize != 16 && sampleSize != 32) {
        fprintf(stderr, "Invalid sampleSize '%d' to write to file \"%s\"\n", sampleSize, path);
        return false;
    }

    // Try to open file
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "Could not create file \"%s\"\n", path);
        return false;
    }

    void* intBuffer = calloc(numSamples, sampleSize);
    if (intBuffer == NULL) {
        fprintf(stderr, "Not enough memory to write \"%s\"\n", path);
        fclose(file);
        return false;
    }

    switch (sampleSize) {
        // 8 bit ints
        case 8: {
                for (int i = 0; i < numSamples; i++)
                    ((uint8_t*)intBuffer)[i] = data[i] * 127;
                break;
            }
        // 16 bit ints
        case 16: {
                for (int i = 0; i < numSamples; i++) {                   
                    ((uint16_t*)intBuffer)[i] = data[i] * 32767;
                }
                break;
            }
        // 32 bit ints
        case 32: {
                for (int i = 0; i < numSamples; i++)
                    ((uint32_t*)intBuffer)[i] = data[i] * 2147483647;
                break;
            }
    }

    // Instantiate header
    union Header* header = (union Header*)malloc(sizeof(union Header));
    if (header == NULL) {
        fprintf(stderr, "Not enough memory to write \"%s\"\n", path);
        fclose(file);
        free(intBuffer);
        return false;
    }
    memcpy(header->fields.chunk_id, "RIFF", 4);
    header->fields.file_length = sizeof(union Header) + sampleSize / 8 * numSamples * numChannels - 8;
    memcpy(header->fields.file_type, "WAVE", 4);
    memcpy(header->fields.format_chunk, "fmt ", 4);
    header->fields.format_length = 16;
    header->fields.audio_format = 1;
    header->fields.num_channels = numChannels;
    header->fields.sample_rate = sampleRate;
    header->fields.bytes_per_second = sampleRate * sampleSize / 8 * numChannels;
    header->fields.bytes_per_block = sampleSize / 8 * numChannels;
    header->fields.sample_size = sampleSize;
    memcpy(header->fields.data_chunk, "data", 4);
    header->fields.data_length = numChannels * numSamples * sampleSize / 8;

    // Write header
    size_t bytesWritten = fwrite(header->raw, sizeof(char), sizeof(union Header), file);
    if (bytesWritten < sizeof(char) * sizeof(union Header)) {
        fprintf(stderr, "Could not write .wav file header at \"%s\"\n", path);
        fclose(file);
        free(intBuffer);
        free(header);
        return false;
    }

    // Write data chunk
    bytesWritten = fwrite(intBuffer, sizeof(char), numChannels * numSamples * sampleSize / 8, file);
    if (bytesWritten < sizeof(char) * numChannels * numSamples * sampleSize / 8) {
        fprintf(stderr, "Could not write .wav file data chunk at \"%s\"\n", path);
        fclose(file);
        free(intBuffer);
        free(header);
        return false;
    }

    free(header);
    free(intBuffer);
    fclose(file);
    return true;
}

/* Imports a .wav file using the input params */
bool readWav(const char* path, int numChannels, long int numSamples, double* data) {
    // Try to open file
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\"\n", path);
        return false;
    }

    /*
    READ AND VERIFY HEADER 
    */
    union Header* header = (union Header*)malloc(sizeof(union Header));
    // Check if buffer was successfully allocated
    if (header->raw == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\"\n", path);
        fclose(file);
        return false;
    }

    size_t bytesRead = fread(header->raw, sizeof(char), sizeof(union Header), file);
    // Check if file was successfully read
    if (bytesRead < sizeof(char) * sizeof(union Header)) {
        fprintf(stderr, "Could not read file \"%s\"\n", path);
        fclose(file);
        free(header);
        return false;
    }

    // Check format is correct
    if (!memcmp(header->fields.chunk_id, "FFIR", 4) || !memcmp(header->fields.file_type, "WAVEfmt\0", 8)) {
        fprintf(stderr, "Could not read .wav header of file \"%s\"\n", path);
        fclose(file);
        free(header);
        return false;
    }

    /*
    READ DATA CHUNK
    */
    // Make buffer
    void* rawdata = malloc(numSamples * numChannels * header->fields.sample_size / 8);
    if (rawdata == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\"\n", path);
        fclose(file);
        free(header);
        return false;
    }

    // Determine how many samples are in .wav file and choose min(numSample, samples_in_file)
    long samplesToRead = 8 * header->fields.data_length / (header->fields.sample_size * header->fields.num_channels);
    if (samplesToRead > numSamples) samplesToRead = numSamples;

    // Read data
    bytesRead = fread(rawdata, sizeof(char), samplesToRead * header->fields.num_channels * header->fields.sample_size / 8, file);
    if (bytesRead < sizeof(char) * samplesToRead * header->fields.sample_size / 8) {
        fprintf(stderr, "Could not read file %d samples from \"%s\"\n", numSamples, path);
        fclose(file);
        free(header);
        free(rawdata);
        return false;
    }

    // Determine number of channels
    int channels = header->fields.num_channels;
    if (channels > numChannels) channels = numChannels;
    // Change int data to double data
    switch (header->fields.sample_size) {
        // 8 bit ints
        case 8: {
                for (int i = 0; i < bytesRead; i++)
                    for (int c = 0; c < channels; c++)
                        data[i * numChannels + c] = ((int8_t*)rawdata)[i * header->fields.num_channels + c] / 127.0;
                break;
            }
        // 16 bit ints
        case 16: {
                const int samples = bytesRead / 2;
                for (int i = 0; i < samples; i++)
                    for (int c = 0; c < channels; c++)
                        data[i * numChannels + c] = ((int16_t*)rawdata)[i * header->fields.num_channels + c] / 32767.0;
                break;
            }
        // 32 bit ints
        case 32: {
                const int samples = bytesRead / 4;
                for (int i = 0; i < samples; i++)
                    for (int c = 0; c < channels; c++)
                        data[i * numChannels + c] = ((int32_t*)rawdata)[i * header->fields.num_channels + c] / 2147483647.0;
                break;
            }
    }

    free(header);
    free(rawdata);
    fclose(file);
    return true;
}

/*
// Main for testing
void main(int argc, const char* argx) {
    double* waves = (double*)malloc(sizeof(double) * 2048*256);
    readWav("sin-wave.wav", 1, 2048*256, waves);
    writeWav("out-test.wav", 1, 44100, 16, 2048*256, waves);
    free(waves);
}
*/
