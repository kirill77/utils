#include "FFT.h"
#include "FFTValidation.h"

#include <cmath>
#include <numbers>

namespace fft {

// In-place radix-2 decimation-in-time FFT on interleaved complex data.
// data layout: [re0, im0, re1, im1, ...], length = 2*n.
static void fftComplex(float* data, int n)
{
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[2 * i],     data[2 * j]);
            std::swap(data[2 * i + 1], data[2 * j + 1]);
        }
    }

    // Cooley-Tukey butterfly passes
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
        float wRe = std::cos(angle);
        float wIm = std::sin(angle);

        for (int i = 0; i < n; i += len) {
            float curRe = 1.0f, curIm = 0.0f;
            for (int j = 0; j < len / 2; ++j) {
                int u = 2 * (i + j);
                int v = 2 * (i + j + len / 2);

                float tRe = curRe * data[v]     - curIm * data[v + 1];
                float tIm = curRe * data[v + 1] + curIm * data[v];

                data[v]     = data[u]     - tRe;
                data[v + 1] = data[u + 1] - tIm;
                data[u]     += tRe;
                data[u + 1] += tIm;

                float nextRe = curRe * wRe - curIm * wIm;
                float nextIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
                curIm = nextIm;
            }
        }
    }
}

void fftReal(const float* in, float* outRe, float* outIm, int n)
{
    // Pack real input into interleaved complex array (imaginary = 0)
    std::vector<float> buf(2 * n, 0.0f);
    for (int i = 0; i < n; ++i) {
        buf[2 * i] = in[i];
    }

    fftComplex(buf.data(), n);

    // Extract n/2+1 bins
    int nBins = n / 2 + 1;
    for (int k = 0; k < nBins; ++k) {
        outRe[k] = buf[2 * k];
        outIm[k] = buf[2 * k + 1];
    }
}

void powerSpectrum(const float* in, float* out, int n)
{
    static bool s_validated = false;
    if (!s_validated) {
        runValidation();
        s_validated = true;
    }

    int nBins = n / 2 + 1;
    std::vector<float> re(nBins), im(nBins);
    fftReal(in, re.data(), im.data(), n);

    float invN = 1.0f / static_cast<float>(n);
    for (int k = 0; k < nBins; ++k) {
        out[k] = (re[k] * re[k] + im[k] * im[k]) * invN;
    }
}

} // namespace fft
