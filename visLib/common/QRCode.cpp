#include "QRCode.h"
#include <stdexcept>
#include <cstring>
#include <vector>

namespace visLib {

// ============================================================================
// GF(256) arithmetic for Reed-Solomon (primitive polynomial 0x11D)
// ============================================================================

static uint8_t gfExp[512]; // antilog table (doubled for mod-free multiply)
static uint8_t gfLog[256]; // log table
static bool gfInitDone = false;

static void gfInit()
{
    if (gfInitDone) return;
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gfExp[i] = static_cast<uint8_t>(x);
        gfLog[x] = static_cast<uint8_t>(i);
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) {
        gfExp[i] = gfExp[i - 255];
    }
    gfLog[0] = 0; // undefined, but 0 is convenient
    gfInitDone = true;
}

static uint8_t gfMul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gfExp[gfLog[a] + gfLog[b]];
}

// ============================================================================
// Reed-Solomon encoder
// ============================================================================

// Generate RS error correction codewords
// data: input data codewords
// numEC: number of EC codewords to generate
static std::vector<uint8_t> rsEncode(const std::vector<uint8_t>& data, int numEC)
{
    gfInit();

    // Build generator polynomial (roots at alpha^0 .. alpha^(numEC-1))
    std::vector<uint8_t> gen(numEC + 1, 0);
    gen[0] = 1;
    for (int i = 0; i < numEC; i++) {
        for (int j = numEC; j > 0; j--) {
            gen[j] = gfMul(gen[j], gfExp[i]) ^ gen[j - 1];
        }
        gen[0] = gfMul(gen[0], gfExp[i]);
    }

    // Polynomial division
    std::vector<uint8_t> remainder(numEC, 0);
    for (size_t i = 0; i < data.size(); i++) {
        uint8_t feedback = data[i] ^ remainder[0];
        for (int j = 0; j < numEC - 1; j++) {
            remainder[j] = remainder[j + 1] ^ gfMul(feedback, gen[numEC - 1 - j]);
        }
        remainder[numEC - 1] = gfMul(feedback, gen[0]);
    }
    return remainder;
}

// ============================================================================
// Alphanumeric encoding
// ============================================================================

static const char* ALPHANUM_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

static int alphanumIndex(char c)
{
    const char* p = std::strchr(ALPHANUM_CHARS, c);
    if (!p) return -1;
    return static_cast<int>(p - ALPHANUM_CHARS);
}

// ============================================================================
// Bit stream helper
// ============================================================================

struct BitStream {
    std::vector<uint8_t> bytes;
    int bitCount = 0;

    void appendBits(uint32_t value, int count) {
        for (int i = count - 1; i >= 0; i--) {
            int byteIdx = bitCount / 8;
            int bitIdx = 7 - (bitCount % 8);
            if (byteIdx >= static_cast<int>(bytes.size()))
                bytes.push_back(0);
            if ((value >> i) & 1)
                bytes[byteIdx] |= (1 << bitIdx);
            bitCount++;
        }
    }

    // Pad to fill required data codeword count
    void padToBytes(int targetBytes) {
        // Terminator (up to 4 zero bits)
        int terminatorBits = std::min(4, targetBytes * 8 - bitCount);
        appendBits(0, terminatorBits);

        // Pad to byte boundary
        int remainder = bitCount % 8;
        if (remainder != 0)
            appendBits(0, 8 - remainder);

        // Pad with alternating 0xEC, 0x11
        uint8_t padBytes[] = { 0xEC, 0x11 };
        int idx = 0;
        while (static_cast<int>(bytes.size()) < targetBytes) {
            bytes.push_back(padBytes[idx % 2]);
            bitCount += 8;
            idx++;
        }
    }
};

// ============================================================================
// QR Code module placement (Version 1)
// ============================================================================

// Version 1 constants
static constexpr int QR_SIZE = 21;
static constexpr int DATA_CODEWORDS_M = 16;  // Version 1-M: 16 data codewords
static constexpr int EC_CODEWORDS_M = 10;    // Version 1-M: 10 EC codewords

