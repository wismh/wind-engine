#include "bench_report.h"

#include <format>

namespace bench {
namespace {

std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
    return out;
}

std::string_view json_bool(bool value) {
    return value ? "true" : "false";
}

}

std::string bench_report_json(const BenchReport& report, std::string_view profile) {
    std::string out = "{\"bench\":{";
    out += "\"scene\":" + json_string(bench_scene_name(report.bench_case.scene));
    out += ",\"mode\":" + json_string(bench_mode_name(report.bench_case.mode));
    out += ",\"variant\":" + json_string(report.bench_case.variant);
    out += std::format(",\"warmup\":{},\"frames\":{}", report.warmup, report.frames);
    out += std::format(",\"window\":{{\"width\":{},\"height\":{},\"drawable_width\":{},\"drawable_height\":{}}}",
            report.width, report.height, report.drawable_width, report.drawable_height);
    out += std::format(",\"vsync\":{},\"hover_targets\":{}", json_bool(report.vsync), report.hover_targets);
    out += ",\"config\":" + json_string(report.config);
    out += ",\"engine_version\":" + json_string(report.engine_version);
    out += ",\"build_id\":" + json_string(report.build_id);
    out += ",\"commit\":" + json_string(report.commit);
    out += std::format(",\"dirty\":{}", json_bool(report.dirty));
    out += ",\"timestamp\":" + json_string(report.timestamp);
    out += "},\"profile\":";
    out += profile.empty() ? std::string_view("null") : profile;
    out += "}\n";
    return out;
}

std::string utc_timestamp(std::chrono::system_clock::time_point time) {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(time));
}

}
