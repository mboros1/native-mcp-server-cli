#include <iostream>
#include <optional>
#include <string>

struct TestParams {
    std::string name;
    int value;
    std::optional<std::string> description;
};

int main() {
    // Test C++20 designated initializers
    TestParams params1{
        .name = "test",
        .value = 42,
        .description = "example"
    };
    
    // Test with optional field omitted
    TestParams params2{
        .name = "minimal",
        .value = 7
        // description is std::nullopt by default
    };
    
    // Test with reordering (C++20 requires order to match declaration)
    TestParams params3{
        .name = "ordered",
        .value = 100,
        .description = std::nullopt
    };
    
    std::cout << "✅ C++20 designated initializers work!" << std::endl;
    std::cout << "Param1: " << params1.name << " = " << params1.value << std::endl;
    std::cout << "Param2: " << params2.name << " = " << params2.value << std::endl;
    
    return 0;
}