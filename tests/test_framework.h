// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

// Minimal zero-dependency unit-test harness.
//
// Vendored in-tree (rather than fetching doctest/Catch2) to keep the offline
// build policy intact and avoid pulling a multi-thousand-line header for the
// handful of pure-logic tests vtplayer needs. Usage:
//
//     #include "test_framework.h"
//     TEST_CASE("description") { CHECK(expr); CHECK_EQ(a, b); }
//
// The runner main() lives in test_main.cpp; link every test TU against it.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace vttest
{

struct TestCase
{
    std::string          name;
    std::function<void()> fn;
};

/// Global registry of all TEST_CASE-declared cases (populated at static-init).
std::vector<TestCase> & registry();

/// Records a failed check for the currently-running case. Implemented in
/// test_main.cpp; prints file:line and the failing expression.
void reportFailure(char const * file, int line, std::string const & expr);

/// Self-registering helper; one static instance per TEST_CASE.
struct Registrar
{
    Registrar(std::string name, std::function<void()> fn)
    {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

} // namespace vttest

#define VTTEST_CONCAT_(a, b) a##b
#define VTTEST_CONCAT(a, b) VTTEST_CONCAT_(a, b)

#define TEST_CASE(name)                                                        \
    static void VTTEST_CONCAT(vttest_fn_, __LINE__)();                          \
    static ::vttest::Registrar VTTEST_CONCAT(vttest_reg_, __LINE__)(            \
        name, &VTTEST_CONCAT(vttest_fn_, __LINE__));                            \
    static void VTTEST_CONCAT(vttest_fn_, __LINE__)()

#define CHECK(expr)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(expr))                                                           \
            ::vttest::reportFailure(__FILE__, __LINE__, #expr);                \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do                                                                         \
    {                                                                          \
        if (!((a) == (b)))                                                     \
            ::vttest::reportFailure(__FILE__, __LINE__,                        \
                                    #a " == " #b);                             \
    } while (0)
