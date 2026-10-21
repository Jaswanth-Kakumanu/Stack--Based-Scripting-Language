#include "cairn/value.hpp"

namespace cairn {

bool operator==(const Value& lhs, const Value& rhs) noexcept {
    if (lhs.type != rhs.type) {
        return false;
    }
    switch (lhs.type) {
        case Value::Type::Int:  return lhs.as_int == rhs.as_int;
        case Value::Type::Bool: return lhs.as_bool == rhs.as_bool;
    }
    return false;
}

bool operator!=(const Value& lhs, const Value& rhs) noexcept { return !(lhs == rhs); }

std::string to_string(const Value& value) {
    switch (value.type) {
        case Value::Type::Int:  return std::to_string(value.as_int);
        case Value::Type::Bool: return value.as_bool ? "true" : "false";
    }
    return "?";
}

}  // namespace cairn
