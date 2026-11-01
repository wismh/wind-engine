#include "render/slang_compiler.h"

#ifdef ENGINE_WITH_SLANG
#include <slang-com-ptr.h>
#include <slang.h>
#endif

#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::render {
namespace {

bool is_ident_char(char c) noexcept {
    const auto uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) != 0 || c == '_';
}

std::string_view trim_view(std::string_view sv) noexcept {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
        sv.remove_suffix(1);
    }
    return sv;
}

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

void replace_identifier(std::string& text, std::string_view from, std::string_view to) {
    if (from.empty() || from == to) {
        return;
    }
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        const bool left_ok = (pos == 0) || !is_ident_char(text[pos - 1]);
        const std::size_t end = pos + from.size();
        const bool right_ok = (end >= text.size()) || !is_ident_char(text[end]);
        if (left_ok && right_ok) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        } else {
            pos = end;
        }
    }
}

void unwrap_global_params_ubo(std::string& src) {
    // 1. Remove `struct GlobalParams_... { ... };`
    std::size_t struct_pos = src.find("struct GlobalParams_");
    while (struct_pos != std::string::npos) {
        const std::size_t brace_close = src.find("};", struct_pos);
        if (brace_close == std::string::npos) {
            break;
        }
        std::size_t erase_end = brace_close + 2;
        while (erase_end < src.size() && (src[erase_end] == '\r' || src[erase_end] == '\n')) {
            ++erase_end;
        }
        src.erase(struct_pos, erase_end - struct_pos);
        struct_pos = src.find("struct GlobalParams_");
    }

    // 2. Unwrap `layout(...) uniform block_GlobalParams_... { ... } globalParams_...;`
    const std::size_t block_kw = src.find("uniform block_GlobalParams_");
    if (block_kw == std::string::npos) {
        return;
    }

    // Walk backward to include any preceding `layout(...)` qualifiers for this block.
    std::size_t decl_start = block_kw;
    while (decl_start > 0) {
        std::size_t prev = decl_start;
        while (prev > 0 && std::isspace(static_cast<unsigned char>(src[prev - 1])) != 0) {
            --prev;
        }
        if (prev > 0 && src[prev - 1] == ')') {
            const std::size_t layout_pos = src.rfind("layout(", prev - 1);
            if (layout_pos != std::string::npos) {
                decl_start = layout_pos;
                continue;
            }
        }
        break;
    }

    const std::size_t open_brace = src.find('{', block_kw);
    if (open_brace == std::string::npos) {
        return;
    }
    const std::size_t close_brace = src.find('}', open_brace + 1);
    if (close_brace == std::string::npos) {
        return;
    }
    const std::size_t semi = src.find(';', close_brace + 1);
    if (semi == std::string::npos) {
        return;
    }

    const std::string instance_name(
            trim_view(std::string_view(src).substr(close_brace + 1, semi - (close_brace + 1))));
    const std::string body(src.substr(open_brace + 1, close_brace - (open_brace + 1)));

    std::string uniforms_decl;
    std::vector<std::string> matrix_uniforms;

    std::size_t field_start = 0;
    while (field_start < body.size()) {
        const std::size_t field_semi = body.find(';', field_start);
        if (field_semi == std::string::npos) {
            break;
        }
        const std::string_view field = trim_view(std::string_view(body).substr(field_start, field_semi - field_start));
        field_start = field_semi + 1;
        if (field.empty()) {
            continue;
        }

        // Split `<type> <name_and_array>`
        std::size_t space = 0;
        while (space < field.size() && std::isspace(static_cast<unsigned char>(field[space])) == 0) {
            ++space;
        }
        const std::string_view raw_type = field.substr(0, space);
        const std::string_view rest = trim_view(field.substr(space));
        if (raw_type.empty() || rest.empty()) {
            continue;
        }

        std::string_view glsl_type = raw_type;
        bool is_matrix = false;
        if (raw_type == "mat4x4" || raw_type == "mat4") {
            glsl_type = "mat4";
            is_matrix = true;
        } else if (raw_type == "mat3x3" || raw_type == "mat3") {
            glsl_type = "mat3";
            is_matrix = true;
        } else if (raw_type == "mat2x2" || raw_type == "mat2") {
            glsl_type = "mat2";
            is_matrix = true;
        }

        if (is_matrix) {
            const std::size_t bracket = rest.find('[');
            const std::string_view mat_name = trim_view(rest.substr(0, bracket));
            if (!mat_name.empty()) {
                matrix_uniforms.emplace_back(mat_name);
            }
        }

        uniforms_decl.append("uniform ");
        uniforms_decl.append(glsl_type);
        uniforms_decl.push_back(' ');
        uniforms_decl.append(rest);
        uniforms_decl.append(";\n");
    }

    std::size_t decl_end = semi + 1;
    while (decl_end < src.size() && (src[decl_end] == '\r' || src[decl_end] == '\n')) {
        ++decl_end;
    }
    src.replace(decl_start, decl_end - decl_start, uniforms_decl);

    if (!instance_name.empty()) {
        for (const std::string& mat : matrix_uniforms) {
            const std::string qualified = instance_name + "." + mat;
            const std::string transposed = "transpose(" + mat + ")";
            replace_identifier(src, qualified, transposed);
        }
        const std::string prefix = instance_name + ".";
        replace_all(src, prefix, "");
    }
}

