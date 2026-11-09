#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// LQ-63: the simulation sources keep to the deterministic float rules (Principles.md, Deterministic simulation). No
// libm transcendental (rule 2): their last bits differ between C libraries. No approximate or fused SIMD and no glm
// SIMD (rule 3). Use <engine/core/det_math.h> and engine::angle_axis instead.

namespace {

// Calls of these, unqualified or as std:: or glm::, are refused.
constexpr std::array<std::string_view, 34> kLibm{
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "asinh", "acosh",
        "atanh", "exp", "exp2", "expm1", "log", "log2", "log10", "log1p", "pow", "cbrt", "hypot", "erf",
        "sinf", "cosf", "tanf", "atan2f", "expf", "logf", "powf", "fma", "fmaf", "sincos"};
// glm functions built on libm. glm::radians and glm::degrees are a multiply and stay allowed.
constexpr std::array<std::string_view, 12> kGlm{"angleAxis", "rotate", "eulerAngles", "yaw", "pitch", "roll", "slerp",
        "angle", "axis", "orientedAngle", "perspective", "quatLookAt"};
// Refused anywhere in the text.
constexpr std::array<std::string_view, 10> kTokens{"_mm_rsqrt", "_mm_rcp", "_mm256_rsqrt", "_mm256_rcp", "fmadd",
        "fmsub", "vfma", "vrsqrte", "vrecpe", "GLM_FORCE_"};

// Comments and string literals blanked, so prose may name what code may not call.
std::string strip(std::string_view text) {
    std::string out(text);
    std::size_t i = 0;
    while (i < out.size()) {
        if (out.compare(i, 2, "//") == 0) {
            while (i < out.size() && out[i] != '\n') {
                out[i++] = ' ';
            }
        } else if (out.compare(i, 2, "/*") == 0) {
            while (i < out.size() && out.compare(i, 2, "*/") != 0) {
                if (out[i] != '\n') {
                    out[i] = ' ';
                }
                ++i;
            }
            for (int k = 0; k < 2 && i < out.size(); ++k) {
                out[i++] = ' ';
            }
        } else if (out[i] == '"' || out[i] == '\'') {
            const char quote = out[i++];
            while (i < out.size() && out[i] != quote && out[i] != '\n') {
                if (out[i] == '\\') {
                    out[i++] = ' ';
                }
                if (i < out.size()) {
                    out[i++] = ' ';
                }
            }
            ++i;
        } else {
            ++i;
        }
    }
    return out;
}

bool word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// "name(" calls of a refused function, with the line number.
std::vector<std::string> violations(std::string_view source) {
    const std::string text = strip(source);
    std::vector<std::string> found;
    const auto line_of = [&](std::size_t at) { return 1 + std::count(text.begin(), text.begin() + static_cast<long>(at), '\n'); };
    for (const std::string_view token : kTokens) {
        for (std::size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + 1)) {
            found.push_back(std::string(token) + " at line " + std::to_string(line_of(at)));
        }
    }
    std::size_t i = 0;
    while (i < text.size()) {
        if (!word_char(text[i]) || (i > 0 && word_char(text[i - 1]))) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < text.size() && word_char(text[i])) {
            ++i;
        }
        const std::string_view name(text.data() + start, i - start);
        std::size_t next = i;
        while (next < text.size() && (text[next] == ' ' || text[next] == '\t')) {
            ++next;
        }
        if (next >= text.size() || text[next] != '(') {
            continue;
        }
        // The qualifier: "std::", "glm::", none, or a member access (allowed: a field such as SinCos::sin).
        std::string_view qualifier;
        if (start >= 2 && text[start - 1] == ':' && text[start - 2] == ':') {
            std::size_t q = start - 2;
            while (q > 0 && word_char(text[q - 1])) {
                --q;
            }
            qualifier = std::string_view(text.data() + q, start - 2 - q);
        } else if (start >= 1 && text[start - 1] == '.') {
            continue;
        } else if (start >= 2 && text[start - 1] == '>' && text[start - 2] == '-') {
            continue;
        }
        const bool libm = std::find(kLibm.begin(), kLibm.end(), name) != kLibm.end() &&
                          (qualifier.empty() || qualifier == "std" || qualifier == "glm");
        const bool glm = qualifier == "glm" && std::find(kGlm.begin(), kGlm.end(), name) != kGlm.end();
        if (libm || glm) {
            found.push_back((qualifier.empty() ? std::string{} : std::string(qualifier) + "::") + std::string(name) +
                            " at line " + std::to_string(line_of(start)));
        }
    }
    return found;
}

// Simulation sources: everything the engine runs on Schedule::Fixed lives in src/ecs and include/engine/ecs. Files
// that only serve rendering are left out by name.
constexpr std::array<std::string_view, 1> kNotSimulation{"camera.cpp"};

}

TEST(SimulationMathCheck, ScannerFindsRefusedCalls) {
    const std::string sample = "float a = std::sin(x);\n"
                               "// std::cos(x) in a comment is fine\n"
                               "const char* s = \"atan2(y, x)\";\n"
                               "float b = rotation.sin + det_atan2(y, x) + glm::radians(1.f);\n"
                               "glm::quat q = glm::angleAxis(a, axis);\n"
                               "float c = pow (a, 2.f);\n"
                               "__m128 r = _mm_rsqrt_ps(v);\n";
    const std::vector<std::string> found = violations(sample);
    ASSERT_EQ(found.size(), 4u) << ::testing::PrintToString(found);
    EXPECT_EQ(found[0], "_mm_rsqrt at line 7");
    EXPECT_EQ(found[1], "std::sin at line 1");
    EXPECT_EQ(found[2], "glm::angleAxis at line 5");
    EXPECT_EQ(found[3], "pow at line 6");
}

TEST(SimulationMathCheck, SimulationSourcesUseDeterministicMath) {
    const std::filesystem::path root{ENGINE_SOURCE_DIR};
    if (!std::filesystem::exists(root / "src" / "ecs")) {
        GTEST_SKIP() << "engine sources are not readable here (" << root.string() << ")";
    }
    std::size_t scanned = 0;
    for (const std::filesystem::path dir : {root / "src" / "ecs", root / "include" / "engine" / "ecs"}) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            const std::string extension = entry.path().extension().string();
            if (extension != ".cpp" && extension != ".h" && extension != ".inl") {
                continue;
            }
            const std::string file = entry.path().filename().string();
            if (std::find(kNotSimulation.begin(), kNotSimulation.end(), file) != kNotSimulation.end()) {
                continue;
            }
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream content;
            content << in.rdbuf();
            for (const std::string& violation : violations(content.str())) {
                ADD_FAILURE() << entry.path().string() << ": " << violation
                              << " (deterministic simulation: use <engine/core/det_math.h>)";
            }
            ++scanned;
        }
    }
    EXPECT_GT(scanned, 40u);
}
