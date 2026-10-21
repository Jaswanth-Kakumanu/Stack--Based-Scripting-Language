#pragma once

// Dependency-free test harness.
//
// GoogleTest would pull a build-system dependency into a project whose whole
// point is that it has none. Roughly sixty lines buys registration, reporting
// and a non-zero exit code, which is all CTest needs.

#include <cstddef>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    std::string name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline bool& current_failed() {
    static bool failed = false;
    return failed;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void report_failure(const char* file, int line, const std::string& what) {
    current_failed() = true;
    std::cerr << "  FAIL " << file << ':' << line << ": " << what << '\n';
}

inline int run_all() {
    std::size_t failures = 0;
    for (const TestCase& test : registry()) {
        current_failed() = false;
        std::cout << "[ RUN  ] " << test.name << '\n';
        test.fn();
        if (current_failed()) {
            ++failures;
            std::cout << "[ FAIL ] " << test.name << '\n';
        } else {
            std::cout << "[  OK  ] " << test.name << '\n';
        }
    }
    std::cout << '\n'
              << registry().size() - failures << " passed, " << failures
              << " failed\n";
    return failures == 0 ? 0 : 1;
}

}  // namespace testing

#define TEST(name)                                                     \
    static void name();                                                \
    static ::testing::Registrar registrar_##name(#name, name);         \
    static void name()

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            ::testing::report_failure(__FILE__, __LINE__,              \
                                      "expected: " #cond);             \
        }                                                              \
    } while (false)

#define CHECK_EQ(actual, expected)                                     \
    do {                                                               \
        const auto& a_ = (actual);                                     \
        const auto& e_ = (expected);                                   \
        if (!(a_ == e_)) {                                             \
            std::ostringstream oss_;                                   \
            oss_ << #actual " == " #expected " (got " << a_            \
                 << ", want " << e_ << ")";                            \
            ::testing::report_failure(__FILE__, __LINE__, oss_.str()); \
        }                                                              \
    } while (false)

#define TEST_MAIN() \
    int main() { return ::testing::run_all(); }