void strip_binding_layouts(std::string& src) {
    std::size_t pos = 0;
    while ((pos = src.find("layout(binding", pos)) != std::string::npos) {
        const std::size_t close = src.find(')', pos);
        if (close == std::string::npos) {
            break;
        }
        std::size_t end = close + 1;
        while (end < src.size() && std::isspace(static_cast<unsigned char>(src[end])) != 0) {
            ++end;
        }
        src.erase(pos, end - pos);
    }
}

void rewrite_stage_io_for_glsl_330(std::string& src, bool is_fragment) {
    std::vector<std::pair<std::string, std::string>> renames;

    std::size_t pos = 0;
    while ((pos = src.find("layout(location", pos)) != std::string::npos) {
        const std::size_t eq = src.find('=', pos);
        const std::size_t close = src.find(')', pos);
        if (eq == std::string::npos || close == std::string::npos || eq > close) {
            pos += 15;
            continue;
        }
        const std::string loc_str(trim_view(std::string_view(src).substr(eq + 1, close - (eq + 1))));

        std::size_t qual_start = close + 1;
        while (qual_start < src.size() && std::isspace(static_cast<unsigned char>(src[qual_start])) != 0) {
            ++qual_start;
        }

        bool is_in = false;
        bool is_out = false;
        std::size_t type_start = qual_start;
        if (src.compare(qual_start, 3, "in ") == 0) {
            is_in = true;
            type_start = qual_start + 3;
        } else if (src.compare(qual_start, 4, "out ") == 0) {
            is_out = true;
            type_start = qual_start + 4;
        } else {
            pos = close + 1;
            continue;
        }

        const std::size_t semi = src.find(';', type_start);
        if (semi == std::string::npos) {
            break;
        }

        const std::string_view type_and_name =
                trim_view(std::string_view(src).substr(type_start, semi - type_start));
        std::size_t last_space = type_and_name.size();
        while (last_space > 0 && std::isspace(static_cast<unsigned char>(type_and_name[last_space - 1])) == 0) {
            --last_space;
        }
        const std::string var_name(trim_view(type_and_name.substr(last_space)));
        if (var_name.empty()) {
            pos = semi + 1;
            continue;
        }

        if (!is_fragment && is_in) {
            // Vertex attribute input: keep `layout(location = N) in`, clean `input_<Name>_0` -> `<Name>`.
            if (var_name.starts_with("input_") && var_name.ends_with("_0") && var_name.size() > 8) {
                const std::string clean = var_name.substr(6, var_name.size() - 8);
                renames.emplace_back(var_name, clean);
            }
            pos = semi + 1;
        } else if ((!is_fragment && is_out) || (is_fragment && is_in)) {
            // Stage varying: GLSL 330 core forbids `layout(location = N)` on stage varyings and matches by name.
            const std::string varying_name = "_slang_v" + loc_str;
            renames.emplace_back(var_name, varying_name);
            src.erase(pos, qual_start - pos);
            pos = pos + (semi - qual_start) + 1;
        } else {
            // Fragment output: keep `layout(location = N) out`.
            pos = semi + 1;
        }
    }

    for (const auto& [from, to] : renames) {
        replace_identifier(src, from, to);
    }
}

