#pragma once

#include <engine/ui/document.h>

#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

struct TrParse {
    enum class Kind { NotTr, Ok, Error };

    Kind kind = Kind::NotTr;
    std::string key;
    std::vector<TrArg> args;
    // The `{binding path}` of each of `args`, in order.
    std::vector<std::string> arg_paths;
    std::string message;
};

// `{tr key}` or `{tr key name={binding path} ...}`. Anything else, including `{binding ...}`
// and a literal, is NotTr. A value that starts a `{tr` clause and is not that grammar is Error.
[[nodiscard]] TrParse parse_tr_attribute(std::string_view raw);

}
