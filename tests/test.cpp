#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <print>
#include <sstream>
#include <thread>

class RefactorToolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for test files
        test_dir = "test_temp";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        // Clean up temp directory
        std::filesystem::remove_all(test_dir);
    }

    std::string runRefactorTool(const std::string &input_code, const std::string &test_name) {
        // Write input code to temporary file
        std::string input_file = test_dir + "/" + test_name + ".cpp";
        std::ofstream out(input_file);
        out << input_code;
        out.close();

        // Run refactoring tool (assuming it's compiled as 'refactor_tool')
        std::string command = "./refactor_tool " + input_file + " > /dev/null 2>&1";
        std::system(command.c_str());

        // Read the modified file
        std::ifstream in(input_file);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    std::string test_dir;
};

TEST_F(RefactorToolTest, NonVirtualDestructor) {
    std::string input_code = R"(
        class Base {
        public:
            Base() = default;
            ~Base() {}  // Should become virtual
        };

        class Derived : public Base {
        public:
            Derived() = default;
            ~Derived() override {}
        };
    )";

    std::string output = runRefactorTool(input_code, "test_nv_dtor");

    // Check if virtual was added before destructor
    EXPECT_NE(output.find("virtual ~Base()"), std::string::npos);

    // Derived destructor should not be modified
    EXPECT_EQ(output.find("virtual ~Derived()"), std::string::npos);
}

TEST_F(RefactorToolTest, MissingOverrideKeyword) {
    std::string input_code = R"(
        class Base {
        public:
            virtual void method() {}
        };

        class Derived : public Base {
        public:
            void method() {}  // Should get override keyword
        };
    )";

    std::string output = runRefactorTool(input_code, "test_override");

    // Check if override was added
    EXPECT_NE(output.find("void method() override"), std::string::npos);
}

TEST_F(RefactorToolTest, ConstLoopVariableWithoutRef) {
    std::string input_code = R"(
        #include <vector>
        #include <string>

        void testFunction() {
            std::vector<std::string> strings = {"hello", "world"};

            for (const auto str : strings) {  // Should become const auto&
                // do something
            }

            for (const int num : {1, 2, 3}) {  // Should NOT be modified (fundamental type)
                // do something
            }
        }
    )";

    std::string output = runRefactorTool(input_code, "test_range_for");

    // Check if & was added for non-fundamental types
    EXPECT_NE(output.find("const auto &str"), std::string::npos);

    // Check that fundamental types are not modified
    EXPECT_NE(output.find("const int num"), std::string::npos);
}

TEST_F(RefactorToolTest, MultipleRefactoringCases) {
    std::string input_code = R"(
        #include <vector>
        #include <string>

        class Base {
        public:
            virtual ~Base() {}  // Already virtual, should not change
            virtual void existing() {}
        };

        class Derived : public Base {
        public:
            void existing() {}  // Should get override
        };

        void testLoops() {
            std::vector<std::string> items;
            for (const auto item : items) {  // Should become const auto&
            }
        }
    )";

    std::string output = runRefactorTool(input_code, "test_multiple");

    // Check all expected changes
    EXPECT_NE(output.find("virtual ~Base()"), std::string::npos);
    EXPECT_NE(output.find("void existing() override"), std::string::npos);
    EXPECT_NE(output.find("const auto &item"), std::string::npos);

    // Check that already correct code is not modified
    EXPECT_NE(output.find("virtual ~Base()"), std::string::npos);
    EXPECT_EQ(output.find("virtual virtual ~Base()"), std::string::npos);  // No duplicate virtual
}

TEST_F(RefactorToolTest, NoChangesNeeded) {
    std::string input_code = R"(
        #include <vector>

        class Base {
        public:
            virtual ~Base() = default;
            virtual void method() = 0;
        };

        class Derived : public Base {
        public:
            ~Derived() override = default;
            void method() override {}
        };

        void testLoops() {
            std::vector<int> numbers;
            for (const auto& num : numbers) {}  // Already has reference
            for (int num : numbers) {}          // Non-const, should not change
        }
    )";

    std::string original = input_code;
    std::string output = runRefactorTool(input_code, "test_no_changes");

    EXPECT_NE(output.find("virtual ~Base()"), std::string::npos);
    EXPECT_NE(output.find("void method() override"), std::string::npos);
    EXPECT_NE(output.find("const auto& num"), std::string::npos);
    EXPECT_NE(output.find("int num"), std::string::npos);
}