std::string lower_slang_glsl_to_330(std::string_view slang_glsl, bool is_fragment) {
    std::string src(slang_glsl);
    replace_all(src, "#version 450", "#version 330 core");
    replace_all(src, "layout(row_major) uniform;\n", "");
    replace_all(src, "layout(row_major) uniform;\r\n", "");
    replace_all(src, "layout(row_major) buffer;\n", "");
    replace_all(src, "layout(row_major) buffer;\r\n", "");
    replace_all(src, "layout(column_major) uniform;\n", "");
    replace_all(src, "layout(column_major) uniform;\r\n", "");
    replace_all(src, "layout(column_major) buffer;\n", "");
    replace_all(src, "layout(column_major) buffer;\r\n", "");

    unwrap_global_params_ubo(src);
    strip_binding_layouts(src);
    rewrite_stage_io_for_glsl_330(src, is_fragment);
    return src;
}

#ifdef ENGINE_WITH_SLANG

std::string blob_to_string(slang::IBlob* blob) {
    if (!blob || !blob->getBufferPointer() || blob->getBufferSize() == 0) {
        return {};
    }
    const auto* ptr = static_cast<const char*>(blob->getBufferPointer());
    std::size_t size = blob->getBufferSize();
    while (size > 0 && ptr[size - 1] == '\0') {
        --size;
    }
    return std::string(ptr, size);
}

std::vector<std::uint32_t> blob_to_spirv(slang::IBlob* blob) {
    if (!blob || !blob->getBufferPointer() || blob->getBufferSize() == 0) {
        return {};
    }
    const std::size_t byte_size = blob->getBufferSize();
    const std::size_t word_count = byte_size / sizeof(std::uint32_t);
    std::vector<std::uint32_t> words(word_count);
    std::memcpy(words.data(), blob->getBufferPointer(), word_count * sizeof(std::uint32_t));
    return words;
}

slang::IGlobalSession* global_slang_session() {
    static Slang::ComPtr<slang::IGlobalSession> session;
    if (!session) {
        slang::createGlobalSession(session.writeRef());
    }
    return session.get();
}

std::mutex& slang_mutex() {
    static std::mutex mutex;
    return mutex;
}

#endif

} // namespace

