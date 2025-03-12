#include <gtest/gtest.h>

#include "ui/math/math_font.h"
#include "ui/math/math_parser.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

namespace {

using namespace engine::ui::math;

// ---- S-expression dump -----------------------------------------------------------------------
// Rows print as `[a b]`, so a whole AST compares as one readable string. Latin italics print as the
// plain letter, other ASCII as itself, everything else as `U+XXXX`; a non-Ord class is a `:suffix`.

std::string hex(char32_t c) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out = "U+";
    bool started = false;
    for (int shift = 20; shift >= 0; shift -= 4) {
        const unsigned nibble = (static_cast<unsigned>(c) >> shift) & 0xF;
        if (nibble != 0 || started || shift <= 12) {
            out += kDigits[nibble];
            started = true;
        }
    }
    return out;
}

std::string glyph(char32_t c) {
    if (c >= 0x1D44E && c <= 0x1D467) {
        return std::string(1, static_cast<char>('a' + (c - 0x1D44E)));
    }
    if (c == 0x210E) {
        return "h";
    }
    if (c >= 0x1D434 && c <= 0x1D44D) {
        return std::string(1, static_cast<char>('A' + (c - 0x1D434)));
    }
    if (c > 0x20 && c < 0x7F) {
        return std::string(1, static_cast<char>(c));
    }
    return hex(c);
}

std::string ascii(const std::u32string& text) {
    std::string out;
    for (const char32_t c : text) {
        out += (c >= 0x20 && c < 0x7F) ? std::string(1, static_cast<char>(c)) : hex(c);
    }
    return out;
}

std::string dump(const Row& row);

std::string dump(const Node& node) {
    struct Visitor {
        std::string operator()(const Symbol& s) const {
            std::string out = glyph(s.codepoint);
            switch (s.math_class) {
                case MathClass::Ord:
                    break;
                case MathClass::Op:
                    out += s.limits_in_display ? ":op+lim" : ":op";
                    break;
                case MathClass::Bin:
                    out += ":bin";
                    break;
                case MathClass::Rel:
                    out += ":rel";
                    break;
                case MathClass::Open:
                    out += ":open";
                    break;
                case MathClass::Close:
                    out += ":close";
                    break;
                case MathClass::Punct:
                    out += ":punct";
                    break;
            }
            return out;
        }
        std::string operator()(const Text& t) const {
            return "\"" + ascii(t.text) + "\"";
        }
        std::string operator()(const OperatorName& f) const {
            return "fn:" + ascii(f.name) + (f.limits_in_display ? "+lim" : "");
        }
        std::string operator()(const Space& s) const {
            return "sp(" + std::to_string(static_cast<int>(std::lround(s.em * 18.0f))) + ")";
        }
        std::string operator()(const Group& g) const {
            std::string out = "{";
            for (std::size_t i = 0; i < g.items.size(); ++i) {
                out += (i == 0 ? "" : " ") + dump(g.items[i]);
            }
            return out + "}";
        }
        std::string operator()(const Fraction& f) const {
            return "(frac " + dump(f.numerator) + " " + dump(f.denominator) + ")";
        }
        std::string operator()(const Radical& r) const {
            return "(sqrt " + dump(r.radicand) + (r.index ? " index=" + dump(*r.index) : "") + ")";
        }
        std::string operator()(const Accent& a) const {
            return "(accent " + hex(a.mark) + " " + dump(a.base) + ")";
        }
        std::string operator()(const Scripts& s) const {
            std::string out = "(scripts " + dump(s.base);
            if (s.subscript) {
                out += " sub=" + dump(*s.subscript);
            }
            if (s.superscript) {
                out += " sup=" + dump(*s.superscript);
            }
            if (s.limits == LimitsMode::Limits) {
                out += " limits";
            } else if (s.limits == LimitsMode::NoLimits) {
                out += " nolimits";
            }
            return out + ")";
        }
        std::string operator()(const Delimited& d) const {
            const auto delimiter = [](char32_t c) { return c == 0 ? std::string(".") : glyph(c); };
            return "(delim " + delimiter(d.open) + " " + delimiter(d.close) + " " + dump(d.body) + ")";
        }
    };
    return std::visit(Visitor{}, node.value);
}

