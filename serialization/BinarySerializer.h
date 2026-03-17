#pragma once

#include <iostream>
#include <string>
#include <cstdint>

namespace serialization {

enum class SerializerMode { Read, Write };

/**
 * Bidirectional binary serializer.  A single serialize() call either
 * reads or writes a value depending on the mode set at construction.
 *
 * On read, once any operation fails the stream latches into a bad state
 * and subsequent serialize() calls become no-ops.
 *
 * Strings are length-prefixed (uint32_t length + raw bytes).
 * A maximum length can be specified to reject corrupted data on read.
 */
class BinarySerializer
{
public:
    BinarySerializer(std::istream& in)
        : m_pIn(&in), m_pOut(nullptr), m_mode(SerializerMode::Read) {}

    BinarySerializer(std::ostream& out)
        : m_pIn(nullptr), m_pOut(&out), m_mode(SerializerMode::Write) {}

    SerializerMode mode() const { return m_mode; }
    bool isReading()      const { return m_mode == SerializerMode::Read; }
    bool isWriting()      const { return m_mode == SerializerMode::Write; }

    void serialize(uint8_t&  v) { serializeRaw(v); }
    void serialize(int32_t&  v) { serializeRaw(v); }
    void serialize(uint32_t& v) { serializeRaw(v); }
    void serialize(int64_t&  v) { serializeRaw(v); }
    void serialize(uint64_t& v) { serializeRaw(v); }
    void serialize(double&   v) { serializeRaw(v); }

    static constexpr uint32_t DEFAULT_MAX_STRING_LEN = 1u << 20; // 1 MB

    void serialize(std::string& s, uint32_t maxLen = 0)
    {
        uint32_t len = static_cast<uint32_t>(s.size());
        serializeRaw(len);

        if (m_mode == SerializerMode::Read)
        {
            if (!m_pIn->good())
                return;
            uint32_t effectiveMax = (maxLen > 0) ? maxLen : DEFAULT_MAX_STRING_LEN;
            if (len > effectiveMax)
            {
                m_pIn->setstate(std::ios::failbit);
                return;
            }
            s.resize(len);
            m_pIn->read(s.data(), len);
        }
        else
        {
            m_pOut->write(s.data(), len);
        }
    }

    /**
     * Write or verify a sentinel value.  On write, emits the constant.
     * On read, reads a uint32_t and returns false if it doesn't match.
     * Structural changes to the serialized format will misalign the
     * stream, causing a sentinel mismatch — no manual version bump needed.
     */
    bool serializeSentinel(uint32_t expected)
    {
        uint32_t v = expected;
        serializeRaw(v);
        if (m_mode == SerializerMode::Read && v != expected)
        {
            m_pIn->setstate(std::ios::failbit);
            return false;
        }
        return good();
    }

    bool good() const
    {
        return m_mode == SerializerMode::Read ? m_pIn->good() : m_pOut->good();
    }

private:
    template <typename T>
    void serializeRaw(T& v)
    {
        if (m_mode == SerializerMode::Read)
            m_pIn->read(reinterpret_cast<char*>(&v), sizeof(v));
        else
            m_pOut->write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    std::istream* m_pIn;
    std::ostream* m_pOut;
    SerializerMode m_mode;
};

} // namespace serialization