std::expected<ShaderDesc, std::string> compile_slang_shader(
        std::string_view slang_source,
        std::string_view vertex_entry,
        std::string_view fragment_entry) {
#ifndef ENGINE_WITH_SLANG
    (void) slang_source;
    (void) vertex_entry;
    (void) fragment_entry;
    return std::unexpected("Slang compiler is not available in this build");
#else
    if (slang_source.empty()) {
        return std::unexpected("Slang shader source is empty");
    }

    std::lock_guard<std::mutex> lock(slang_mutex());
    slang::IGlobalSession* global_session = global_slang_session();
    if (!global_session) {
        return std::unexpected("Failed to initialize Slang global session");
    }

    slang::CompilerOptionEntry options[1]{};
    options[0].name = slang::CompilerOptionName::NoMangle;
    options[0].value.kind = slang::CompilerOptionValueKind::Int;
    options[0].value.intValue0 = 1;

    slang::TargetDesc targets[2]{};
    targets[0].format = SLANG_SPIRV;
    targets[0].profile = global_session->findProfile("spirv_1_5");
    targets[0].lineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_NONE;

    targets[1].format = SLANG_GLSL;
    targets[1].profile = global_session->findProfile("glsl_450");
    targets[1].lineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_NONE;

    slang::SessionDesc session_desc{};
    session_desc.targets = targets;
    session_desc.targetCount = 2;
    session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    session_desc.compilerOptionEntries = options;
    session_desc.compilerOptionEntryCount = 1;

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef())) || !session) {
        return std::unexpected("Failed to create Slang compile session");
    }

    const std::string source_str(slang_source);
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = session->loadModuleFromSourceString(
            "wind_shader", "wind_shader.slang", source_str.c_str(), diagnostics.writeRef());
    if (!module) {
        std::string err = blob_to_string(diagnostics.get());
        if (err.empty()) {
            err = "Failed to load Slang shader module";
        }
        return std::unexpected(std::move(err));
    }

    const std::string vs_entry_name(vertex_entry);
    Slang::ComPtr<slang::IEntryPoint> vs_entry;
    diagnostics.setNull();
    if (SLANG_FAILED(module->findAndCheckEntryPoint(
                vs_entry_name.c_str(), SLANG_STAGE_VERTEX, vs_entry.writeRef(), diagnostics.writeRef())) ||
            !vs_entry) {
        std::string err = blob_to_string(diagnostics.get());
        if (err.empty()) {
            err = "Vertex entry point '" + vs_entry_name + "' not found";
        }
        return std::unexpected(std::move(err));
    }

    const std::string fs_entry_name(fragment_entry);
    Slang::ComPtr<slang::IEntryPoint> fs_entry;
    diagnostics.setNull();
    if (SLANG_FAILED(module->findAndCheckEntryPoint(
                fs_entry_name.c_str(), SLANG_STAGE_FRAGMENT, fs_entry.writeRef(), diagnostics.writeRef())) ||
            !fs_entry) {
        std::string err = blob_to_string(diagnostics.get());
        if (err.empty()) {
            err = "Fragment entry point '" + fs_entry_name + "' not found";
        }
        return std::unexpected(std::move(err));
    }

    slang::IComponentType* components[3] = {module, vs_entry.get(), fs_entry.get()};
    Slang::ComPtr<slang::IComponentType> composed;
    diagnostics.setNull();
    if (SLANG_FAILED(session->createCompositeComponentType(
                components, 3, composed.writeRef(), diagnostics.writeRef())) ||
            !composed) {
        std::string err = blob_to_string(diagnostics.get());
        if (err.empty()) {
            err = "Failed to compose Slang shader components";
        }
        return std::unexpected(std::move(err));
    }

    Slang::ComPtr<slang::IComponentType> linked;
    diagnostics.setNull();
    if (SLANG_FAILED(composed->link(linked.writeRef(), diagnostics.writeRef())) || !linked) {
        std::string err = blob_to_string(diagnostics.get());
        if (err.empty()) {
            err = "Failed to link Slang shader program";
        }
        return std::unexpected(std::move(err));
    }

    Slang::ComPtr<slang::IBlob> vs_spirv_blob;
    diagnostics.setNull();
    if (SLANG_FAILED(linked->getEntryPointCode(0, 0, vs_spirv_blob.writeRef(), diagnostics.writeRef())) ||
            !vs_spirv_blob) {
        return std::unexpected("Failed to emit vertex SPIR-V: " + blob_to_string(diagnostics.get()));
    }

    Slang::ComPtr<slang::IBlob> fs_spirv_blob;
    diagnostics.setNull();
    if (SLANG_FAILED(linked->getEntryPointCode(1, 0, fs_spirv_blob.writeRef(), diagnostics.writeRef())) ||
            !fs_spirv_blob) {
        return std::unexpected("Failed to emit fragment SPIR-V: " + blob_to_string(diagnostics.get()));
    }

    Slang::ComPtr<slang::IBlob> vs_glsl_blob;
    diagnostics.setNull();
    if (SLANG_FAILED(linked->getEntryPointCode(0, 1, vs_glsl_blob.writeRef(), diagnostics.writeRef())) ||
            !vs_glsl_blob) {
        return std::unexpected("Failed to emit vertex GLSL: " + blob_to_string(diagnostics.get()));
    }

    Slang::ComPtr<slang::IBlob> fs_glsl_blob;
    diagnostics.setNull();
    if (SLANG_FAILED(linked->getEntryPointCode(1, 1, fs_glsl_blob.writeRef(), diagnostics.writeRef())) ||
            !fs_glsl_blob) {
        return std::unexpected("Failed to emit fragment GLSL: " + blob_to_string(diagnostics.get()));
    }

    ShaderDesc out;
    out.vertex_spirv = blob_to_spirv(vs_spirv_blob.get());
    out.fragment_spirv = blob_to_spirv(fs_spirv_blob.get());
    out.vertex_src = lower_slang_glsl_to_330(blob_to_string(vs_glsl_blob.get()), false);
    out.fragment_src = lower_slang_glsl_to_330(blob_to_string(fs_glsl_blob.get()), true);
    return out;
#endif
}

std::expected<ShaderDesc, std::string> compile_slang_shader_stages(
        std::string_view vertex_slang,
        std::string_view fragment_slang,
        std::string_view vertex_entry,
        std::string_view fragment_entry) {
    if (vertex_slang.empty() || fragment_slang.empty()) {
        return std::unexpected("Slang vertex or fragment stage source is empty");
    }
    std::string combined;
    combined.reserve(vertex_slang.size() + fragment_slang.size() + 2);
    combined.append(vertex_slang);
    combined.push_back('\n');
    combined.append(fragment_slang);
    return compile_slang_shader(combined, vertex_entry, fragment_entry);
}

} // namespace engine::render
