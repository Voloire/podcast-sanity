// podcast-sanity - micro test framework
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tinytest {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
inline int& checks() {
    static int c = 0;
    return c;
}
inline std::string& current() {
    static std::string s;
    return s;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

inline void report_fail(const char* file, int line, const std::string& msg) {
    failures()++;
    std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str());
}

inline int run_all() {
    int failed_cases = 0;
    for (auto& c : registry()) {
        current() = c.name;
        const int before = failures();
        std::printf("  [ RUN  ] %s\n", c.name.c_str());
        try {
            c.fn();
        } catch (const std::exception& e) {
            report_fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
        }
        if (failures() == before) {
            std::printf("  [  OK  ] %s\n", c.name.c_str());
        } else {
            std::printf("  [ FAIL ] %s\n", c.name.c_str());
            failed_cases++;
        }
    }
    std::printf("\n%s: %d cases, %d checks, %d failed\n",
                failed_cases == 0 ? "PASS" : "FAIL",
                static_cast<int>(registry().size()), checks(), failed_cases);
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace tinytest

#define TT_CONCAT_(a, b) a##b
#define TT_CONCAT(a, b) TT_CONCAT_(a, b)
#define TT_CASE(name)                                                          \
    static void TT_CONCAT(tt_case_, __LINE__)();                               \
    static ::tinytest::Registrar TT_CONCAT(tt_reg_, __LINE__)(name,            \
                                                             TT_CONCAT(tt_case_, __LINE__)); \
    static void TT_CONCAT(tt_case_, __LINE__)()

#define CHECK(cond)                                                            \
    do {                                                                       \
        ::tinytest::checks()++;                                                \
        if (!(cond)) ::tinytest::report_fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                  \
    do {                                                                       \
        ::tinytest::checks()++;                                                \
        const double _a = (a), _b = (b), _t = (tol);                           \
        if (!(std::fabs(_a - _b) <= _t)) {                                      \
            char _buf[256];                                                     \
            std::snprintf(_buf, sizeof(_buf), "CHECK_NEAR(%s=%g, %s=%g, tol %g)", \
                          #a, _a, #b, _b, _t);                                 \
            ::tinytest::report_fail(__FILE__, __LINE__, _buf);                  \
        }                                                                      \
    } while (0)

#define CHECK_LE(a, b)                                                         \
    do {                                                                       \
        ::tinytest::checks()++;                                                \
        if (!((a) <= (b))) {                                                    \
            char _buf[256];                                                     \
            std::snprintf(_buf, sizeof(_buf), "CHECK_LE(%s=%g, %s=%g)", #a,     \
                          static_cast<double>(a), #b, static_cast<double>(b));  \
            ::tinytest::report_fail(__FILE__, __LINE__, _buf);                  \
        }                                                                      \
    } while (0)

#define CHECK_GE(a, b)                                                         \
    do {                                                                       \
        ::tinytest::checks()++;                                                \
        if (!((a) >= (b))) {                                                    \
            char _buf[256];                                                     \
            std::snprintf(_buf, sizeof(_buf), "CHECK_GE(%s=%g, %s=%g)", #a,     \
                          static_cast<double>(a), #b, static_cast<double>(b));  \
            ::tinytest::report_fail(__FILE__, __LINE__, _buf);                  \
        }                                                                      \
    } while (0)
