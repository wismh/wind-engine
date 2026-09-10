#include <gtest/gtest.h>

#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/bindable.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/command.h>
#include <engine/ui/document.h>
#include <engine/ui/paint.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

class RecordingFatalError final : public engine::IFatalError {
public:
    std::vector<std::string> messages;

    void report(std::string_view message) override { messages.emplace_back(message); }
};

// Registers `title` and `go`, but not the paint `world` nor the property `score` that kBrokenXml also names.
class PartialVm final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> title{"Hello"};
    engine::ui::RelayCommand go{[] {}};

    PartialVm() {
        property(engine::ui::intern("title"), title);
        command(engine::ui::intern("go"), go);
    }
};

constexpr std::string_view kBrokenXml = R"(<Canvas>
  <Component paint="{binding world}"/>
  <Label text="{binding score}"/>
  <Label text="{binding title}"/>
  <Button command="{binding go}" content="Go"/>
</Canvas>)";

}

TEST(UiBindError, MissingBindingDoesNotLeaveTheRestUnbound) {
    auto parsed = engine::ui::parse_xml(kBrokenXml);
    ASSERT_TRUE(parsed.has_value());
    PartialVm vm;
    RecordingFatalError fatal;

    const auto applied = engine::ui::apply_bindings(*parsed, vm, &fatal);

    ASSERT_FALSE(applied.has_value());
    EXPECT_EQ(applied.error(), engine::ui::UiError::MissingBinding);
    const auto& children = parsed->root.children;
    EXPECT_EQ(children[0].paint, nullptr);
    EXPECT_EQ(children[2].text, "Hello");
    EXPECT_EQ(children[3].command, &vm.go);
    ASSERT_EQ(fatal.messages.size(), 2u);
    EXPECT_EQ(fatal.messages[0], "UI paint binding \"world\" is not registered on the data context");
    EXPECT_EQ(fatal.messages[1], "UI text binding \"score\" is not registered on the data context");
}

TEST(UiBindError, BuilderDocumentNamesTheBindingById) {
    auto document = engine::ui::make_document(
            engine::ui::canvas().add(engine::ui::component().paint_bind(engine::ui::intern("world"))));
    ASSERT_TRUE(document.has_value());
    engine::ui::ViewModel vm;
    RecordingFatalError fatal;

    EXPECT_FALSE(engine::ui::apply_bindings(*document, vm, &fatal).has_value());

    ASSERT_EQ(fatal.messages.size(), 1u);
    EXPECT_NE(fatal.messages[0].find("UI paint binding #"), std::string::npos) << fatal.messages[0];
}

TEST(UiBindError, RunBindReportsEachMissingBindingOncePerCanvas) {
    engine::ecs::World world;
    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});

    auto parsed = engine::ui::parse_xml(kBrokenXml);
    ASSERT_TRUE(parsed.has_value());
    const auto vm = std::make_shared<PartialVm>();
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas;
    canvas.rect = {0.0f, 0.0f, 200.0f, 100.0f};
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    world.run(engine::ecs::Schedule::Frame);
    world.run(engine::ecs::Schedule::Frame);
    world.run(engine::ecs::Schedule::Frame);

    const engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    EXPECT_EQ(instance.reported_bind_errors.size(), 2u);
    EXPECT_TRUE(instance.reported_bind_errors.contains(
            "UI paint binding \"world\" is not registered on the data context"));
    EXPECT_EQ(instance.document.root.children[2].text, "Hello");
}
