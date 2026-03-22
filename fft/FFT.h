#pragma once

#include <vector>

namespace fft {

// Radix-2 Cooley-Tukey FFT on real input.
// n must be a power of 2. Writes n/2+1 complex bins.
void fftReal(const float* in, float* outRe, float* outIm, int n);

// Convenience: computes (re^2 + im^2) / n for bins 0..n/2.
// out must have space for n/2+1 floats.
// Calls runValidation() once on first use.
void powerSpectrum(const float* in, float* out, int n);

} // namespace fft
