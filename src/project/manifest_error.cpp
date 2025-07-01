#include <engine/project/manifest_error.h>

namespace engine {

std::string_view to_string(ManifestError error) noexcept {
    switch (error) {
        case ManifestError::Missing:
            return "File not found";
        case ManifestError::Malformed:
            return "Not valid TOML";
        case ManifestError::MissingKey:
            return "Missing key";
        case ManifestError::WrongType:
            return "Wrong type";
    }
    return "Manifest error";
}

std::string describe(const ManifestFailure& failure) {
    return std::string(to_string(failure.kind)) + ": " + failure.detail;
}

}
