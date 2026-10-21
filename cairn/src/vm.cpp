#include "cairn/vm.hpp"

#include <istream>
#include <limits>
#include <ostream>
#include <sstream>

// Computed goto is a GCC/Clang extension. Where it is unavailable - or when
// CAIRN_FORCE_SWITCH is defined to benchmark the two against each other - the
// same loop body compiles as an ordinary switch.
#if defined(__GNUC__) && !defined(CAIRN_FORCE_SWITCH)
#define CAIRN_COMPUTED_GOTO 1
#else
#define CAIRN_COMPUTED_GOTO 0
#endif

// One instruction body, two dispatch strategies. The macros keep a single copy
// of the semantics: duplicating the loop would guarantee the two drift apart.
#if CAIRN_COMPUTED_GOTO
#define VM_LOOP_BEGIN() DISPATCH();
#define VM_CASE(name) L_##name
#define VM_LOOP_END()
#define DISPATCH() goto* kDispatch[read_byte()]
#else
#define VM_LOOP_BEGIN() \
    for (;;) {          \
        switch (static_cast<OpCode>(read_byte())) {
#define VM_CASE(name) case OpCode::name
#define VM_LOOP_END() \
    }                 \
    }
#define DISPATCH() break
#endif

#if CAIRN_COMPUTED_GOTO
// Computed gotos and label addresses are GNU extensions. Rather than dropping
// -Wpedantic from the build, it is disabled for this one translation unit.
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