// Place finder pattern (7x7) at (row, col)
static void placeFinder(bool modules[][QR_SIZE], bool reserved[][QR_SIZE], int row, int col)
{
    for (int r = -1; r <= 7; r++) {
        for (int c = -1; c <= 7; c++) {
            int rr = row + r, cc = col + c;
            if (rr < 0 || rr >= QR_SIZE || cc < 0 || cc >= QR_SIZE)
                continue;
            bool dark = (r >= 0 && r <= 6 && c >= 0 && c <= 6) &&
                        (r == 0 || r == 6 || c == 0 || c == 6 ||
                         (r >= 2 && r <= 4 && c >= 2 && c <= 4));
            modules[rr][cc] = dark;
            reserved[rr][cc] = true;
        }
    }
}

// Place timing patterns
static void placeTiming(bool modules[][QR_SIZE], bool reserved[][QR_SIZE])
{
    for (int i = 8; i < QR_SIZE - 8; i++) {
        bool dark = (i % 2 == 0);
        // Horizontal timing
        if (!reserved[6][i]) {
            modules[6][i] = dark;
            reserved[6][i] = true;
        }
        // Vertical timing
        if (!reserved[i][6]) {
            modules[i][6] = dark;
            reserved[i][6] = true;
        }
    }
}

// Reserve format information areas (don't write data here)
static void reserveFormatAreas(bool reserved[][QR_SIZE])
{
    // Around top-left finder
    for (int i = 0; i < 9; i++) {
        reserved[8][i] = true;
        reserved[i][8] = true;
    }
    // Around top-right finder
    for (int i = 0; i < 8; i++) {
        reserved[8][QR_SIZE - 1 - i] = true;
    }
    // Around bottom-left finder
    for (int i = 0; i < 7; i++) {
        reserved[QR_SIZE - 1 - i][8] = true;
    }
    // Dark module
    reserved[QR_SIZE - 8][8] = true;
}

// Get the data module positions in the correct order (right-to-left, bottom-to-top zigzag)
static std::vector<std::pair<int, int>> getDataPositions(const bool reserved[][QR_SIZE])
{
    std::vector<std::pair<int, int>> positions;
    int col = QR_SIZE - 1;
    bool upward = true;

    while (col >= 0) {
        if (col == 6) col--; // Skip vertical timing column

        for (int step = 0; step < QR_SIZE; step++) {
            int row = upward ? (QR_SIZE - 1 - step) : step;
            // Right column of the pair
            if (col >= 0 && !reserved[row][col])
                positions.push_back({ row, col });
            // Left column of the pair
            if (col - 1 >= 0 && !reserved[row][col - 1])
                positions.push_back({ row, col - 1 });
        }

        col -= 2;
        upward = !upward;
    }
    return positions;
}

// Place data bits into the module matrix
static void placeData(bool modules[][QR_SIZE], const bool reserved[][QR_SIZE],
                      const std::vector<uint8_t>& data)
{
    auto positions = getDataPositions(reserved);
    int bitIdx = 0;
    int totalBits = static_cast<int>(data.size()) * 8;

    for (const auto& [row, col] : positions) {
        if (bitIdx < totalBits) {
            int byteIdx = bitIdx / 8;
            int bitPos = 7 - (bitIdx % 8);
            modules[row][col] = (data[byteIdx] >> bitPos) & 1;
            bitIdx++;
        }
    }
}

// ============================================================================
// Masking
// ============================================================================

using MaskFunc = bool(*)(int row, int col);

static bool mask0(int r, int c) { return (r + c) % 2 == 0; }
static bool mask1(int r, int /*c*/) { return r % 2 == 0; }
static bool mask2(int /*r*/, int c) { return c % 3 == 0; }
static bool mask3(int r, int c) { return (r + c) % 3 == 0; }
static bool mask4(int r, int c) { return (r / 2 + c / 3) % 2 == 0; }
static bool mask5(int r, int c) { return (r * c) % 2 + (r * c) % 3 == 0; }
static bool mask6(int r, int c) { return ((r * c) % 2 + (r * c) % 3) % 2 == 0; }
static bool mask7(int r, int c) { return ((r + c) % 2 + (r * c) % 3) % 2 == 0; }

static MaskFunc maskFuncs[8] = { mask0, mask1, mask2, mask3, mask4, mask5, mask6, mask7 };

