#ifndef wavetable_fft_h
#define wavetable_fft_h

#include <complex.h> 

void fft_2048_by2(double* in, double _Complex* out);
void ifft_2048_by2(double _Complex* in, double* out);

#endif