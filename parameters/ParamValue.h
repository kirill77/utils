#pragma once

#include <string>
#include <variant>

// Type of a parameter value
enum class ParamType {
    STRING,
    INT,
    DOUBLE,
    BOOL
};

// Type-aware parameter value
// Stores a value with explicit type information
class ParamValue {
public:
    using Value = std::variant<std::string, int, double, bool>;
    
    // Factory methods for explicit construction
    static ParamValue fromInt(int value);
    static ParamValue fromDouble(double value);
    static ParamValue fromBool(bool value);
    static ParamValue fromString(const std::string& value);
    
    // Constructor from string with explicit type (for CSV loading)
    ParamValue(const std::string& stringValue, ParamType type);
    
    // Type-safe getters (throw std::bad_variant_access if type mismatch)
    std::string asString() const;
    int asInt() const;
    double asDouble() const;
    bool asBool() const;
    
    // Type metadata
    ParamType getType() const { return m_type; }
    std::string getTypeName() const;
    
    // Serialization - convert value to string
    std::string toString() const;
    
    // Type name to/from string conversion (for CSV)
    static std::string typeToString(ParamType type);
    static ParamType stringToType(const std::string& str);
    
private:
    friend class Parameters;  // Allow Parameters to use private constructors
    
    // Private constructors - use factory methods or let Parameters class handle
    explicit ParamValue(int value);
    explicit ParamValue(double value);
    explicit ParamValue(bool value);
    explicit ParamValue(const std::string& value);
    
    // Parse string value according to type
    static Value parseValue(const std::string& stringValue, ParamType type);
    
    ParamType m_type;
    Value m_value;
};

