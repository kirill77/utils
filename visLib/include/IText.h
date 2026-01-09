#pragma once

#include "Types.h"
#include <cstdint>
#include <string>
#include <memory>
#include <cstdarg>

namespace visLib {

// Forward declarations
class IFont;

// Text line with formatting options
class TextLine {
public:
    virtual ~TextLine() = default;

    // Set text content using printf-style formatting
    virtual int printf(const char* format, ...) = 0;

    // Get current text content
    virtual const std::string& getText() const = 0;
    virtual bool isEmpty() const = 0;

    // Text color (RGBA, 0.0-1.0)
    virtual void setColor(const float4& color) = 0;
    virtual const float4& getColor() const = 0;

    // Lifetime control (0 = permanent, >0 = auto-remove after N seconds)
    virtual void setLifetime(uint32_t seconds) = 0;
    virtual uint32_t getLifetime() const = 0;
};

// Abstract text rendering interface
// Manages a collection of text lines for on-screen display
class IText {
public:
    virtual ~IText() = default;

    // Set position for text block (screen coordinates, pixels from top-left)
    virtual void setPosition(const float2& position) = 0;

    // Create a new text line (returned line is owned by IText)
    virtual std::shared_ptr<TextLine> createLine() = 0;

    // Set default text color for new lines
    virtual void setDefaultColor(const float4& color) = 0;

    // Get the font used by this text renderer
    virtual std::shared_ptr<IFont> getFont() const = 0;
};

} // namespace visLib
