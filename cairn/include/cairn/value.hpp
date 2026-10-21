#pragma once

#include <cstdint>
#include <string>

namespace cairn {

// A runtime value: 64-bit integer or boolean.
//
// A tagged union, not std::variant. variant carries its own index, generates a
// visit dispatch, and is not trivially copyable for every alternative set; this
// is 16 bytes, trivially copyable, and the tag check compiles to a byte compare.
// See DESIGN.md D26 for the NaN-boxing alternative and why it is not here yet.
struct Value {
    enum class Type : std::uint8_t {
        Int,
        Bool,
    };

    Type type = Type::Int;
    union {
        std::int64_t as_int;
        bool as_bool;
    };

    constexpr Value() noexcept : type(Type::Int), as_int(0) {}

    static constexpr Value integer(std::int64_t v) noexcept {
        Value value;
        value.type = Type::Int;
        value.as_int = v;
        return value;
    }

    static constexpr Value boolean(bool v) noexcept {
        Value value;
        value.type = Type::Bool;
        value.as_bool = v;
        return value;
    }

    constexpr bool is_int() const noexcept { return type == Type::Int; }
    constexpr bool is_bool() const noexcept { return type == Type::Bool; }
};

// Value equality is type-strict: 1 and true are different values, so the
// constant pool never merges them.
bool operator==(const Value& lhs, const Value& rhs) noexcept;
bool operator!=(const Value& lhs, const Value& rhs) noexcept;

std::string to_string(const Value& value);

}  // namespace cairn
