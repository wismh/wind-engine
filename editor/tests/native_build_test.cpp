#include <gtest/gtest.h>

#include "native_build.h"

#include <string>

namespace {

bool has_variable(const std::vector<engine::ProcessVariable>& variables, const std::string& name,
        const std::string& value) {
    for (const engine::ProcessVariable& variable : variables) {
        if (variable.name == name && variable.value == value) {
            return true;
        }
    }
    return false;
}

}

TEST(NativeBuild, ParseEnvironmentDumpReadsNameAndValue) {
    const std::vector<engine::ProcessVariable> variables =
            editor::parse_environment_dump("INCLUDE=C:\\inc\r\nLIB=C:\\lib\\a=b\r\n\r\n=C:=C:\\work\r\nPATH=C:\\bin\n");
    EXPECT_TRUE(has_variable(variables, "INCLUDE", "C:\\inc"));
    EXPECT_TRUE(has_variable(variables, "LIB", "C:\\lib\\a=b"));
    EXPECT_TRUE(has_variable(variables, "PATH", "C:\\bin"));
    EXPECT_EQ(variables.size(), 3u);
}

#if defined(_WIN32)

TEST(NativeBuild, MsvcEnvironmentWhenTheCompilerCanBeFound) {
    const editor::NativeBuild& native = editor::native_build();
    ASSERT_TRUE(native.problem.empty()) << native.problem;
    for (const engine::ProcessVariable& variable : native.environment) {
        EXPECT_FALSE(variable.name.empty());
        EXPECT_NE(variable.name.front(), '=');
    }
}

#endif
