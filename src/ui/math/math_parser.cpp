#include "ui/math/math_parser.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace engine::ui::math {
namespace {

constexpr Symbol ord(char32_t c) {
    return {c, MathClass::Ord, false};
}
constexpr Symbol bin(char32_t c) {
    return {c, MathClass::Bin, false};
}
constexpr Symbol rel(char32_t c) {
    return {c, MathClass::Rel, false};
}
constexpr Symbol open_delim(char32_t c) {
    return {c, MathClass::Open, false};
}
constexpr Symbol close_delim(char32_t c) {
    return {c, MathClass::Close, false};
}
constexpr Symbol large_op(char32_t c, bool limits_in_display) {
    return {c, MathClass::Op, limits_in_display};
}

// Mathematical Italic small Greek alpha..omega run (U+1D6FC..U+1D714, final sigma included).
constexpr char32_t greek_italic(char32_t index) {
    return 0x1D6FC + index;
}

constexpr CommandSymbol kCommandSymbols[] = {
        // Lower-case Greek, italic as TeX sets it. `\epsilon` is the lunate form, `\varepsilon` the
        // curly one; `\phi`/`\varphi` follow unicode-math.
        {"alpha", ord(greek_italic(0))},
        {"beta", ord(greek_italic(1))},
        {"gamma", ord(greek_italic(2))},
        {"delta", ord(greek_italic(3))},
        {"varepsilon", ord(greek_italic(4))},
        {"zeta", ord(greek_italic(5))},
        {"eta", ord(greek_italic(6))},
        {"theta", ord(greek_italic(7))},
        {"iota", ord(greek_italic(8))},
        {"kappa", ord(greek_italic(9))},
        {"lambda", ord(greek_italic(10))},
        {"mu", ord(greek_italic(11))},
        {"nu", ord(greek_italic(12))},
        {"xi", ord(greek_italic(13))},
        {"pi", ord(greek_italic(15))},
        {"rho", ord(greek_italic(16))},
        {"varsigma", ord(greek_italic(17))},
        {"sigma", ord(greek_italic(18))},
        {"tau", ord(greek_italic(19))},
        {"upsilon", ord(greek_italic(20))},
        {"varphi", ord(greek_italic(21))},
        {"chi", ord(greek_italic(22))},
        {"psi", ord(greek_italic(23))},
        {"omega", ord(greek_italic(24))},
        {"epsilon", ord(0x1D716)},
        {"vartheta", ord(0x1D717)},
        {"phi", ord(0x1D719)},
        {"varrho", ord(0x1D71A)},
        {"varpi", ord(0x1D71B)},
        {"partial", ord(0x1D715)},
        // Upper-case Greek stays upright.
        {"Gamma", ord(0x0393)},
        {"Delta", ord(0x0394)},
        {"Theta", ord(0x0398)},
        {"Lambda", ord(0x039B)},
        {"Xi", ord(0x039E)},
        {"Pi", ord(0x03A0)},
        {"Sigma", ord(0x03A3)},
        {"Upsilon", ord(0x03A5)},
        {"Phi", ord(0x03A6)},
        {"Psi", ord(0x03A8)},
        {"Omega", ord(0x03A9)},
        // Large operators.
        {"sum", large_op(0x2211, true)},
        {"prod", large_op(0x220F, true)},
        {"coprod", large_op(0x2210, true)},
        {"int", large_op(0x222B, false)},
        {"iint", large_op(0x222C, false)},
        {"iiint", large_op(0x222D, false)},
        {"oint", large_op(0x222E, false)},
        {"bigcup", large_op(0x22C3, true)},
        {"bigcap", large_op(0x22C2, true)},
        {"bigvee", large_op(0x22C1, true)},
        {"bigwedge", large_op(0x22C0, true)},
        {"bigoplus", large_op(0x2A01, true)},
        {"bigotimes", large_op(0x2A02, true)},
        // Binary operators.
        {"cdot", bin(0x22C5)},
        {"times", bin(0x00D7)},
        {"div", bin(0x00F7)},
        {"pm", bin(0x00B1)},
        {"mp", bin(0x2213)},
        {"ast", bin(0x2217)},
        {"star", bin(0x22C6)},
        {"circ", bin(0x2218)},
        {"setminus", bin(0x2216)},
        {"cup", bin(0x222A)},
        {"cap", bin(0x2229)},
        {"wedge", bin(0x2227)},
        {"land", bin(0x2227)},
        {"vee", bin(0x2228)},
        {"lor", bin(0x2228)},
        {"oplus", bin(0x2295)},
        {"otimes", bin(0x2297)},
        // Relations.
        {"leq", rel(0x2264)},
        {"le", rel(0x2264)},
        {"geq", rel(0x2265)},
        {"ge", rel(0x2265)},
        {"neq", rel(0x2260)},
        {"ne", rel(0x2260)},
        {"approx", rel(0x2248)},
        {"equiv", rel(0x2261)},
        {"sim", rel(0x223C)},
        {"simeq", rel(0x2243)},
        {"cong", rel(0x2245)},
        {"propto", rel(0x221D)},
        {"ll", rel(0x226A)},
        {"gg", rel(0x226B)},
        {"in", rel(0x2208)},
        {"notin", rel(0x2209)},
        {"ni", rel(0x220B)},
        {"subset", rel(0x2282)},
        {"supset", rel(0x2283)},
        {"subseteq", rel(0x2286)},
        {"supseteq", rel(0x2287)},
        {"perp", rel(0x22A5)},
        {"parallel", rel(0x2225)},
        {"to", rel(0x2192)},
        {"rightarrow", rel(0x2192)},
        {"leftarrow", rel(0x2190)},
        {"leftrightarrow", rel(0x2194)},
        {"Rightarrow", rel(0x21D2)},
        {"Leftarrow", rel(0x21D0)},
        {"Leftrightarrow", rel(0x21D4)},
        {"mapsto", rel(0x21A6)},
        // Ordinary symbols.
        {"infty", ord(0x221E)},
        {"nabla", ord(0x2207)},
        {"forall", ord(0x2200)},
        {"exists", ord(0x2203)},
        {"neg", ord(0x00AC)},
        {"emptyset", ord(0x2205)},
        {"prime", ord(0x2032)},
        {"angle", ord(0x2220)},
        {"hbar", ord(0x210F)},
        {"ell", ord(0x2113)},
        {"aleph", ord(0x2135)},
        {"Re", ord(0x211C)},
        {"Im", ord(0x2111)},
        {"ldots", ord(0x2026)},
        {"cdots", ord(0x22EF)},
        {"vdots", ord(0x22EE)},
        {"ddots", ord(0x22F1)},
        {"backslash", ord(0x005C)},
        {"vert", ord(0x007C)},
        {"Vert", ord(0x2016)},
        {"|", ord(0x2016)},
        // Delimiters and literal characters spelled as control symbols.
        {"{", open_delim(0x007B)},
        {"}", close_delim(0x007D)},
        {"lbrace", open_delim(0x007B)},
        {"rbrace", close_delim(0x007D)},
        {"langle", open_delim(0x27E8)},
        {"rangle", close_delim(0x27E9)},
        {"lfloor", open_delim(0x230A)},
        {"rfloor", close_delim(0x230B)},
        {"lceil", open_delim(0x2308)},
        {"rceil", close_delim(0x2309)},
        {"%", ord(U'%')},
        {"$", ord(U'$')},
        {"&", ord(U'&')},
        {"#", ord(U'#')},
        {"_", ord(U'_')},
};

constexpr FunctionName kFunctionNames[] = {
        {"sin", false},
        {"cos", false},
        {"tan", false},
        {"cot", false},
        {"sec", false},
        {"csc", false},
        {"arcsin", false},
        {"arccos", false},
        {"arctan", false},
        {"sinh", false},
        {"cosh", false},
        {"tanh", false},
        {"coth", false},
        {"log", false},
        {"ln", false},
        {"lg", false},
        {"exp", false},
        {"arg", false},
        {"deg", false},
        {"dim", false},
        {"ker", false},
        {"hom", false},
        {"lim", true},
        {"limsup", true},
        {"liminf", true},
        {"max", true},
        {"min", true},
        {"sup", true},
        {"inf", true},
        {"det", true},
        {"gcd", true},
        {"Pr", true},
};

constexpr std::size_t kMaxDepth = 64;

constexpr float kThinSpace = 3.0f / 18.0f;
constexpr float kMediumSpace = 4.0f / 18.0f;
constexpr float kThickSpace = 5.0f / 18.0f;

// `a`-`z` -> Mathematical Italic (the lone hole at `h` is Planck's constant), `A`-`Z` likewise.
char32_t italic_latin(char32_t c) {
    if (c >= U'a' && c <= U'z') {
        return c == U'h' ? char32_t{0x210E} : char32_t{0x1D44E} + (c - U'a');
    }
    return char32_t{0x1D434} + (c - U'A');
}

bool is_ascii_letter(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
}

bool is_space(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r';
}

std::string encode_utf8(char32_t c) {
    std::string out;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

const std::unordered_map<std::string_view, const CommandSymbol*>& symbol_index() {
    static const auto index = [] {
        std::unordered_map<std::string_view, const CommandSymbol*> map;
        for (const CommandSymbol& entry : kCommandSymbols) {
            map.emplace(entry.name, &entry);
        }
        return map;
    }();
    return index;
}

const std::unordered_map<std::string_view, const FunctionName*>& function_index() {
    static const auto index = [] {
        std::unordered_map<std::string_view, const FunctionName*> map;
        for (const FunctionName& entry : kFunctionNames) {
            map.emplace(entry.name, &entry);
        }
        return map;
    }();
    return index;
}

Node make_node(auto&& value) {
    return Node{std::forward<decltype(value)>(value)};
}

class Parser {
public:
    explicit Parser(std::string_view source) : source_size_(source.size()) {
        decode(source);
    }

    ParseResult run() {
        ParseResult result;
        result.root = parse_row(RowEnd::Top);
        result.errors = std::move(errors_);
        return result;
    }

private:
    enum class RowEnd {
        Top,
        Brace,    // closed by `}`
        Bracket,  // closed by `]` (the optional `\sqrt[n]` index)
        Right,    // closed by `\right` (left unconsumed for the caller)
    };

    void decode(std::string_view source) {
        std::size_t i = 0;
        while (i < source.size()) {
            const auto byte = static_cast<unsigned char>(source[i]);
            std::size_t length = 1;
            char32_t cp = byte;
            if (byte >= 0xF0 && byte <= 0xF4) {
                length = 4;
                cp = byte & 0x07;
            } else if (byte >= 0xE0) {
                length = 3;
                cp = byte & 0x0F;
            } else if (byte >= 0xC2 && byte < 0xE0) {
                length = 2;
                cp = byte & 0x1F;
            } else if (byte >= 0x80) {
                length = 0;
            }
            bool valid = length != 0 && i + length <= source.size() && byte <= 0xF4;
            for (std::size_t k = 1; valid && k < length; ++k) {
                const auto next = static_cast<unsigned char>(source[i + k]);
                if ((next & 0xC0) != 0x80) {
                    valid = false;
                } else {
                    cp = (cp << 6) | (next & 0x3F);
                }
            }
            if (valid && ((length == 3 && cp < 0x800) || (length == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
                                 (cp >= 0xD800 && cp <= 0xDFFF))) {
                valid = false;
            }
            offsets_.push_back(i);
            if (!valid) {
                errors_.push_back({ParseErrorKind::InvalidUtf8, i, {}});
                cps_.push_back(char32_t{0xFFFD});
                i += 1;
                continue;
            }
            cps_.push_back(cp);
            i += length;
        }
    }

    [[nodiscard]] bool at_end() const {
        return pos_ >= cps_.size();
    }

    [[nodiscard]] char32_t peek() const {
        return pos_ < cps_.size() ? cps_[pos_] : U'\0';
    }

    [[nodiscard]] std::size_t offset_here() const {
        return pos_ < offsets_.size() ? offsets_[pos_] : source_size_;
    }

    void skip_space() {
        while (!at_end() && is_space(peek())) {
            ++pos_;
        }
    }

    void report(ParseErrorKind kind, std::string detail = {}) {
        if (aborted_) {
            return;
        }
        errors_.push_back({kind, offset_here(), std::move(detail)});
    }

    bool enter() {
        if (depth_ >= kMaxDepth) {
            report(ParseErrorKind::TooDeep);
            aborted_ = true;
            pos_ = cps_.size();
            return false;
        }
        ++depth_;
        return true;
    }

    // Name of the command at the cursor (cursor on the backslash), without consuming anything.
    [[nodiscard]] std::string peek_command_name() {
        const std::size_t saved = pos_;
        ++pos_;
        std::string name = read_command_name();
        pos_ = saved;
        return name;
    }

    // Cursor just past a backslash. Letters make a word; anything else is a one-character control symbol.
    std::string read_command_name() {
        if (at_end()) {
            return {};
        }
        std::string name;
        if (is_ascii_letter(peek())) {
            while (!at_end() && is_ascii_letter(peek())) {
                name += static_cast<char>(peek());
                ++pos_;
            }
            return name;
        }
        name = encode_utf8(peek());
        ++pos_;
        return name;
    }

    Row parse_row(RowEnd end) {
        Row row;
        if (!enter()) {
            return row;
        }
        for (;;) {
            skip_space();
            if (at_end()) {
                if (end == RowEnd::Brace || end == RowEnd::Bracket) {
                    report(ParseErrorKind::MissingClosingBrace);
                } else if (end == RowEnd::Right) {
                    report(ParseErrorKind::UnmatchedLeft);
                }
                break;
            }
            const char32_t c = peek();
            if (c == U'}') {
                if (end == RowEnd::Brace) {
                    ++pos_;
                    break;
                }
                report(ParseErrorKind::UnexpectedClosingBrace);
                ++pos_;
                continue;
            }
            if (c == U']' && end == RowEnd::Bracket) {
                ++pos_;
                break;
            }
            if (c == U'\\' && peek_command_name() == "right") {
                if (end == RowEnd::Right) {
                    break;
                }
                report(ParseErrorKind::UnmatchedRight);
                ++pos_;
                read_command_name();
                parse_delimiter();
                continue;
            }
            parse_atom(row);
            if (aborted_) {
                break;
            }
        }
        --depth_;
        return row;
    }

    void parse_atom(Row& row) {
        std::optional<Node> base;
        const char32_t c = peek();
        if (c == U'^' || c == U'_' || c == U'\'') {
            base = make_node(Group{});
        } else {
            base = parse_base();
        }
        if (!base) {
            return;
        }

        const bool takes_limits = [&] {
            if (const auto* symbol = std::get_if<Symbol>(&base->value)) {
                return symbol->math_class == MathClass::Op;
            }
            return std::holds_alternative<OperatorName>(base->value);
        }();

        std::optional<Row> subscript;
        std::optional<Row> superscript;
        bool superscript_is_primes = false;
        LimitsMode limits = LimitsMode::Default;
        for (;;) {
            skip_space();
            if (at_end()) {
                break;
            }
            const char32_t next = peek();
            if (next == U'^' || next == U'_') {
                ++pos_;
                Row argument = parse_argument();
                std::optional<Row>& slot = next == U'^' ? superscript : subscript;
                if (slot) {
                    report(ParseErrorKind::DoubleScript);
                } else {
                    slot = std::move(argument);
                    superscript_is_primes = false;
                }
            } else if (next == U'\'') {
                ++pos_;
                if (superscript && !superscript_is_primes) {
                    report(ParseErrorKind::DoubleScript);
                    continue;
                }
                if (!superscript) {
                    superscript = Row{};
                }
                superscript->push_back(make_node(ord(0x2032)));
                superscript_is_primes = true;
            } else if (next == U'\\') {
                const std::string name = peek_command_name();
                if (name != "limits" && name != "nolimits") {
                    break;
                }
                ++pos_;
                read_command_name();
                if (takes_limits) {
                    limits = name == "limits" ? LimitsMode::Limits : LimitsMode::NoLimits;
                } else {
                    report(ParseErrorKind::LimitsOnNonOperator, name);
                }
            } else {
                break;
            }
        }

        if (!subscript && !superscript) {
            row.push_back(std::move(*base));
            return;
        }
        Scripts scripts;
        scripts.base.push_back(std::move(*base));
        scripts.subscript = std::move(subscript);
        scripts.superscript = std::move(superscript);
        scripts.limits = limits;
        row.push_back(make_node(std::move(scripts)));
    }

    // A script/`\frac`/`\sqrt` argument: `{row}` or, as in TeX, one single token (`x^2`, `\frac12`).
    Row parse_argument() {
        skip_space();
        if (at_end()) {
            report(ParseErrorKind::MissingArgument);
            return {};
        }
        const char32_t c = peek();
        if (c == U'{') {
            ++pos_;
            return parse_row(RowEnd::Brace);
        }
        if (c == U'}' || c == U'^' || c == U'_') {
            report(ParseErrorKind::MissingArgument);
            return {};
        }
        Row row;
        if (std::optional<Node> token = parse_base()) {
            row.push_back(std::move(*token));
        }
        return row;
    }

    std::optional<Node> parse_base() {
        const char32_t c = peek();
        if (c == U'{') {
            ++pos_;
            return make_node(Group{parse_row(RowEnd::Brace)});
        }
        if (c == U'\\') {
            ++pos_;
            return parse_command();
        }
        ++pos_;
        if (is_ascii_letter(c)) {
            return make_node(ord(italic_latin(c)));
        }
        if ((c >= U'0' && c <= U'9') || c == U'.' || c == U'!' || c == U'/' || c == U'|' || c == U'?' || c == U'@') {
            return make_node(ord(c));
        }
        switch (c) {
            case U'+':
                return make_node(bin(U'+'));
            case U'-':
                return make_node(bin(0x2212));
            case U'*':
                return make_node(bin(0x2217));
            case U'=':
            case U'<':
            case U'>':
            case U':':
                return make_node(rel(c));
            case U'(':
            case U'[':
                return make_node(open_delim(c));
            case U')':
            case U']':
                return make_node(close_delim(c));
            case U',':
            case U';':
                return make_node(Symbol{c, MathClass::Punct, false});
            default:
                break;
        }
        if (c == char32_t{0xFFFD}) {
            return std::nullopt;  // already reported as InvalidUtf8
        }
        if (c >= 0x80) {
            return make_node(ord(c));
        }
        --pos_;
        report(ParseErrorKind::UnexpectedCharacter, encode_utf8(c));
        ++pos_;
        return std::nullopt;
    }

    std::optional<Node> parse_command() {
        if (at_end()) {
            report(ParseErrorKind::UnexpectedCharacter, "\\");
            return std::nullopt;
        }
        const std::size_t command_pos = pos_ - 1;
        const std::string name = read_command_name();

        if (name == "frac") {
            Row numerator = parse_argument();
            Row denominator = parse_argument();
            return make_node(Fraction{std::move(numerator), std::move(denominator)});
        }
        if (name == "sqrt") {
            skip_space();
            std::optional<Row> index;
            if (!at_end() && peek() == U'[') {
                ++pos_;
                index = parse_row(RowEnd::Bracket);
            }
            Row radicand = parse_argument();
            return make_node(Radical{std::move(radicand), std::move(index)});
        }
        if (name == "left") {
            const char32_t open = parse_delimiter();
            Row body = parse_row(RowEnd::Right);
            char32_t close = 0;
            if (!at_end() && peek() == U'\\' && peek_command_name() == "right") {
                ++pos_;
                read_command_name();
                close = parse_delimiter();
            }
            return make_node(Delimited{open, close, std::move(body)});
        }
        if (name == "text") {
            return make_node(Text{read_raw_group(true)});
        }
        if (name == "mathrm") {
            return make_node(Text{read_raw_group(false)});
        }
        if (name == "operatorname") {
            return make_node(OperatorName{read_raw_group(false), false});
        }
        if (name == "limits" || name == "nolimits") {
            report(ParseErrorKind::LimitsOnNonOperator, name);
            return std::nullopt;
        }
        if (name == ",") {
            return make_node(Space{kThinSpace});
        }
        if (name == ":") {
            return make_node(Space{kMediumSpace});
        }
        if (name == ";") {
            return make_node(Space{kThickSpace});
        }
        if (name == "!") {
            return make_node(Space{-kThinSpace});
        }
        if (name == " ") {
            return make_node(Space{1.0f / 3.0f});
        }
        if (name == "quad") {
            return make_node(Space{1.0f});
        }
        if (name == "qquad") {
            return make_node(Space{2.0f});
        }
        if (const auto it = symbol_index().find(name); it != symbol_index().end()) {
            return make_node(it->second->symbol);
        }
        if (const auto it = function_index().find(name); it != function_index().end()) {
            OperatorName function;
            for (const char c : it->second->name) {
                function.name += static_cast<char32_t>(c);
            }
            function.limits_in_display = it->second->limits_in_display;
            return make_node(std::move(function));
        }

        // Unknown (or `\\`, which has no meaning without multi-line support): keep its source so something
        // visible still marks the spot.
        errors_.push_back({ParseErrorKind::UnknownCommand, offsets_[command_pos], name});
        Text fallback;
        fallback.text += U'\\';
        for (const char c : name) {
            fallback.text += static_cast<char32_t>(static_cast<unsigned char>(c));
        }
        return make_node(std::move(fallback));
    }

    // `\left` / `\right` delimiter; 0 for `.` or when none follows.
    char32_t parse_delimiter() {
        skip_space();
        if (at_end()) {
            report(ParseErrorKind::MissingDelimiter);
            return 0;
        }
        const char32_t c = peek();
        if (c == U'.') {
            ++pos_;
            return 0;
        }
        if (c == U'\\') {
            ++pos_;
            const std::size_t command_pos = pos_ - 1;
            const std::string name = read_command_name();
            if (const auto it = symbol_index().find(name); it != symbol_index().end()) {
                const MathClass math_class = it->second->symbol.math_class;
                if (math_class == MathClass::Open || math_class == MathClass::Close || math_class == MathClass::Ord) {
                    return it->second->symbol.codepoint;
                }
            }
            errors_.push_back({ParseErrorKind::MissingDelimiter, offsets_[command_pos], name});
            return 0;
        }
        if (c == U'(' || c == U')' || c == U'[' || c == U']' || c == U'|' || c == U'/') {
            ++pos_;
            return c;
        }
        if (c == U'<' || c == U'>') {
            ++pos_;
            return c == U'<' ? char32_t{0x27E8} : char32_t{0x27E9};
        }
        report(ParseErrorKind::MissingDelimiter);
        return 0;
    }

    // `{raw text}` for `\text`, `\mathrm`, `\operatorname`: no commands, braces nest and vanish. `keep_spaces`
    // collapses whitespace runs to one space (`\text`); otherwise whitespace is dropped.
    std::u32string read_raw_group(bool keep_spaces) {
        skip_space();
        if (at_end() || peek() != U'{') {
            report(ParseErrorKind::MissingArgument);
            return {};
        }
        ++pos_;
        std::u32string out;
        int depth = 1;
        bool pending_space = false;
        while (!at_end()) {
            const char32_t c = cps_[pos_++];
            if (c == U'{') {
                ++depth;
                continue;
            }
            if (c == U'}') {
                if (--depth == 0) {
                    if (pending_space) {
                        out += U' ';
                    }
                    return out;
                }
                continue;
            }
            if (is_space(c)) {
                pending_space = keep_spaces;
                continue;
            }
            if (pending_space) {
                out += U' ';
                pending_space = false;
            }
            if (c == U'\\' && !at_end() && std::u32string_view(U"{}%$&#_").find(peek()) != std::u32string_view::npos) {
                out += cps_[pos_++];
                continue;
            }
            out += c;
        }
        report(ParseErrorKind::MissingClosingBrace);
        return out;
    }

    std::vector<char32_t> cps_;
    std::vector<std::size_t> offsets_;
    std::vector<ParseError> errors_;
    std::size_t source_size_ = 0;
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
    bool aborted_ = false;
};

}

ParseResult parse_formula(std::string_view source) {
    return Parser(source).run();
}

std::string_view to_string(ParseErrorKind kind) noexcept {
    switch (kind) {
        case ParseErrorKind::UnknownCommand:
            return "unknown command";
        case ParseErrorKind::UnexpectedCharacter:
            return "unexpected character";
        case ParseErrorKind::MissingClosingBrace:
            return "missing closing brace";
        case ParseErrorKind::UnexpectedClosingBrace:
            return "unexpected closing brace";
        case ParseErrorKind::MissingArgument:
            return "missing argument";
        case ParseErrorKind::MissingDelimiter:
            return "missing delimiter";
        case ParseErrorKind::UnmatchedLeft:
            return "\\left without \\right";
        case ParseErrorKind::UnmatchedRight:
            return "\\right without \\left";
        case ParseErrorKind::DoubleScript:
            return "double script";
        case ParseErrorKind::LimitsOnNonOperator:
            return "limits on a non-operator";
        case ParseErrorKind::InvalidUtf8:
            return "invalid UTF-8";
        case ParseErrorKind::TooDeep:
            return "formula nested too deeply";
    }
    return "unknown error";
}

std::string describe(const ParseError& error) {
    std::string text(to_string(error.kind));
    if (!error.detail.empty()) {
        text += " '";
        text += error.detail;
        text += "'";
    }
    text += " at byte ";
    text += std::to_string(error.offset);
    return text;
}

std::span<const CommandSymbol> command_symbols() {
    return kCommandSymbols;
}

std::span<const FunctionName> function_names() {
    return kFunctionNames;
}

}
