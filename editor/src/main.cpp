#include "editor_app.h"
#include "editor_options.h"

#include <cstddef>
#include <span>

int main(int argc, char** argv) {
    const editor::EditorOptions options =
            editor::parse_editor_options(std::span<char* const>(argv + 1, static_cast<std::size_t>(argc - 1)));
    editor::EditorApp app;
    return app.run(options);
}
