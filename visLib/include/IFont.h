#pragma once

#include "utils/visLib/include/Types.h"
#include <cstdint>
#include <memory>

namespace visLib {

// Glyph metrics for text layout
struct GlyphInfo {
    float2 texCoordMin;     // UV coordinates in font atlas (top-left)
    float2 texCoordMax;     // UV coordinates in font atlas (bottom-right)
    float2 size;            // Glyph dimensions in pixels
    float2 bearing;         // Offset from baseline to top-left of glyph
    float advance;          // Horizontal advance to next character
};

// Abstract font interface
// Represents a font loaded for GPU text rendering
class IFont {
public:
    virtual ~IFont() = default;

    // Get font metrics
    virtual float getFontSize() const = 0;
    virtual float getLineHeight() const = 0;

    // Get glyph information for a character
    // Returns nullptr if character is not in the font
    virtual const GlyphInfo* getGlyphInfo(char character) const = 0;
};

} // namespace visLib
