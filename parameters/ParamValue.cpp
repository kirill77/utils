#include "ParamValue.h"
#include <stdexcept>
#include <sstream>

// Factory methods
ParamValue ParamValue::fromInt(int value) {
    return ParamValue(value);
}

ParamValue ParamValue::fromDouble(double value) {
    return ParamValue(value);
}

ParamValue ParamValue::fromBool(bool value) {
    return ParamValue(value);
}

ParamValue ParamValue::fromString(const std::string& value) {
    return ParamValue(value);
}

// Private constructors
ParamValue::ParamValue(int value)
    : m_type(ParamType::INT)
    , m_value(value)
{
}

ParamValue::ParamValue(double value)
    : m_type(ParamType::DOUBLE)
    , m_value(value)
{
}

ParamValue::ParamValue(bool value)
    : m_type(ParamType::BOOL)
    , m_value(value)
{
}

ParamValue::ParamValue(const std::string& value)
    : m_type(ParamType::STRING)
    , m_value(value)
{
}

// Constructor from string with explicit type (for CSV loading)
ParamValue::ParamValue(const std::string& stringValue, ParamType type)
    : m_type(type)
    , m_value(parseValue(stringValue, type))
{
}

// Type-safe getters
std::string ParamValue::asString() const {
    return std::get<std::string>(m_value);
}

int ParamValue::asInt() const {
    return std::get<int>(m_value);
}

double ParamValue::asDouble() const {
    return std::get<double>(m_value);
}

bool ParamValue::asBool() const {
    return std::get<bool>(m_value);
}

// Get type name as string
std::string ParamValue::getTypeName() const {
    return typeToString(m_type);
}

// Convert value to string for serialization
std::string ParamValue::toString() const {
    return std::visit([](const auto& val) -> std::string {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return val;
        } else if constexpr (std::is_same_v<T, bool>) {
            return val ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int>) {
            return std::to_string(val);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(val);
        }
    }, m_value);
}

// Type name conversions for CSV
std::string ParamValue::typeToString(ParamType type) {
    switch (type) {
        case ParamType::STRING: return "STRING";
        case ParamType::INT:    return "INT";
        case ParamType::DOUBLE: return "DOUBLE";
        case ParamType::BOOL:   return "BOOL";
        default:                return "UNKNOWN";
    }
}

ParamType ParamValue::stringToType(const std::string& str) {
    if (str == "STRING") return ParamType::STRING;
    if (str == "INT")    return ParamType::INT;
    if (str == "DOUBLE") return ParamType::DOUBLE;
    if (str == "BOOL")   return ParamType::BOOL;
    throw std::invalid_argument("Unknown ParamType: " + str);
}

// Parse string value according to type
ParamValue::Value ParamValue::parseValue(const std::string& stringValue, ParamType type) {
    try {
        switch (type) {
            case ParamType::STRING:
                return stringValue;
            
            case ParamType::INT:
                return std::stoi(stringValue);
            
            case ParamType::DOUBLE:
                return std::stod(stringValue);
            
            case ParamType::BOOL:
                if (stringValue == "true" || stringValue == "1") {
                    return true;
                } else if (stringValue == "false" || stringValue == "0") {
                    return false;
                } else {
                    throw std::invalid_argument("Invalid bool value: " + stringValue);
                }
            
            default:
                throw std::invalid_argument("Unknown ParamType");
        }
    } catch (const std::exception& e) {
        throw std::invalid_argument("Failed to parse '" + stringValue + "' as " + 
                                   typeToString(type) + ": " + e.what());
    }
}