std::string dump(const Row& row) {
    std::string out = "[";
    for (std::size_t i = 0; i < row.size(); ++i) {
        out += (i == 0 ? "" : " ") + dump(row[i]);
    }
    return out + "]";
}

std::string parsed(std::string_view source) {
    const ParseResult result = parse_formula(source);
    EXPECT_TRUE(result.ok()) << source << ": " << (result.errors.empty() ? "" : describe(result.errors.front()));
    return dump(result.root);
}

// ---- fixtures --------------------------------------------------------------------------------

const MathFont& stix() {
    static const MathFont font = [] {
        const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "fonts" / "math.otf";
        std::ifstream in(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto loaded = MathFont::load(bytes);
        EXPECT_TRUE(loaded.has_value());
        return std::move(*loaded);
    }();
    return font;
}

}

TEST(MathParser, EmptyAndWhitespaceOnlyGiveEmptyRow) {
    EXPECT_EQ(parsed(""), "[]");
    EXPECT_EQ(parsed("  \t\n "), "[]");
}

TEST(MathParser, ClassifiesBasicCharacters) {
    EXPECT_EQ(parsed("x"), "[x]");
    EXPECT_EQ(parsed("a  b"), "[a b]");
    EXPECT_EQ(parsed("x+y"), "[x +:bin y]");
    EXPECT_EQ(parsed("a=b"), "[a =:rel b]");
    EXPECT_EQ(parsed("a<b"), "[a <:rel b]");
    EXPECT_EQ(parsed("(a,b)"), "[(:open a ,:punct b ):close]");
    EXPECT_EQ(parsed("[0;1]"), "[[:open 0 ;:punct 1 ]:close]");
    EXPECT_EQ(parsed("12.5"), "[1 2 . 5]");
    EXPECT_EQ(parsed("n!"), "[n !]");
    // ASCII minus becomes the real minus sign; `*` the asterisk operator.
    EXPECT_EQ(parsed("a-b"), "[a U+2212:bin b]");
    EXPECT_EQ(parsed("a*b"), "[a U+2217:bin b]");
}

TEST(MathParser, MapsLatinLettersToMathItalicExceptH) {
    const ParseResult result = parse_formula("aAzZh");
    ASSERT_EQ(result.root.size(), 5u);
    const auto code = [&](std::size_t i) { return std::get<Symbol>(result.root[i].value).codepoint; };
    EXPECT_EQ(code(0), char32_t{0x1D44E});
    EXPECT_EQ(code(1), char32_t{0x1D434});
    EXPECT_EQ(code(2), char32_t{0x1D467});
    EXPECT_EQ(code(3), char32_t{0x1D44D});
    EXPECT_EQ(code(4), char32_t{0x210E});  // the hole in the italic block: Planck's constant
}

TEST(MathParser, KeepsRawUnicodeAsOrdinarySymbols) {
    const ParseResult result = parse_formula("\xCE\xB1+\xCE\xB2");  // alpha + beta
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.root.size(), 3u);
    EXPECT_EQ(std::get<Symbol>(result.root[0].value).codepoint, char32_t{0x03B1});
    EXPECT_EQ(std::get<Symbol>(result.root[0].value).math_class, MathClass::Ord);
    EXPECT_EQ(std::get<Symbol>(result.root[2].value).codepoint, char32_t{0x03B2});
}

TEST(MathParser, ParsesGroups) {
    EXPECT_EQ(parsed("{ab}c"), "[{a b} c]");
    EXPECT_EQ(parsed("{}"), "[{}]");
}