static void applyMask(bool modules[][QR_SIZE], const bool reserved[][QR_SIZE], int maskIdx)
{
    for (int r = 0; r < QR_SIZE; r++) {
        for (int c = 0; c < QR_SIZE; c++) {
            if (!reserved[r][c] && maskFuncs[maskIdx](r, c)) {
                modules[r][c] = !modules[r][c];
            }
        }
    }
}

// ============================================================================
// Penalty scoring (simplified — we evaluate all 8 masks and pick the lowest)
// ============================================================================

static int computePenalty(const bool modules[][QR_SIZE])
{
    int penalty = 0;

    // Rule 1: runs of 5+ same-color modules
    for (int r = 0; r < QR_SIZE; r++) {
        int run = 1;
        for (int c = 1; c < QR_SIZE; c++) {
            if (modules[r][c] == modules[r][c - 1]) {
                run++;
            } else {
                if (run >= 5) penalty += run - 2;
                run = 1;
            }
        }
        if (run >= 5) penalty += run - 2;
    }
    for (int c = 0; c < QR_SIZE; c++) {
        int run = 1;
        for (int r = 1; r < QR_SIZE; r++) {
            if (modules[r][c] == modules[r - 1][c]) {
                run++;
            } else {
                if (run >= 5) penalty += run - 2;
                run = 1;
            }
        }
        if (run >= 5) penalty += run - 2;
    }

    // Rule 2: 2x2 blocks of same color
    for (int r = 0; r < QR_SIZE - 1; r++) {
        for (int c = 0; c < QR_SIZE - 1; c++) {
            bool v = modules[r][c];
            if (v == modules[r][c + 1] && v == modules[r + 1][c] && v == modules[r + 1][c + 1])
                penalty += 3;
        }
    }

    return penalty;
}

// ============================================================================
// Format information
// ============================================================================

// Pre-computed format info strings for error correction level M (01) with masks 0-7
// Format: 5-bit data (EC level + mask) + 10-bit BCH error correction, XORed with 0x5412
static const uint16_t FORMAT_INFO_M[8] = {
    0x5412 ^ 0x0800, // mask 0: data=01000
    0x5412 ^ 0x0A00, // mask 1: data=01001  (shifted to 15 bits below)
    0x5412 ^ 0x0C00, // mask 2: data=01010
    0x5412 ^ 0x0E00, // mask 3: data=01011
    0x5412 ^ 0x1000, // mask 4: data=01100
    0x5412 ^ 0x1200, // mask 5: data=01101
    0x5412 ^ 0x1400, // mask 6: data=01110
    0x5412 ^ 0x1600, // mask 7: data=01111
};

// Compute BCH(15,5) for format information
static uint32_t bchFormatInfo(uint32_t data5)
{
    // Generator polynomial for QR format info: x^10 + x^8 + x^5 + x^4 + x^2 + x + 1 = 0x537
    uint32_t d = data5 << 10;
    uint32_t gen = 0x537;
    for (int i = 4; i >= 0; i--) {
        if (d & (1 << (i + 10)))
            d ^= gen << i;
    }
    return (data5 << 10) | d;
}

static void placeFormatInfo(bool modules[][QR_SIZE], int maskIdx)
{
    // EC level M = 00, wait no. The format info encodes:
    // 2-bit EC level indicator: L=01, M=00, Q=11, H=10
    // 3-bit mask pattern
    uint32_t data5 = (0b00 << 3) | maskIdx;  // M level = 00
    uint32_t formatBits = bchFormatInfo(data5) ^ 0x5412;

    // Place around top-left finder (horizontal + vertical)
    // Bits 14..0, MSB first
    static const int hPositions[][2] = {
        {8, 0}, {8, 1}, {8, 2}, {8, 3}, {8, 4}, {8, 5}, {8, 7}, {8, 8},
        {7, 8}, {5, 8}, {4, 8}, {3, 8}, {2, 8}, {1, 8}, {0, 8}
    };
    static const int vPositions[][2] = {
        {QR_SIZE - 1, 8}, {QR_SIZE - 2, 8}, {QR_SIZE - 3, 8}, {QR_SIZE - 4, 8},
        {QR_SIZE - 5, 8}, {QR_SIZE - 6, 8}, {QR_SIZE - 7, 8},
        {8, QR_SIZE - 8}, {8, QR_SIZE - 7}, {8, QR_SIZE - 6}, {8, QR_SIZE - 5},
        {8, QR_SIZE - 4}, {8, QR_SIZE - 3}, {8, QR_SIZE - 2}, {8, QR_SIZE - 1}
    };

    for (int i = 0; i < 15; i++) {
        bool dark = (formatBits >> (14 - i)) & 1;
        modules[hPositions[i][0]][hPositions[i][1]] = dark;
        modules[vPositions[i][0]][vPositions[i][1]] = dark;
    }

    // Dark module (always dark)
    modules[QR_SIZE - 8][8] = true;
}

