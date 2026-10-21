#include "svg_internal.h"

#include <algorithm>

namespace engine::ui::svg_detail {

std::string_view trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string lower_copy(std::string_view value) {
    std::string out(value);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::optional<float> length_value(std::string_view raw) {
    raw = trim(raw);
    if (raw.size() >= 2) {
        const std::string tail = lower_copy(raw.substr(raw.size() - 2));
        if (tail == "px") {
            raw = trim(raw.substr(0, raw.size() - 2));
        } else if (
                tail == "em" || tail == "ex" || tail == "pt" || tail == "pc" || tail == "in" || tail == "cm" ||
                tail == "mm") {
            return std::nullopt;
        }
    }
    if (!raw.empty() && raw.back() == '%') {
        return std::nullopt;
    }
    Numbers numbers{raw};
    if (!numbers.has()) {
        return std::nullopt;
    }
    const float value = numbers.number();
    numbers.skip();
    if (!numbers.ok || numbers.i != numbers.text.size()) {
        return std::nullopt;
    }
    return value;
}

bool parse_transform(std::string_view text, Aff2& out) {
    Numbers numbers{text};
    Aff2 combined;
    numbers.skip();
    while (numbers.i < numbers.text.size()) {
        const std::size_t begin = numbers.i;
        while (numbers.i < numbers.text.size() &&
               std::isalpha(static_cast<unsigned char>(numbers.text[numbers.i])) != 0) {
            ++numbers.i;
        }
        const std::string_view name = numbers.text.substr(begin, numbers.i - begin);
        numbers.skip();
        if (numbers.i >= numbers.text.size() || numbers.text[numbers.i] != '(') {
            return false;
        }
        ++numbers.i;
        float args[6] = {};
        int count = 0;
        while (count < 6 && numbers.has()) {
            args[count++] = numbers.number();
        }
        numbers.skip();
        if (!numbers.ok || numbers.i >= numbers.text.size() || numbers.text[numbers.i] != ')') {
            return false;
        }
        ++numbers.i;
        Aff2 local;
        if (name == "matrix") {
            if (count != 6) {
                return false;
            }
            local = {args[0], args[1], args[2], args[3], args[4], args[5]};
        } else if (name == "translate") {
            if (count < 1 || count > 2) {
                return false;
            }
            local.e = args[0];
            local.f = count == 2 ? args[1] : 0.0f;
        } else if (name == "scale") {
            if (count < 1 || count > 2) {
                return false;
            }
            local.a = args[0];
            local.d = count == 2 ? args[1] : args[0];
        } else if (name == "rotate") {
            if (count != 1 && count != 3) {
                return false;
            }
            const float rad = args[0] * kPi / 180.0f;
            const float cos_a = std::cos(rad);
            const float sin_a = std::sin(rad);
            local.a = cos_a;
            local.b = sin_a;
            local.c = -sin_a;
            local.d = cos_a;
            if (count == 3) {
                Aff2 to;
                to.e = args[1];
                to.f = args[2];
                Aff2 back;
                back.e = -args[1];
                back.f = -args[2];
                local = mul(mul(to, local), back);
            }
        } else {
            return false;
        }
        combined = mul(combined, local);
        numbers.skip();
    }
    if (!numbers.ok) {
        return false;
    }
    out = combined;
    return true;
}

bool element_transform(const tinyxml2::XMLElement* element, Aff2 parent, Aff2& out) {
    Aff2 local;
    if (const char* value = element->Attribute("transform")) {
        if (!parse_transform(value, local)) {
            return false;
        }
    }
    out = mul(parent, local);
    return true;
}

float attr_length(const tinyxml2::XMLElement* element, const char* name, float fallback, bool& ok) {
    const char* value = element->Attribute(name);
    if (value == nullptr) {
        return fallback;
    }
    const auto parsed = length_value(value);
    if (!parsed) {
        ok = false;
        return fallback;
    }
    return *parsed;
}

}
