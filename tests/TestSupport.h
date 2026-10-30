#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

/** Throws on a failed expectation in all build configurations, unlike a compiled-out assert. */
inline void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

/** Reports test failures as a nonzero process outcome, including unexpected exceptions. */
template <typename Test>
int runTests(Test test)
{
    try
    {
        test();
        std::cout << "All checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
