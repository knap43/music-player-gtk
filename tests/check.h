#pragma once

// A minimal test harness: CHECK / CHECK_EQ record failures, run() reports.
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace check {

inline int &failures()
{
    static int n = 0;
    return n;
}

inline std::vector<std::pair<std::string, std::function<void()>>> &tests()
{
    static std::vector<std::pair<std::string, std::function<void()>>> list;
    return list;
}

struct Register {
    Register(const char *name, std::function<void()> fn) { tests().emplace_back(name, std::move(fn)); }
};

inline int run()
{
    for (auto &[name, fn] : tests()) {
        const int before = failures();
        fn();
        std::cout << (failures() == before ? "PASS " : "FAIL ") << name << "\n";
    }
    std::cout << (failures() ? "FAILED" : "OK") << " (" << tests().size() << " tests)\n";
    return failures() ? 1 : 0;
}

template <typename A, typename B>
void equal(const A &a, const B &b, const char *ea, const char *eb, const char *file, int line)
{
    if (!(a == b)) {
        std::ostringstream s;
        s << file << ":" << line << ": " << ea << " == " << eb << "\n    actual:   " << a
          << "\n    expected: " << b;
        std::cout << s.str() << "\n";
        ++failures();
    }
}

} // namespace check

#define TEST(name)                                                                                 \
    static void name();                                                                            \
    static check::Register reg_##name(#name, name);                                                \
    static void name()

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cout << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n";             \
            ++check::failures();                                                                   \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b) check::equal((a), (b), #a, #b, __FILE__, __LINE__)
