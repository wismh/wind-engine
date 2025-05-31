#include <engine/core/build_info.h>

namespace engine {

std::string_view build_id() noexcept {
    return kBuildId;
}

}
