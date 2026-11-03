#include "vulkan_spirv.h"

#include <algorithm>
#include <cstddef>

namespace engine::render {

namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203u;

constexpr std::uint16_t kOpName = 5;
constexpr std::uint16_t kOpMemberName = 6;
constexpr std::uint16_t kOpEntryPoint = 15;
constexpr std::uint16_t kOpTypeInt = 21;
constexpr std::uint16_t kOpTypeFloat = 22;
constexpr std::uint16_t kOpTypeVector = 23;
constexpr std::uint16_t kOpTypeMatrix = 24;
constexpr std::uint16_t kOpTypeImage = 25;
constexpr std::uint16_t kOpTypeSampler = 26;
constexpr std::uint16_t kOpTypeSampledImage = 27;
constexpr std::uint16_t kOpTypeArray = 28;
constexpr std::uint16_t kOpTypeStruct = 30;
constexpr std::uint16_t kOpTypePointer = 32;
constexpr std::uint16_t kOpConstant = 43;
constexpr std::uint16_t kOpVariable = 59;
constexpr std::uint16_t kOpDecorate = 71;
constexpr std::uint16_t kOpMemberDecorate = 72;

constexpr std::uint32_t kStorageClassUniformConstant = 0;
constexpr std::uint32_t kStorageClassUniform = 2;

constexpr std::uint32_t kDecorationBlock = 2;
constexpr std::uint32_t kDecorationRowMajor = 4;
constexpr std::uint32_t kDecorationColMajor = 5;
constexpr std::uint32_t kDecorationArrayStride = 6;
constexpr std::uint32_t kDecorationMatrixStride = 7;
constexpr std::uint32_t kDecorationBinding = 33;
constexpr std::uint32_t kDecorationDescriptorSet = 34;
constexpr std::uint32_t kDecorationOffset = 35;

std::string read_spirv_string(std::span<const std::uint32_t> words) {
    std::string out;
    for (std::uint32_t word : words) {
        for (int b = 0; b < 4; ++b) {
            const char c = static_cast<char>((word >> (b * 8)) & 0xFFu);
            if (c == '\0') {
                return out;
            }
            out.push_back(c);
        }
    }
    return out;
}

struct TypeRecord {
    enum class Kind {
        Unknown,
        Scalar,
        Vector,
        Matrix,
        Array,
        Struct,
        Pointer,
        SamplerOrImage,
    };