TEST(MathParser, ParsesSuperAndSubscripts) {
    EXPECT_EQ(parsed("x^2"), "[(scripts [x] sup=[2])]");
    EXPECT_EQ(parsed("x_i"), "[(scripts [x] sub=[i])]");
    EXPECT_EQ(parsed("x_i^2"), "[(scripts [x] sub=[i] sup=[2])]");
    EXPECT_EQ(parsed("x^2_i"), "[(scripts [x] sub=[i] sup=[2])]");
    EXPECT_EQ(parsed("x ^ 2"), "[(scripts [x] sup=[2])]");
    EXPECT_EQ(parsed("x^{ab}"), "[(scripts [x] sup=[a b])]");
    EXPECT_EQ(parsed("e^{x^2}"), "[(scripts [e] sup=[(scripts [x] sup=[2])])]");
    // A script on a group scripts the whole group; a bare `^2` scripts an empty base.
    EXPECT_EQ(parsed("{ab}^2"), "[(scripts [{a b}] sup=[2])]");
    EXPECT_EQ(parsed("^2"), "[(scripts [{}] sup=[2])]");
    // A single-token script argument is one token, not the rest of the row.
    EXPECT_EQ(parsed("x^2y"), "[(scripts [x] sup=[2]) y]");
}

TEST(MathParser, TreatsApostrophesAsPrimeSuperscripts) {
    EXPECT_EQ(parsed("f'"), "[(scripts [f] sup=[U+2032])]");
    EXPECT_EQ(parsed("f''"), "[(scripts [f] sup=[U+2032 U+2032])]");
    EXPECT_EQ(parsed("f'_i"), "[(scripts [f] sub=[i] sup=[U+2032])]");
}

TEST(MathParser, ParsesFractions) {
    EXPECT_EQ(parsed("\\frac{a}{b}"), "[(frac [a] [b])]");
    EXPECT_EQ(parsed("\\frac12"), "[(frac [1] [2])]");
    EXPECT_EQ(parsed("\\frac{a+b}{c}"), "[(frac [a +:bin b] [c])]");
    EXPECT_EQ(parsed("\\frac{1}{\\frac{a}{b}}"), "[(frac [1] [(frac [a] [b])])]");
    EXPECT_EQ(parsed("\\frac a b"), "[(frac [a] [b])]");
}

TEST(MathParser, ParsesRadicals) {
    EXPECT_EQ(parsed("\\sqrt{x}"), "[(sqrt [x])]");
    EXPECT_EQ(parsed("\\sqrt x"), "[(sqrt [x])]");
    EXPECT_EQ(parsed("\\sqrt[3]{x}"), "[(sqrt [x] index=[3])]");
    EXPECT_EQ(parsed("\\sqrt{\\sqrt{x}}"), "[(sqrt [(sqrt [x])])]");
    EXPECT_EQ(parsed("\\sqrt [n] {x+1}"), "[(sqrt [x +:bin 1] index=[n])]");
}

TEST(MathParser, ParsesLargeOperatorsWithLimits) {
    EXPECT_EQ(parsed("\\sum"), "[U+2211:op+lim]");
    EXPECT_EQ(parsed("\\sum_{i=0}^{n} i"),
            "[(scripts [U+2211:op+lim] sub=[i =:rel 0] sup=[n]) i]");
    // Integrals set their limits beside the sign by default.
    EXPECT_EQ(parsed("\\int_0^1"), "[(scripts [U+222B:op] sub=[0] sup=[1])]");
    EXPECT_EQ(parsed("\\sum\\nolimits_i"), "[(scripts [U+2211:op+lim] sub=[i] nolimits)]");
    EXPECT_EQ(parsed("\\int\\limits_a^b"), "[(scripts [U+222B:op] sub=[a] sup=[b] limits)]");
    EXPECT_EQ(parsed("\\prod_{k}"), "[(scripts [U+220F:op+lim] sub=[k])]");
}

