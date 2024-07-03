#include <engine/render/shader_adapt.h>

#include <string>

namespace engine::render {
namespace {

void replace_all(std::string& text, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return;
    }
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool line_has_version_300_es(std::string_view line) {
    return line.find("300") != std::string_view::npos && line.find("es") != std::string_view::npos;
}

bool has_float_precision(std::string_view src) {
    const std::size_t precision = src.find("precision");
    if (precision == std::string_view::npos) {
        return false;
    }
    const std::size_t flt = src.find("float", precision);
    return flt != std::string_view::npos;
}

std::string rewrite_version_to_300_es(std::string src) {
    const std::size_t version = src.find("#version");
    if (version == std::string::npos) {
        if (src.empty()) {
            return "#version 300 es\n";
        }
        return std::string("#version 300 es\n") + src;
    }
    std::size_t eol = src.find('\n', version);
    if (eol == std::string::npos) {
        eol = src.size();
    }
    const std::string_view line{src.data() + version, eol - version};
    if (line_has_version_300_es(line)) {
        return src;
    }
    src.replace(version, eol - version, "#version 300 es");
    return src;
}

std::string insert_fragment_precision(std::string src) {
    if (has_float_precision(src)) {
        return src;
    }
    const std::size_t version = src.find("#version");
    if (version == std::string::npos) {
        return std::string("precision mediump float;\n") + src;
    }
    const std::size_t eol = src.find('\n', version);
    if (eol == std::string::npos) {
        return src + "\nprecision mediump float;\n";
    }
    src.insert(eol + 1, "precision mediump float;\n");
    return src;
}

}

std::string adapt_glsl(std::string_view src, ShaderTarget target, bool fragment) {
    if (target == ShaderTarget::Glsl330Core) {
        return std::string(src);
    }

    std::string out = rewrite_version_to_300_es(std::string(src));
    replace_all(out, "texture2D(", "texture(");
    if (fragment) {
        out = insert_fragment_precision(out);
    }
    return out;
}

ShaderDesc adapt_shader(const ShaderDesc& src, ShaderTarget target) {
    ShaderDesc out;
    out.vertex_src = adapt_glsl(src.vertex_src, target, false);
    out.fragment_src = adapt_glsl(src.fragment_src, target, true);
    return out;
}

}