    Kind kind = Kind::Unknown;
    std::uint32_t byte_size = 0;
    std::uint32_t elem_type = 0;
    std::uint32_t count = 0;
    std::uint32_t array_stride = 0;
    std::uint32_t storage_class = 0;
    bool is_block = false;
    // OpTypeImage's Arrayed operand (a sampler2DArray).
    bool arrayed = false;
    std::vector<std::uint32_t> member_types;
};

struct MemberDeco {
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t matrix_stride = 0;
    bool is_row_major = false;
    bool has_offset = false;
};

struct VarRecord {
    std::uint32_t pointer_type = 0;
    std::uint32_t storage_class = 0;
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    bool has_binding = false;
};

std::uint32_t compute_type_size(
    std::uint32_t type_id,
    const std::unordered_map<std::uint32_t, TypeRecord>& types,
    std::uint32_t matrix_stride) {
    const auto it = types.find(type_id);
    if (it == types.end()) {
        return 0;
    }
    const TypeRecord& t = it->second;
    switch (t.kind) {
    case TypeRecord::Kind::Scalar:
        return t.byte_size;
    case TypeRecord::Kind::Vector:
        return t.count * compute_type_size(t.elem_type, types, 0);
    case TypeRecord::Kind::Matrix: {
        const std::uint32_t stride =
            matrix_stride > 0 ? matrix_stride : compute_type_size(t.elem_type, types, 0);
        return t.count * stride;
    }
    case TypeRecord::Kind::Array: {
        const std::uint32_t stride =
            t.array_stride > 0 ? t.array_stride : compute_type_size(t.elem_type, types, matrix_stride);
        return t.count * stride;
    }
    default:
        return 0;
    }
}

void collect_struct_uniforms(
    std::uint32_t struct_id,
    std::uint32_t base_offset,
    const std::unordered_map<std::uint32_t, TypeRecord>& types,
    const std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, MemberDeco>>& struct_members,
    SpirvReflection& out) {
    const auto st_it = types.find(struct_id);
    if (st_it == types.end() || st_it->second.kind != TypeRecord::Kind::Struct) {
        return;
    }
    const auto decos_it = struct_members.find(struct_id);
    if (decos_it == struct_members.end()) {
        return;
    }
    const auto& member_types = st_it->second.member_types;
    const auto& decos = decos_it->second;

    for (std::uint32_t m_idx = 0; m_idx < member_types.size(); ++m_idx) {
        const auto d_it = decos.find(m_idx);
        const std::uint32_t rel_offset = (d_it != decos.end()) ? d_it->second.offset : 0u;
        std::uint32_t abs_offset = base_offset + rel_offset;
        std::uint32_t m_type_id = member_types[m_idx];

        if (const auto mt_it = types.find(m_type_id);
            mt_it != types.end() && mt_it->second.kind == TypeRecord::Kind::Struct) {
            bool unwrapped = false;
            if (mt_it->second.member_types.size() == 1 && d_it != decos.end() &&
                !d_it->second.name.empty()) {
                const std::uint32_t inner_type_id = mt_it->second.member_types[0];
                const auto inner_it = types.find(inner_type_id);
                if (inner_it != types.end() && inner_it->second.kind != TypeRecord::Kind::Struct) {
                    if (const auto inner_decos_it = struct_members.find(m_type_id);
                        inner_decos_it != struct_members.end()) {
                        if (const auto m0_it = inner_decos_it->second.find(0);
                            m0_it != inner_decos_it->second.end()) {
                            abs_offset += m0_it->second.offset;
                        }
                    }
                    m_type_id = inner_type_id;
                    unwrapped = true;
                }
            }
            if (!unwrapped) {
                collect_struct_uniforms(m_type_id, abs_offset, types, struct_members, out);
                continue;
            }
        }

        if (d_it == decos.end() || d_it->second.name.empty()) {
            continue;
        }
        const MemberDeco& d = d_it->second;
        const std::uint32_t m_size = compute_type_size(m_type_id, types, d.matrix_stride);

        SpirvUniformMember info{};
        info.name = d.name;
        info.offset = abs_offset;
        info.size = m_size;
        info.matrix_stride = d.matrix_stride;
        info.is_row_major = d.is_row_major;

        if (const auto mt_it = types.find(m_type_id); mt_it != types.end()) {
            if (mt_it->second.kind == TypeRecord::Kind::Matrix) {
                info.columns = mt_it->second.count;
                if (const auto vec_it = types.find(mt_it->second.elem_type); vec_it != types.end()) {
                    info.rows = vec_it->second.count;
                }
            } else if (mt_it->second.kind == TypeRecord::Kind::Array) {
                info.array_stride = mt_it->second.array_stride;
                info.array_elements = mt_it->second.count;
            }
        }

        out.ubo_size = std::max(out.ubo_size, info.offset + info.size);
        out.uniforms[info.name] = std::move(info);
    }
}

void reflect_stage(std::span<const std::uint32_t> spirv, SpirvReflection& out) {
    if (spirv.size() < 5 || spirv[0] != kSpirvMagic) {
        return;
    }

    std::unordered_map<std::uint32_t, TypeRecord> types;
    std::unordered_map<std::uint32_t, std::uint32_t> constants;
    std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, MemberDeco>> struct_members;
    std::unordered_map<std::uint32_t, VarRecord> variables;

    std::size_t idx = 5;
    while (idx < spirv.size()) {
        const std::uint32_t header = spirv[idx];
        const std::uint16_t opcode = static_cast<std::uint16_t>(header & 0xFFFFu);
        const std::uint16_t word_count = static_cast<std::uint16_t>(header >> 16);
        if (word_count == 0 || idx + word_count > spirv.size()) {
            break;
        }
        const std::span<const std::uint32_t> inst = spirv.subspan(idx, word_count);
        idx += word_count;

        switch (opcode) {
        case kOpMemberName:
            if (word_count >= 4) {
                struct_members[inst[1]][inst[2]].name = read_spirv_string(inst.subspan(3));
            }
            break;
        case kOpDecorate:
            if (word_count >= 3) {
                const std::uint32_t target = inst[1];
                const std::uint32_t deco = inst[2];
                if (deco == kDecorationBlock) {
                    types[target].is_block = true;
                } else if (deco == kDecorationArrayStride && word_count >= 4) {
                    types[target].array_stride = inst[3];
                } else if (deco == kDecorationBinding && word_count >= 4) {
                    variables[target].binding = inst[3];
                    variables[target].has_binding = true;
                } else if (deco == kDecorationDescriptorSet && word_count >= 4) {
                    variables[target].set = inst[3];
                }
            }
            break;
        case kOpMemberDecorate:
            if (word_count >= 4) {
                auto& m = struct_members[inst[1]][inst[2]];
                const std::uint32_t deco = inst[3];
                if (deco == kDecorationOffset && word_count >= 5) {
                    m.offset = inst[4];
                    m.has_offset = true;
                } else if (deco == kDecorationMatrixStride && word_count >= 5) {
                    m.matrix_stride = inst[4];
                } else if (deco == kDecorationRowMajor) {
                    m.is_row_major = true;
                } else if (deco == kDecorationColMajor) {
                    m.is_row_major = false;
                }
            }
            break;
        case kOpTypeInt:
        case kOpTypeFloat:
            if (word_count >= 3) {
                auto& t = types[inst[1]];
                t.kind = TypeRecord::Kind::Scalar;
                t.byte_size = inst[2] / 8u;
            }
            break;
        case kOpTypeVector:
            if (word_count >= 4) {
                auto& t = types[inst[1]];
                t.kind = TypeRecord::Kind::Vector;
                t.elem_type = inst[2];
                t.count = inst[3];
            }
            break;
        case kOpTypeMatrix:
            if (word_count >= 4) {
                auto& t = types[inst[1]];
                t.kind = TypeRecord::Kind::Matrix;
                t.elem_type = inst[2];
                t.count = inst[3];
            }
            break;
        case kOpTypeImage:
            // OpTypeImage result, sampled type, Dim, Depth, Arrayed, ...
            if (word_count >= 2) {
                types[inst[1]].kind = TypeRecord::Kind::SamplerOrImage;
                types[inst[1]].arrayed = word_count >= 6 && inst[5] != 0u;
            }
            break;
        case kOpTypeSampledImage:
            if (word_count >= 2) {
                types[inst[1]].kind = TypeRecord::Kind::SamplerOrImage;
                types[inst[1]].elem_type = word_count >= 3 ? inst[2] : 0u;
            }
            break;
        case kOpTypeSampler:
            if (word_count >= 2) {
                types[inst[1]].kind = TypeRecord::Kind::SamplerOrImage;
            }
            break;
        case kOpConstant:
            if (word_count >= 4) {
                constants[inst[2]] = inst[3];
            }
            break;
        case kOpTypeArray:
            if (word_count >= 4) {
                auto& t = types[inst[1]];
                t.kind = TypeRecord::Kind::Array;
                t.elem_type = inst[2];
                const auto cit = constants.find(inst[3]);
                t.count = cit != constants.end() ? cit->second : 1u;
            }
            break;
        case kOpTypeStruct:
            if (word_count >= 2) {
                auto& t = types[inst[1]];
                t.kind = TypeRecord::Kind::Struct;
                t.member_types.assign(inst.begin() + 2, inst.end());
            }
            break;
        case kOpTypePointer:
            if (word_count >= 4) {
                auto& t = types[inst[1]];
                t.kind = TypeRecord::Kind::Pointer;
                t.storage_class = inst[2];
                t.elem_type = inst[3];
            }
            break;
        case kOpVariable:
            if (word_count >= 4) {
                auto& v = variables[inst[2]];
                v.pointer_type = inst[1];
                v.storage_class = inst[3];
            }
            break;
        default:
            break;
        }
    }

    for (const auto& [var_id, var] : variables) {
        const auto ptr_it = types.find(var.pointer_type);
        if (ptr_it == types.end() || ptr_it->second.kind != TypeRecord::Kind::Pointer) {
            continue;
        }
        const std::uint32_t pointee_id = ptr_it->second.elem_type;
        const auto pointee_it = types.find(pointee_id);
        if (pointee_it == types.end()) {
            continue;
        }

        if (var.storage_class == kStorageClassUniform &&
            pointee_it->second.kind == TypeRecord::Kind::Struct) {
            out.has_ubo = true;
            if (var.has_binding) {
                out.ubo_set = var.set;
                out.ubo_binding = var.binding;
            }
            collect_struct_uniforms(pointee_id, 0u, types, struct_members, out);
        } else if (
            var.storage_class == kStorageClassUniformConstant &&
            pointee_it->second.kind == TypeRecord::Kind::SamplerOrImage) {
            out.has_sampler = true;
            bool arrayed = pointee_it->second.arrayed;
            if (const auto image_it = types.find(pointee_it->second.elem_type); image_it != types.end()) {
                arrayed = arrayed || image_it->second.arrayed;
            }
            out.sampler_arrayed = out.sampler_arrayed || arrayed;
            if (var.has_binding) {
                out.sampler_set = var.set;
                out.sampler_binding = var.binding;
            }
        }
    }

    if (out.ubo_size > 0) {
        out.ubo_size = (out.ubo_size + 15u) & ~15u;
    }
}

} // namespace