TEST(MathParser, ParsesVectorAccents) {
    EXPECT_EQ(parsed(R"(\vec E)"), "[(accent U+20D7 [E])]");
    EXPECT_EQ(parsed(R"(\vec{F})"), "[(accent U+20D7 [F])]");
    EXPECT_EQ(parsed(R"(\vec{AB})"), "[(accent U+20D7 [A B])]");
    EXPECT_EQ(parsed(R"(\vec{x+y})"), "[(accent U+20D7 [x +:bin y])]");
    // Like TeX the argument is one token, so what follows is not swallowed.
    EXPECT_EQ(parsed(R"(\vec ab)"), "[(accent U+20D7 [a]) b]");
    EXPECT_EQ(parsed(R"(m\vec a)"), "[m (accent U+20D7 [a])]");
    EXPECT_EQ(parsed(R"(\vec\alpha)"), "[(accent U+20D7 [U+1D6FC])]");
    // Scripts attach to the whole accented symbol, and an accent nests.
    EXPECT_EQ(parsed(R"(\vec x_1)"), "[(scripts [(accent U+20D7 [x])] sub=[1])]");
    EXPECT_EQ(parsed(R"(\vec{F}^2)"), "[(scripts [(accent U+20D7 [F])] sup=[2])]");
    EXPECT_EQ(parsed(R"(\vec{\vec{x}})"), "[(accent U+20D7 [(accent U+20D7 [x])])]");
    EXPECT_EQ(parsed(R"(\frac{\vec F}{q})"), "[(frac [(accent U+20D7 [F])] [q])]");
}

TEST(MathParser, ParsesNamedFunctions) {
    EXPECT_EQ(parsed("\\sin x"), "[fn:sin x]");
    EXPECT_EQ(parsed("\\log_2 n"), "[(scripts [fn:log] sub=[2]) n]");
    EXPECT_EQ(parsed("\\lim_{x\\to0}"), "[(scripts [fn:lim+lim] sub=[x U+2192:rel 0])]");
    EXPECT_EQ(parsed("\\max\\limits_i"), "[(scripts [fn:max+lim] sub=[i] limits)]");
    EXPECT_EQ(parsed("\\operatorname{foo} x"), "[fn:foo x]");
}

TEST(MathParser, ParsesLeftRightDelimiters) {
    EXPECT_EQ(parsed("\\left( x \\right)"), "[(delim ( ) [x])]");
    EXPECT_EQ(parsed("\\left[ a,b \\right]"), "[(delim [ ] [a ,:punct b])]");
    EXPECT_EQ(parsed("\\left. x \\right|"), "[(delim . | [x])]");
    EXPECT_EQ(parsed("\\left\\{ x \\right\\}"), "[(delim { } [x])]");
    EXPECT_EQ(parsed("\\left\\langle x \\right\\rangle"), "[(delim U+27E8 U+27E9 [x])]");
    EXPECT_EQ(parsed("\\left< x \\right>"), "[(delim U+27E8 U+27E9 [x])]");
    EXPECT_EQ(parsed("\\left\\| x \\right\\|"), "[(delim U+2016 U+2016 [x])]");
    EXPECT_EQ(parsed("\\left\\lfloor x \\right\\rfloor"), "[(delim U+230A U+230B [x])]");
    EXPECT_EQ(parsed("\\left( \\left[ x \\right] \\right)"), "[(delim ( ) [(delim [ ] [x])])]");
    // Scripts attach to the whole delimited group.
    EXPECT_EQ(parsed("\\left( x \\right)^2"), "[(scripts [(delim ( ) [x])] sup=[2])]");
}

TEST(MathParser, MapsGreekLetters) {
    EXPECT_EQ(parsed("\\alpha"), "[U+1D6FC]");
    EXPECT_EQ(parsed("\\omega"), "[U+1D714]");
    EXPECT_EQ(parsed("\\pi"), "[U+1D70B]");
    EXPECT_EQ(parsed("\\epsilon"), "[U+1D716]");
    EXPECT_EQ(parsed("\\varepsilon"), "[U+1D700]");
    EXPECT_EQ(parsed("\\partial"), "[U+1D715]");
    // Capital Greek stays upright.
    EXPECT_EQ(parsed("\\Gamma"), "[U+0393]");
    EXPECT_EQ(parsed("\\Omega"), "[U+03A9]");
}

TEST(MathParser, ParsesSymbolsByClass) {
    EXPECT_EQ(parsed("a\\cdot b"), "[a U+22C5:bin b]");
    EXPECT_EQ(parsed("a\\times b"), "[a U+00D7:bin b]");
    EXPECT_EQ(parsed("a\\leq b"), "[a U+2264:rel b]");
    EXPECT_EQ(parsed("a\\le b"), "[a U+2264:rel b]");
    EXPECT_EQ(parsed("x\\to y"), "[x U+2192:rel y]");
    EXPECT_EQ(parsed("\\infty"), "[U+221E]");
    EXPECT_EQ(parsed("\\{x\\}"), "[{:open x }:close]");
    EXPECT_EQ(parsed("50\\%"), "[5 0 %]");
}

