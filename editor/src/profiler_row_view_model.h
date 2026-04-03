#pragma once

#include "method_command.h"

#include <engine/ecs/entity.h>
#include <engine/ui/bindable.h>
#include <engine/ui/profiler.h>
#include <engine/ui/view_model.h>

#include <string>

namespace editor {

class ProfilerPanel;

// One canvas in the profiler's list (`canvases` in assets/ui/profiler.xml). The entity is only handed
// back to the engine; the panel drops every row on Stop.
class ProfilerRowViewModel final : public engine::ui::ViewModel {
public:
    explicit ProfilerRowViewModel(ProfilerPanel& panel);

    ProfilerRowViewModel(const ProfilerRowViewModel&) = delete;
    ProfilerRowViewModel& operator=(const ProfilerRowViewModel&) = delete;

    void show(const engine::ui::ProfilerCanvas& canvas);
    [[nodiscard]] engine::ecs::Entity canvas() const;

    // The row button: profile this canvas.
    void select_row();

    engine::ui::Bindable<std::string> label;
    // Row background through `var-row`: the selected canvas is green.
    engine::ui::Bindable<std::string> rowFill;
    MethodCommand select;

private:
    ProfilerPanel* panel_;
    engine::ecs::Entity canvas_{};
};

}