// ============================================================================
// Public interface
// ============================================================================

std::array<uint32_t, QRCode::PACKED_UINT32S> QRCode::pack() const
{
    std::array<uint32_t, PACKED_UINT32S> result = {};
    for (int r = 0; r < SIZE; r++) {
        for (int c = 0; c < SIZE; c++) {
            int bitIdx = r * SIZE + c;
            if (modules[r][c]) {
                result[bitIdx / 32] |= (1u << (bitIdx % 32));
            }
        }
    }
    return result;
}

QRCode QRCode::encode(const std::string& text)
{
    if (text.size() > 20) {
        throw std::runtime_error("QRCode::encode: text too long for Version 1 (max 20 alphanumeric chars)");
    }

    // Validate and encode alphanumeric data
    for (char c : text) {
        if (alphanumIndex(c) < 0) {
            throw std::runtime_error(std::string("QRCode::encode: invalid alphanumeric character '") + c + "'");
        }
    }

    // Build data bitstream
    BitStream bs;
    bs.appendBits(0b0010, 4);                         // Mode: alphanumeric
    bs.appendBits(static_cast<uint32_t>(text.size()), 9); // Character count (9 bits for V1)

    // Encode pairs of characters
    for (size_t i = 0; i + 1 < text.size(); i += 2) {
        int val = alphanumIndex(text[i]) * 45 + alphanumIndex(text[i + 1]);
        bs.appendBits(val, 11);
    }
    // Odd remainder character
    if (text.size() % 2 == 1) {
        bs.appendBits(alphanumIndex(text.back()), 6);
    }

    bs.padToBytes(DATA_CODEWORDS_M);

    // Reed-Solomon error correction
    auto ecBytes = rsEncode(bs.bytes, EC_CODEWORDS_M);

    // Interleave data + EC (Version 1 has single block, so just concatenate)
    std::vector<uint8_t> allCodewords = bs.bytes;
    allCodewords.insert(allCodewords.end(), ecBytes.begin(), ecBytes.end());

    // Build the QR code matrix
    bool modules[QR_SIZE][QR_SIZE] = {};
    bool reserved[QR_SIZE][QR_SIZE] = {};

    // Place fixed patterns
    placeFinder(modules, reserved, 0, 0);                   // Top-left
    placeFinder(modules, reserved, 0, QR_SIZE - 7);         // Top-right
    placeFinder(modules, reserved, QR_SIZE - 7, 0);         // Bottom-left
    placeTiming(modules, reserved);
    reserveFormatAreas(reserved);

    // Place data
    placeData(modules, reserved, allCodewords);

    // Try all 8 masks, pick the one with lowest penalty
    int bestMask = 0;
    int bestPenalty = INT_MAX;

    bool testModules[QR_SIZE][QR_SIZE];
    for (int m = 0; m < 8; m++) {
        std::memcpy(testModules, modules, sizeof(modules));
        applyMask(testModules, reserved, m);
        placeFormatInfo(testModules, m);
        int p = computePenalty(testModules);
        if (p < bestPenalty) {
            bestPenalty = p;
            bestMask = m;
        }
    }

    // Apply best mask
    applyMask(modules, reserved, bestMask);
    placeFormatInfo(modules, bestMask);

    // Copy result
    QRCode qr;
    std::memcpy(qr.modules, modules, sizeof(modules));
    return qr;
}

} // namespace visLib