TEST(MathParser, ParsesSpacing) {
    EXPECT_EQ(parsed("a\\,b"), "[a sp(3) b]");
    EXPECT_EQ(parsed("a\\:b"), "[a sp(4) b]");
    EXPECT_EQ(parsed("a\\;b"), "[a sp(5) b]");
    EXPECT_EQ(parsed("a\\!b"), "[a sp(-3) b]");
    EXPECT_EQ(parsed("a\\quad b"), "[a sp(18) b]");
    EXPECT_EQ(parsed("a\\qquad b"), "[a sp(36) b]");
}

TEST(MathParser, ParsesTextGroups) {
    EXPECT_EQ(parsed("\\text{if } x"), "[\"if \" x]");
    EXPECT_EQ(parsed("\\text{ and }"), "[\" and \"]");
    EXPECT_EQ(parsed("\\text{a    b}"), "[\"a b\"]");
    EXPECT_EQ(parsed("\\text{50\\% off}"), "[\"50% off\"]");
    EXPECT_EQ(parsed("\\text{a {b} c}"), "[\"a b c\"]");
    // `\mathrm` drops whitespace: it is for upright symbols like the differential d.
    EXPECT_EQ(parsed("\\mathrm{d} x"), "[\"d\" x]");
    EXPECT_EQ(parsed("\\mathrm{a b}"), "[\"ab\"]");
}

TEST(MathParser, ParsesARealisticFormula) {
    EXPECT_EQ(parsed("x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}"),
            "[x =:rel (frac [U+2212:bin b U+00B1:bin (sqrt [(scripts [b] sup=[2]) U+2212:bin 4 a c])] [2 a])]");
    EXPECT_EQ(parsed("\\int_0^\\infty e^{-x^2}\\,\\mathrm{d}x = \\frac{\\sqrt\\pi}{2}"),
            "[(scripts [U+222B:op] sub=[0] sup=[U+221E]) (scripts [e] sup=[U+2212:bin (scripts [x] sup=[2])])"
            " sp(3) \"d\" x =:rel (frac [(sqrt [U+1D70B])] [2])]");
}

// ---- error recovery --------------------------------------------------------------------------

namespace {

struct Recovered {
    std::string tree;
    std::vector<ParseError> errors;
};

Recovered recover(std::string_view source) {
    ParseResult result = parse_formula(source);
    return {dump(result.root), std::move(result.errors)};
}

}

TEST(MathParserErrors, UnknownCommandKeepsItsSourceAsText) {
    const Recovered r = recover("a\\foo b");
    EXPECT_EQ(r.tree, "[a \"\\foo\" b]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::UnknownCommand);
    EXPECT_EQ(r.errors[0].detail, "foo");
    EXPECT_EQ(r.errors[0].offset, 1u);
}

TEST(MathParserErrors, ReportsByteOffsetsNotCodePointIndexes) {
    const Recovered r = recover("\xCE\xB1\\foo");  // alpha is two bytes
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].offset, 2u);
}

