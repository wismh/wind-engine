#include "directory_watch.h"

#include <utility>

namespace editor {

DirectoryWatch::~DirectoryWatch() {
    stop();
}

void DirectoryWatch::start(std::filesystem::path assets, std::atomic<bool>* dirty) {
    stop();
    stop_.store(false);
    dirty_ = dirty;
    if (!arm() || dirty_ == nullptr) {
        disarm();
        dirty_ = nullptr;
        return;
    }
    thread_ = std::thread([this, assets = std::move(assets)] { run(assets); });
}

void DirectoryWatch::stop() {
    if (thread_.joinable()) {
        stop_.store(true);
        wake();
        thread_.join();
    }
    disarm();
    dirty_ = nullptr;
}

}
