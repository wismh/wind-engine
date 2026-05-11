#include "process/line_splitter.h"

#include <utility>

namespace engine {
namespace {

void push_line(std::string line, std::vector<std::string>& lines) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    lines.push_back(std::move(line));
}

}

void LineSplitter::feed(std::string_view bytes, std::vector<std::string>& lines) {
    while (!bytes.empty()) {
        const std::size_t end = bytes.find('\n');
        if (end == std::string_view::npos) {
            partial_.append(bytes);
            return;
        }
        partial_.append(bytes.substr(0, end));
        push_line(std::exchange(partial_, {}), lines);
        bytes.remove_prefix(end + 1);
    }
}

void LineSplitter::finish(std::vector<std::string>& lines) {
    if (!partial_.empty()) {
        push_line(std::exchange(partial_, {}), lines);
    }
}

}