const SpirvUniformMember* SpirvReflection::find_uniform(std::string_view name) const noexcept {
    const auto it = uniforms.find(std::string(name));
    return it != uniforms.end() ? &it->second : nullptr;
}

std::string parse_spirv_entry_point(std::span<const std::uint32_t> spirv) {
    if (spirv.size() < 5 || spirv[0] != kSpirvMagic) {
        return "main";
    }
    std::size_t idx = 5;
    while (idx < spirv.size()) {
        const std::uint32_t header = spirv[idx];
        const std::uint16_t opcode = static_cast<std::uint16_t>(header & 0xFFFFu);
        const std::uint16_t word_count = static_cast<std::uint16_t>(header >> 16);
        if (word_count == 0 || idx + word_count > spirv.size()) {
            break;
        }
        if (opcode == kOpEntryPoint && word_count >= 4) {
            const std::string name = read_spirv_string(spirv.subspan(idx + 3, word_count - 3));
            if (!name.empty()) {
                return name;
            }
        }
        idx += word_count;
    }
    return "main";
}

SpirvReflection reflect_spirv_program(
    std::span<const std::uint32_t> vertex_spirv,
    std::span<const std::uint32_t> fragment_spirv) {
    SpirvReflection out;
    out.vertex_entry = parse_spirv_entry_point(vertex_spirv);
    out.fragment_entry = parse_spirv_entry_point(fragment_spirv);
    reflect_stage(vertex_spirv, out);
    reflect_stage(fragment_spirv, out);
    return out;
}

} // namespace engine::render
