#include "core/file_dialog_queue.h"

#include <engine/core/worlds.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>

#include <utility>

namespace engine {

FileDialogRequest FileDialogQueue::begin(WindowId owner) {
    const FileDialogRequest request{next_++};
    owners_.insert_or_assign(request, owner);
    return request;
}

void FileDialogQueue::complete(FileDialogRequest request, std::optional<std::filesystem::path> path) {
    const std::scoped_lock lock(mutex_);
    answers_.push_back(Answer{.request = request, .path = std::move(path)});
}

void FileDialogQueue::deliver(Worlds& worlds) {
    std::vector<Answer> answers;
    {
        const std::scoped_lock lock(mutex_);
        answers.swap(answers_);
    }
    for (Answer& answer : answers) {
        const auto owner = owners_.find(answer.request);
        if (owner == owners_.end()) {
            continue;
        }
        ecs::World* const world = worlds.world_for(owner->second);
        owners_.erase(owner);
        if (world == nullptr) {
            continue;
        }
        ecs::EventWriter<FileDialogResultEvent>{*world}.send(
                FileDialogResultEvent{.request = answer.request, .path = std::move(answer.path)});
    }
}

}
