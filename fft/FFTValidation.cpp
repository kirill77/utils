#include "FFTValidation.h"
#include "FFT.h"

#include "utils/log/ILog.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace {

constexpr float kEps = 1e-4f;

// Naive DFT for reference (O(N^2))
void naiveDFT(const float* in, float* outRe, float* outIm, int n)
{
    for (int k = 0; k < n / 2 + 1; ++k) {
        float re = 0.0f, im = 0.0f;
        for (int j = 0; j < n; ++j) {
            float angle = 2.0f * std::numbers::pi_v<float> * k * j / static_cast<float>(n);
            re += in[j] * std::cos(angle);
            im -= in[j] * std::sin(angle);
        }
        outRe[k] = re;
        outIm[k] = im;
    }
}

// Test 1: DC signal — all energy in bin 0
bool test_dc()
{
    constexpr int N = 64;
    std::vector<float> in(N, 1.0f);
    std::vector<float> ps(N / 2 + 1);
    fft::powerSpectrum(in.data(), ps.data(), N);

    // Bin 0 should have power = N (sum of 1s squared / N = N^2 / N = N)
    if (std::abs(ps[0] - static_cast<float>(N)) > kEps) return false;

    // All other bins should be ~0
    for (int k = 1; k < N / 2 + 1; ++k) {
        if (ps[k] > kEps) return false;
    }
    return true;
}

// Test 2: Pure sine — peak at correct bin
bool test_sine()
{
    constexpr int N = 256;
    constexpr int freqBin = 10;
    std::vector<float> in(N);
    for (int i = 0; i < N; ++i) {
        in[i] = std::sin(2.0f * std::numbers::pi_v<float> * freqBin * i / N);
    }

    std::vector<float> ps(N / 2 + 1);
    fft::powerSpectrum(in.data(), ps.data(), N);

    // Find bin with max power
    int maxBin = 0;
    for (int k = 1; k < N / 2 + 1; ++k) {
        if (ps[k] > ps[maxBin]) maxBin = k;
    }
    return maxBin == freqBin;
}

// Test 3: Parseval's theorem — time-domain energy ≈ freq-domain energy
bool test_parseval()
{
    constexpr int N = 512;
    std::vector<float> in(N);
    for (int i = 0; i < N; ++i) {
        in[i] = std::sin(3.0f * i) + 0.5f * std::cos(7.0f * i);
    }

    // Time-domain energy
    float timeEnergy = 0.0f;
    for (int i = 0; i < N; ++i) timeEnergy += in[i] * in[i];

    // Freq-domain energy: sum of |X[k]|^2 / N
    std::vector<float> re(N / 2 + 1), im(N / 2 + 1);
    fft::fftReal(in.data(), re.data(), im.data(), N);

    float freqEnergy = 0.0f;
    for (int k = 0; k < N / 2 + 1; ++k) {
        float mag2 = re[k] * re[k] + im[k] * im[k];
        // Bin 0 and bin N/2 appear once; all others appear twice (conjugate symmetry)
        if (k == 0 || k == N / 2)
            freqEnergy += mag2;
        else
            freqEnergy += 2.0f * mag2;
    }
    freqEnergy /= static_cast<float>(N);

    return std::abs(timeEnergy - freqEnergy) / timeEnergy < 1e-3f;
}

// Test 4: Known 8-point DFT values
bool test_known_8pt()
{
    constexpr int N = 8;
    float in[N] = {1, 0, -1, 0, 1, 0, -1, 0};
    // DFT of this signal: X[0]=0, X[1]=0, X[2]=4, X[3]=0, X[4]=0
    float re[N / 2 + 1], im[N / 2 + 1];
    fft::fftReal(in, re, im, N);

    if (std::abs(re[0]) > kEps) return false;
    if (std::abs(re[1]) > kEps || std::abs(im[1]) > kEps) return false;
    if (std::abs(re[2] - 4.0f) > kEps || std::abs(im[2]) > kEps) return false;
    if (std::abs(re[3]) > kEps || std::abs(im[3]) > kEps) return false;
    if (std::abs(re[4]) > kEps) return false;
    return true;
}

// Test 5: Cross-check against naive DFT for various sizes
bool test_cross_check()
{
    for (int N : {64, 256, 512, 1024}) {
        std::vector<float> in(N);
        for (int i = 0; i < N; ++i) {
            in[i] = std::sin(0.1f * i) * std::cos(0.03f * i) + 0.2f;
        }

        int nBins = N / 2 + 1;
        std::vector<float> fftRe(nBins), fftIm(nBins);
        std::vector<float> dftRe(nBins), dftIm(nBins);

        fft::fftReal(in.data(), fftRe.data(), fftIm.data(), N);
        naiveDFT(in.data(), dftRe.data(), dftIm.data(), N);

        for (int k = 0; k < nBins; ++k) {
            if (std::abs(fftRe[k] - dftRe[k]) > 0.05f) return false;
            if (std::abs(fftIm[k] - dftIm[k]) > 0.05f) return false;
        }
    }
    return true;
}

} // anonymous namespace

bool fft::runValidation()
{
    static bool s_alreadyRun = false;
    if (s_alreadyRun)
        return true;
    s_alreadyRun = true;

    LOG_INFO("FFT: running validation (5 tests)...");

    struct TestEntry { bool (*fn)(); const char* name; };
    TestEntry tests[] = {
        { test_dc,          "FFT:dc_signal" },
        { test_sine,        "FFT:sine_peak" },
        { test_parseval,    "FFT:parseval" },
        { test_known_8pt,   "FFT:known_8pt" },
        { test_cross_check, "FFT:cross_check_vs_naive" },
    };

    int total = static_cast<int>(std::size(tests));
    for (int i = 0; i < total; ++i) {
        if (!tests[i].fn()) {
            LOG_ERROR("FFT validation FAILED at test %d/%d: %s", i + 1, total, tests[i].name);
            return false;
        }
    }

    LOG_INFO("FFT: all %d validation tests passed.", total);
    return true;
}
