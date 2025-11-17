#ifndef VP_TEST_HPP
#define VP_TEST_HPP

#include <iostream>
#include <string>
#include <functional>
#include <vector>

namespace vp {
namespace test {

struct TestResult {
    std::string name;
    bool passed;
    std::string error;
};

class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner runner;
        return runner;
    }

    void registerTest(const std::string& name, std::function<void()> test) {
        tests_.push_back({name, test});
    }

    int run() {
        int failed = 0;
        int passed = 0;

        std::cout << "Running " << tests_.size() << " tests...\n\n";

        for (const auto& t : tests_) {
            std::cout << "[ RUN      ] " << t.name << "\n";
            try {
                t.test();
                std::cout << "[       OK ] " << t.name << "\n";
                passed++;
            } catch (const std::exception& e) {
                std::cout << "[  FAILED  ] " << t.name << "\n";
                std::cout << "  Error: " << e.what() << "\n";
                failed++;
            }
        }

        std::cout << "\n";
        std::cout << "Tests passed: " << passed << "\n";
        std::cout << "Tests failed: " << failed << "\n";

        return failed;
    }

private:
    struct Test {
        std::string name;
        std::function<void()> test;
    };

    std::vector<Test> tests_;
};

class TestCase {
public:
    TestCase(const std::string& name, std::function<void()> test) {
        TestRunner::instance().registerTest(name, test);
    }
};

inline void assertTrue(bool condition, const std::string& message = "") {
    if (!condition) {
        throw std::runtime_error("Assertion failed: " + message);
    }
}

inline void assertEqual(int expected, int actual, const std::string& message = "") {
    if (expected != actual) {
        throw std::runtime_error("Expected " + std::to_string(expected) +
                               ", got " + std::to_string(actual) + ". " + message);
    }
}

inline void assertEqual(const std::string& expected, const std::string& actual,
                       const std::string& message = "") {
    if (expected != actual) {
        throw std::runtime_error("Expected '" + expected + "', got '" + actual + "'. " + message);
    }
}

#define TEST(name) \
    static void test_##name(); \
    static vp::test::TestCase testcase_##name(#name, test_##name); \
    static void test_##name()

} // namespace test
} // namespace vp

#endif // VP_TEST_HPP