TEST(MathParserErrors, MissingClosingBraceClosesAtTheEnd) {
    const Recovered r = recover("{a");
    EXPECT_EQ(r.tree, "[{a}]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::MissingClosingBrace);
    EXPECT_EQ(r.errors[0].offset, 2u);
}

TEST(MathParserErrors, StrayClosingBraceIsDropped) {
    const Recovered r = recover("a}b");
    EXPECT_EQ(r.tree, "[a b]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::UnexpectedClosingBrace);
    EXPECT_EQ(r.errors[0].offset, 1u);
}

TEST(MathParserErrors, MissingArgumentGivesAnEmptyRow) {
    const Recovered r = recover("\\frac{a}");
    EXPECT_EQ(r.tree, "[(frac [a] [])]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::MissingArgument);

    const Recovered script = recover("x^");
    EXPECT_EQ(script.tree, "[(scripts [x] sup=[])]");
    ASSERT_EQ(script.errors.size(), 1u);
    EXPECT_EQ(script.errors[0].kind, ParseErrorKind::MissingArgument);
}

TEST(MathParserErrors, AccentWithoutAnArgument) {
    const Recovered bare = recover(R"(\vec)");
    EXPECT_EQ(bare.tree, "[(accent U+20D7 [])]");
    ASSERT_EQ(bare.errors.size(), 1u);
    EXPECT_EQ(bare.errors[0].kind, ParseErrorKind::MissingArgument);

    const Recovered before_brace = recover(R"({\vec})");
    ASSERT_EQ(before_brace.errors.size(), 1u);
    EXPECT_EQ(before_brace.errors[0].kind, ParseErrorKind::MissingArgument);
}

TEST(MathParserErrors, LeftWithoutRight) {
    const Recovered r = recover("\\left( x");
    EXPECT_EQ(r.tree, "[(delim ( . [x])]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::UnmatchedLeft);
}

TEST(MathParserErrors, RightWithoutLeft) {
    const Recovered r = recover("x \\right) y");
    EXPECT_EQ(r.tree, "[x y]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::UnmatchedRight);
}

TEST(MathParserErrors, MissingOrUnknownDelimiter) {
    const Recovered bare = recover("\\left");
    ASSERT_GE(bare.errors.size(), 1u);
    EXPECT_EQ(bare.errors[0].kind, ParseErrorKind::MissingDelimiter);

    const Recovered unknown = recover("\\left\\foo x \\right)");
    EXPECT_EQ(unknown.tree, "[(delim . ) [x])]");
    ASSERT_EQ(unknown.errors.size(), 1u);
    EXPECT_EQ(unknown.errors[0].kind, ParseErrorKind::MissingDelimiter);
    EXPECT_EQ(unknown.errors[0].detail, "foo");
}

TEST(MathParserErrors, DoubleScriptKeepsTheFirst) {
    const Recovered r = recover("x^1^2");
    EXPECT_EQ(r.tree, "[(scripts [x] sup=[1])]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::DoubleScript);

    const Recovered sub = recover("x_1_2");
    ASSERT_EQ(sub.errors.size(), 1u);
    EXPECT_EQ(sub.errors[0].kind, ParseErrorKind::DoubleScript);
}

TEST(MathParserErrors, LimitsOnlyApplyToOperators) {
    const Recovered r = recover("x\\limits^2");
    EXPECT_EQ(r.tree, "[(scripts [x] sup=[2])]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::LimitsOnNonOperator);

    const Recovered loose = recover("\\nolimits");
    ASSERT_EQ(loose.errors.size(), 1u);
    EXPECT_EQ(loose.errors[0].kind, ParseErrorKind::LimitsOnNonOperator);
}

TEST(MathParserErrors, UnsupportedCharactersAreSkipped) {
    const Recovered r = recover("a$b");
    EXPECT_EQ(r.tree, "[a b]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::UnexpectedCharacter);
    EXPECT_EQ(r.errors[0].detail, "$");
    EXPECT_EQ(r.errors[0].offset, 1u);

    // `\\` would be a line break; there is no multi-line layout yet, so it is an unknown command.
    const Recovered line_break = recover("a\\\\b");
    ASSERT_EQ(line_break.errors.size(), 1u);
    EXPECT_EQ(line_break.errors[0].kind, ParseErrorKind::UnknownCommand);
}

TEST(MathParserErrors, InvalidUtf8IsReportedOnce) {
    const Recovered r = recover("a\xFF" "b");
    EXPECT_EQ(r.tree, "[a b]");
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_EQ(r.errors[0].kind, ParseErrorKind::InvalidUtf8);
    EXPECT_EQ(r.errors[0].offset, 1u);

    const Recovered truncated = recover("\xCE");  // lead byte with no continuation
    ASSERT_EQ(truncated.errors.size(), 1u);
    EXPECT_EQ(truncated.errors[0].kind, ParseErrorKind::InvalidUtf8);
}

TEST(MathParserErrors, CollectsEveryErrorInSourceOrder) {
    const Recovered r = recover("\\foo $ \\bar");
    ASSERT_EQ(r.errors.size(), 3u);
    EXPECT_EQ(r.errors[0].offset, 0u);
    EXPECT_EQ(r.errors[1].offset, 5u);
    EXPECT_EQ(r.errors[2].offset, 7u);
}

TEST(MathParserErrors, DeepNestingIsCappedWithOneError) {
    const std::string deep(500, '{');
    const ParseResult result = parse_formula(deep);
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_EQ(result.errors[0].kind, ParseErrorKind::TooDeep);

    // Reasonable nesting is fine.
    std::string fine;
    for (int i = 0; i < 30; ++i) {
        fine += "{";
    }
    fine += "x";
    for (int i = 0; i < 30; ++i) {
        fine += "}";
    }
    EXPECT_TRUE(parse_formula(fine).ok());
}

TEST(MathParserErrors, DescribeNamesTheProblem) {
    const ParseResult result = parse_formula("a\\foo");
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_EQ(describe(result.errors[0]), "unknown command 'foo' at byte 1");
}

// ---- tables ----------------------------------------------------------------------------------

TEST(MathParserTables, CommandNamesAreUnique) {
    std::set<std::string_view> names;
    for (const CommandSymbol& entry : command_symbols()) {
        EXPECT_TRUE(names.insert(entry.name).second) << "duplicate command \\" << entry.name;
    }
    for (const FunctionName& entry : function_names()) {
        EXPECT_TRUE(names.insert(entry.name).second) << "function collides with a command: \\" << entry.name;
    }
    for (const AccentCommand& entry : accent_commands()) {
        EXPECT_TRUE(names.insert(entry.name).second) << "accent collides with a command: \\" << entry.name;
    }
}

// Every glyph the parser can hand to layout must exist in the shipped math font, or a formula would
// render `.notdef` boxes.
TEST(MathParserTables, EveryEmittedCodepointExistsInTheBuiltinFont) {
    const MathFont& font = stix();
    const auto require = [&](char32_t c, std::string_view what) {
        EXPECT_NE(font.glyph_index(c), 0) << what << " " << hex(c);
    };
    for (const CommandSymbol& entry : command_symbols()) {
        require(entry.symbol.codepoint, std::string("\\") + std::string(entry.name));
    }
    for (const FunctionName& entry : function_names()) {
        for (const char c : entry.name) {
            require(static_cast<char32_t>(c), std::string("\\") + std::string(entry.name));
        }
    }
    for (const AccentCommand& entry : accent_commands()) {
        require(entry.mark, std::string("\\") + std::string(entry.name));
        // The mark must also carry a top-accent attachment or layout falls back to its ink centre.
        EXPECT_TRUE(stix().top_accent_attachment(stix().glyph_index(entry.mark)).has_value()) << entry.name;
    }
    for (char32_t c = U'a'; c <= U'z'; ++c) {
        const ParseResult result = parse_formula(std::string(1, static_cast<char>(c)));
        require(std::get<Symbol>(result.root.front().value).codepoint, "italic letter");
    }
    for (char32_t c = U'A'; c <= U'Z'; ++c) {
        const ParseResult result = parse_formula(std::string(1, static_cast<char>(c)));
        require(std::get<Symbol>(result.root.front().value).codepoint, "italic capital");
    }
    for (const char* source : {"0123456789.!/|?@+-*=<>:()[],;'", "\\left< x \\right>"}) {
        const ParseResult result = parse_formula(source);
        for (const Node& node : result.root) {
            if (const auto* symbol = std::get_if<Symbol>(&node.value)) {
                require(symbol->codepoint, source);
            } else if (const auto* scripts = std::get_if<Scripts>(&node.value)) {
                require(std::get<Symbol>(scripts->base.front().value).codepoint, source);
                require(std::get<Symbol>(scripts->superscript->front().value).codepoint, source);
            } else if (const auto* delimited = std::get_if<Delimited>(&node.value)) {
                require(delimited->open, source);
                require(delimited->close, source);
            }
        }
    }
}
