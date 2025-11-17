#include "test.hpp"
#include <iostream>

namespace vp {
namespace test {

// Define the test registry in a .cpp file to ensure single instance
std::vector<TestCase>& getTestRegistry() {
    static std::vector<TestCase> tests;
    return tests;
}

int& testsPassedCounter() {
    static int count = 0;
    return count;
}

int& testsFailedCounter() {
    static int count = 0;
    return count;
}

void registerTest(const std::string& name, std::function<void()> func) {
    getTestRegistry().push_back({name, func});
}

int runAllTests() {
    auto& tests = getTestRegistry();
    std::cout << "Running " << tests.size() << " tests...\n\n";

    for (const auto& test : tests) {
        std::cout << "[ RUN      ] " << test.name << "\n";
        try {
            test.func();
            std::cout << "[       OK ] " << test.name << "\n";
            testsPassedCounter()++;
        } catch (const std::exception& e) {
            std::cout << "[  FAILED  ] " << test.name << "\n";
            std::cout << "  Error: " << e.what() << "\n";
            testsFailedCounter()++;
        }
    }

    std::cout << "\nTests passed: " << testsPassedCounter() << "\n";
    std::cout << "Tests failed: " << testsFailedCounter() << "\n";

    return testsFailedCounter() > 0 ? 1 : 0;
}

} // namespace test
} // namespace vp
