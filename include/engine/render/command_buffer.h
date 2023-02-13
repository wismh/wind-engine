#pragma once

#include <engine/render/commands.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace engine::render {

class CommandBuffer {
public:
    void push(Command command) {
        commands_.push_back(std::move(command));
    }

    void push(CmdDrawMesh command) {
        commands_.emplace_back(std::move(command));
    }

    void push(CmdDrawUI command) {
        commands_.emplace_back(std::move(command));
    }

    void clear() {
        commands_.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
        return commands_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return commands_.size();
    }

    [[nodiscard]] const Command& operator[](std::size_t index) const {
        return commands_[index];
    }

    [[nodiscard]] const std::vector<Command>& commands() const noexcept {
        return commands_;
    }

    [[nodiscard]] auto begin() const noexcept {
        return commands_.begin();
    }

    [[nodiscard]] auto end() const noexcept {
        return commands_.end();
    }

    template<typename Visitor>
    void execute(Visitor&& visitor) const {
        for (const Command& command : commands_) {
            std::visit(visitor, command);
        }
    }

private:
    std::vector<Command> commands_;
};

}
