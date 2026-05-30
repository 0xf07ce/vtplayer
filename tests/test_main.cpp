// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test_framework.h"

#include <cstdio>

namespace vttest
{

std::vector<TestCase> & registry()
{
    static std::vector<TestCase> r;
    return r;
}

namespace
{
bool g_currentFailed = false;
int  g_failedCases   = 0;
} // namespace

void reportFailure(char const * file, int line, std::string const & expr)
{
    g_currentFailed = true;
    std::fprintf(stderr, "    FAILED: %s:%d: CHECK(%s)\n", file, line,
                 expr.c_str());
}

} // namespace vttest

int main()
{
    using namespace vttest;

    int passed = 0;
    for (auto const & tc : registry())
    {
        g_currentFailed = false;
        try
        {
            tc.fn();
        }
        catch (std::exception const & e)
        {
            g_currentFailed = true;
            std::fprintf(stderr, "    FAILED: threw exception: %s\n", e.what());
        }
        catch (...)
        {
            g_currentFailed = true;
            std::fprintf(stderr, "    FAILED: threw unknown exception\n");
        }

        if (g_currentFailed)
        {
            ++g_failedCases;
            std::fprintf(stderr, "[FAIL] %s\n", tc.name.c_str());
        }
        else
        {
            ++passed;
            std::fprintf(stdout, "[ ok ] %s\n", tc.name.c_str());
        }
    }

    std::fprintf(stdout, "\n%d passed, %d failed (%zu total)\n", passed,
                 g_failedCases, registry().size());
    return g_failedCases == 0 ? 0 : 1;
}
