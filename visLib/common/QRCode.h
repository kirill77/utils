#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace visLib {

// QR Code Version 1 encoder (21x21 modules)
// Supports alphanumeric mode with M-level error correction
// Encodes up to 20 alphanumeric characters
struct QRCode {
    static constexpr int SIZE = 21;
    static constexpr int BITS = SIZE * SIZE;              // 441
    static constexpr int PACKED_UINT32S = (BITS + 31) / 32; // 14

    // Module matrix: true = dark, false = light
    bool modules[SIZE][SIZE];

    // Pack the module matrix into uint32 array for GPU upload
    // Row-major, bit 0 of word 0 = modules[0][0]
    std::array<uint32_t, PACKED_UINT32S> pack() const;

    // Encode an alphanumeric string (A-Z, 0-9, space, $%*+-./:)
    // Throws std::runtime_error if text is too long or contains invalid chars
    static QRCode encode(const std::string& text);
};

} // namespace visLib