namespace cairn {

bool VM::uses_computed_goto() noexcept { return CAIRN_COMPUTED_GOTO != 0; }

VM::VM(std::ostream& out, std::istream& in) noexcept : out_(out), in_(in) {
    stack_.reserve(kStackCapacity);
}

VM::Result VM::run(const Chunk& chunk) {
    error_.clear();
    instructions_ = 0;
    stack_.clear();

    const std::uint8_t* const base = chunk.code().data();
    const std::uint8_t* ip = base;
    const Value* const constants = chunk.constants().data();

    // The stack is a raw pointer into preallocated storage. Capacity is fixed,
    // so no reallocation can invalidate it mid-run.
    Value* const stack_base = stack_.data();
    Value* top = stack_base;

#if CAIRN_COMPUTED_GOTO
    // Order must match the OpCode enum exactly; the static_assert below catches
    // a new opcode added without a matching entry.
    static void* const kDispatch[] = {
        &&L_Constant, &&L_True,     &&L_False,    &&L_GetLocal,     &&L_SetLocal,
        &&L_Pop,      &&L_Add,      &&L_Sub,      &&L_Mul,          &&L_Div,
        &&L_Negate,   &&L_Not,      &&L_Equal,    &&L_NotEqual,     &&L_Less,
        &&L_LessEqual, &&L_Greater, &&L_GreaterEqual, &&L_Jump,     &&L_JumpIfFalse,
        &&L_Loop,     &&L_Print,    &&L_Input,    &&L_Halt,
    };
    static_assert(sizeof(kDispatch) / sizeof(kDispatch[0]) == kOpCodeCount,
                  "dispatch table and OpCode enum have drifted apart");
#endif

    const auto read_byte = [&ip]() noexcept -> std::uint8_t { return *ip++; };
    const auto read_u16 = [&ip]() noexcept -> std::uint16_t {
        const std::uint16_t low = *ip++;
        const std::uint16_t high = *ip++;
        return static_cast<std::uint16_t>(low | (high << 8));
    };

    // Reports against the opcode byte just consumed. Every error below is
    // raised before any operand is read, so ip - 1 is the instruction.
    const auto fail = [&](const char* message) {
        const std::size_t offset = static_cast<std::size_t>(ip - base) - 1;
        std::ostringstream formatted;
        formatted << "line " << chunk.line_at(offset) << ": " << message;
        error_ = formatted.str();
    };

    const auto push = [&](const Value& value) noexcept -> bool {
        if (static_cast<std::size_t>(top - stack_base) >= kStackCapacity) {
            return false;
        }
        *top++ = value;
        return true;
    };

#define BINARY_INT_OP(result_expr)                                     \
    do {                                                               \
        const Value b = *--top;                                        \
        const Value a = *--top;                                        \
        if (!a.is_int() || !b.is_int()) {                              \
            fail("arithmetic on a non-integer value");                 \
            return Result::RuntimeError;                               \
        }                                                              \
        *top++ = (result_expr);                                        \
    } while (false)

    VM_LOOP_BEGIN()

    VM_CASE(Constant) : {
        ++instructions_;
        const std::uint16_t index = read_u16();
        if (!push(constants[index])) {
            fail("stack overflow");
            return Result::RuntimeError;
        }
        DISPATCH();
    }

    VM_CASE(True) : {
        ++instructions_;
        if (!push(Value::boolean(true))) {
            fail("stack overflow");
            return Result::RuntimeError;
        }
        DISPATCH();
    }

    VM_CASE(False) : {
        ++instructions_;
        if (!push(Value::boolean(false))) {
            fail("stack overflow");
            return Result::RuntimeError;
        }
        DISPATCH();
    }

    VM_CASE(GetLocal) : {
        ++instructions_;
        const std::uint16_t slot = read_u16();
        // No name lookup, no hash: the compiler already turned this into an
        // index. This is the payoff for DESIGN.md D22.
        if (!push(stack_base[slot])) {
            fail("stack overflow");
            return Result::RuntimeError;
        }
        DISPATCH();
    }

    VM_CASE(SetLocal) : {
        ++instructions_;
        const std::uint16_t slot = read_u16();
        stack_base[slot] = top[-1];  // assignment is an expression: value stays
        DISPATCH();
    }

    VM_CASE(Pop) : {
        ++instructions_;
        --top;
        DISPATCH();
    }

    VM_CASE(Add) : {
        ++instructions_;
        const Value b = *--top;
        const Value a = *--top;
        if (!a.is_int() || !b.is_int()) {
            fail("arithmetic on a non-integer value");
            return Result::RuntimeError;
        }
        std::int64_t result = 0;
        // Signed overflow is undefined behaviour in C++, so it is detected
        // rather than performed. See DESIGN.md D34.
        if (__builtin_add_overflow(a.as_int, b.as_int, &result)) {
            fail("integer overflow in addition");
            return Result::RuntimeError;
        }
        *top++ = Value::integer(result);
        DISPATCH();
    }

    VM_CASE(Sub) : {
        ++instructions_;
        const Value b = *--top;
        const Value a = *--top;
        if (!a.is_int() || !b.is_int()) {
            fail("arithmetic on a non-integer value");
            return Result::RuntimeError;
        }
        std::int64_t result = 0;
        if (__builtin_sub_overflow(a.as_int, b.as_int, &result)) {
            fail("integer overflow in subtraction");
            return Result::RuntimeError;
        }
        *top++ = Value::integer(result);
        DISPATCH();
    }

    VM_CASE(Mul) : {
        ++instructions_;
        const Value b = *--top;
        const Value a = *--top;
        if (!a.is_int() || !b.is_int()) {
            fail("arithmetic on a non-integer value");
            return Result::RuntimeError;
        }
        std::int64_t result = 0;
        if (__builtin_mul_overflow(a.as_int, b.as_int, &result)) {
            fail("integer overflow in multiplication");
            return Result::RuntimeError;
        }
        *top++ = Value::integer(result);
        DISPATCH();
    }

    VM_CASE(Div) : {
        ++instructions_;
        const Value b = *--top;
        const Value a = *--top;
        if (!a.is_int() || !b.is_int()) {
            fail("arithmetic on a non-integer value");
            return Result::RuntimeError;
        }
        if (b.as_int == 0) {
            fail("division by zero");
            return Result::RuntimeError;
        }
        // The one signed-division case that traps on x86: the quotient is not
        // representable, so the hardware raises SIGFPE rather than wrapping.
        if (a.as_int == std::numeric_limits<std::int64_t>::min() && b.as_int == -1) {
            fail("integer overflow in division");
            return Result::RuntimeError;
        }
        *top++ = Value::integer(a.as_int / b.as_int);
        DISPATCH();
    }

    VM_CASE(Negate) : {
        ++instructions_;
        const Value a = *--top;
        if (!a.is_int()) {
            fail("negation of a non-integer value");
            return Result::RuntimeError;
        }
        if (a.as_int == std::numeric_limits<std::int64_t>::min()) {
            fail("integer overflow in negation");
            return Result::RuntimeError;
        }
        *top++ = Value::integer(-a.as_int);
        DISPATCH();
    }

    VM_CASE(Not) : {
        ++instructions_;
        const Value a = *--top;
        if (!a.is_bool()) {
            fail("'!' applied to a non-boolean value");
            return Result::RuntimeError;
        }
        *top++ = Value::boolean(!a.as_bool);
        DISPATCH();
    }

    VM_CASE(Equal) : {
        ++instructions_;
        const Value b = *--top;
        const Value a = *--top;
        *top++ = Value::boolean(a == b);
        DISPATCH();
    }

    VM_CASE(NotEqual) : {
        ++instructions_;
        const Value b = *--top;
        const Value a = *--top;
        *top++ = Value::boolean(a != b);
        DISPATCH();
    }

    VM_CASE(Less) : {
        ++instructions_;
        BINARY_INT_OP(Value::boolean(a.as_int < b.as_int));
        DISPATCH();
    }

    VM_CASE(LessEqual) : {
        ++instructions_;
        BINARY_INT_OP(Value::boolean(a.as_int <= b.as_int));
        DISPATCH();
    }

    VM_CASE(Greater) : {
        ++instructions_;
        BINARY_INT_OP(Value::boolean(a.as_int > b.as_int));
        DISPATCH();
    }

    VM_CASE(GreaterEqual) : {
        ++instructions_;
        BINARY_INT_OP(Value::boolean(a.as_int >= b.as_int));
        DISPATCH();
    }

    VM_CASE(Jump) : {
        ++instructions_;
        const std::uint16_t offset = read_u16();
        ip += offset;
        DISPATCH();
    }

    VM_CASE(JumpIfFalse) : {
        ++instructions_;
        const Value condition = top[-1];
        if (!condition.is_bool()) {
            fail("condition is not a boolean");
            return Result::RuntimeError;
        }
        --top;  // this instruction pops its own condition; see DESIGN.md D24
        const std::uint16_t offset = read_u16();
        if (!condition.as_bool) {
            ip += offset;
        }
        DISPATCH();
    }

    VM_CASE(Loop) : {
        ++instructions_;
        const std::uint16_t offset = read_u16();
        ip -= offset;
        DISPATCH();
    }

    VM_CASE(Print) : {
        ++instructions_;
        out_ << to_string(*--top) << '\n';
        DISPATCH();
    }

    VM_CASE(Input) : {
        ++instructions_;
        std::int64_t value = 0;
        if (!(in_ >> value)) {
            fail("expected an integer on standard input");
            return Result::RuntimeError;
        }
        if (!push(Value::integer(value))) {
            fail("stack overflow");
            return Result::RuntimeError;
        }
        DISPATCH();
    }

    VM_CASE(Halt) : {
        ++instructions_;
        return Result::Ok;
    }

    VM_LOOP_END()

#undef BINARY_INT_OP

#if !CAIRN_COMPUTED_GOTO
    return Result::Ok;  // unreachable: HALT always terminates the switch loop
#endif
}

}  // namespace cairn
