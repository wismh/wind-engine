#pragma once

#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace engine::cli {

    // Streaming JSON for CLI bodies: keys, values, and nesting, with the commas written for the caller.
    class Json {
    public:
        void begin_object() { begin('{'); }
        void end_object() { end('}'); }
        void begin_array() { begin('['); }
        void end_array() { end(']'); }

        void key(std::string_view name) {
            comma();
            raw_string(name);
            out_ += ':';
            suppress_ = true;
        }

        void string(std::string_view value) {
            comma();
            raw_string(value);
        }

        void boolean(bool value) {
            comma();
            out_ += value ? "true" : "false";
        }

        void null() {
            comma();
            out_ += "null";
        }

        void integer(std::int64_t value) {
            comma();
            out_ += std::to_string(value);
        }

        void number(double value) {
            comma();
            if (!std::isfinite(value)) {
                out_ += "null";
                return;
            }
            out_ += std::format("{:.6g}", value);
        }

        [[nodiscard]] std::string str() const { return out_; }

    private:
        void begin(char open) {
            comma();
            out_ += open;
            fresh_.push_back(true);
            suppress_ = false;
        }

        void end(char close) {
            out_ += close;
            if (!fresh_.empty()) {
                fresh_.pop_back();
            }
            suppress_ = false;
        }

        void comma() {
            if (suppress_) {
                suppress_ = false;
                return;
            }
            if (!fresh_.empty() && !fresh_.back()) {
                out_ += ',';
            }
            if (!fresh_.empty()) {
                fresh_.back() = false;
            }
        }

        void raw_string(std::string_view value) {
            out_ += '"';
            for (const unsigned char c: value) {
                switch (c) {
                    case '"':
                        out_ += "\\\"";
                        break;
                    case '\\':
                        out_ += "\\\\";
                        break;
                    case '\n':
                        out_ += "\\n";
                        break;
                    case '\r':
                        out_ += "\\r";
                        break;
                    case '\t':
                        out_ += "\\t";
                        break;
                    default:
                        if (c < 0x20) {
                            out_ += std::format("\\u{:04x}", static_cast<unsigned>(c));
                        } else {
                            out_ += static_cast<char>(c);
                        }
                        break;
                }
            }
            out_ += '"';
        }

        std::string out_;
        std::vector<char> fresh_{true};
        bool suppress_ = false;
    };

} // namespace engine::cli
