#include "fixtures/fake_services.h"
#include <engine/resources/assets_db.h>
#include <gtest/gtest.h>

#include "command_history.h"
#include "editor_selection.h"
#include "inspector_euler.h"
#include "inspector_panel.h"

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/sprite.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

// A game world with one canvas: Canvas > Button#go > Label#lab, and a rule for the label.
struct Game {
    Game() {
        engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = {800, 600};
        auto document = engine::ui::parse_xml(R"(<Canvas><Button id="go"><Label id="lab">Go</Label></Button></Canvas>)");
        EXPECT_TRUE(document.has_value());
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css("Label { color: #ffffff; }\n#go > Label { color: #ff0000; }\n", warnings);
        EXPECT_TRUE(sheet.has_value());
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {0.0f, 0.0f, 800.0f, 600.0f};
        entity = engine::ui::spawn_canvas(world, canvas, std::move(*document), std::move(*sheet));
        engine::ui::set_inspector_attached(world, true);
    }

    engine::ecs::World world;
    engine::ecs::Entity entity{};
};

// One text file in a directory of its own.
class TextFile {
public:
    explicit TextFile(std::string_view text)
        : dir_(std::filesystem::temp_directory_path() / "wind_inspector_panel_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        write(text);
    }

    ~TextFile() {
        std::error_code error;
        std::filesystem::remove_all(dir_, error);
    }

    TextFile(const TextFile&) = delete;
    TextFile& operator=(const TextFile&) = delete;

    void write(std::string_view text) const { std::ofstream(path(), std::ios::binary) << text; }

    [[nodiscard]] std::filesystem::path path() const { return dir_ / "notes.txt"; }

    [[nodiscard]] editor::AssetSelection selection() const {
        return editor::AssetSelection{.key = "notes.txt", .path = path(), .size = std::filesystem::file_size(path())};
    }

private:
    std::filesystem::path dir_;
};

std::vector<std::string> headings(const editor::InspectorViewModel& vm) {
    std::vector<std::string> out;
    for (const auto& section : vm.sections.get()) {
        out.push_back(section->heading.get());
    }
    return out;
}

std::vector<std::string> lines(const editor::InspectorSectionViewModel& section) {
    std::vector<std::string> out;
    for (const auto& line : section.lines.get()) {
        out.push_back(line->text.get());
    }
    return out;
}

std::shared_ptr<editor::InspectorFieldViewModel> field_named(
        const editor::InspectorSectionViewModel& section, std::string_view key) {
    for (const auto& field : section.fields.get()) {
        if (field->label.get() == key) {
            return field;
        }
    }
    return nullptr;
}

}

TEST(InspectorPanel, NothingSelectedSaysHowToSelect) {
    const editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.refresh();
    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.title.get(), "Nothing selected");
    EXPECT_NE(vm.subtitle.get().find("UI Tree"), std::string::npos);
    EXPECT_NE(vm.subtitle.get().find("Hierarchy"), std::string::npos);
    EXPECT_TRUE(vm.sections.get().empty());
}

TEST(InspectorPanel, AFileIsReadWhenSelectedAndAgainWhenSelectedAgain) {
    const TextFile file("first\nsecond\n");
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    selection.select(file.selection());
    panel.refresh();
    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.title.get(), "notes.txt");
    EXPECT_EQ(vm.subtitle.get(), "File");
    EXPECT_EQ(headings(vm), (std::vector<std::string>{"File", "Content"}));
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"first", "second"}));

    file.write("changed\n");
    panel.refresh();
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"first", "second"}))
            << "the disk is not read every frame";
    selection.select(file.selection());
    panel.refresh();
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"changed"}));
}

TEST(InspectorPanel, ACollapsedSectionStaysCollapsedForTheNextSelection) {
    const TextFile file("first\n");
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    selection.select(file.selection());
    panel.refresh();
    const editor::InspectorViewModel& vm = *panel.view_model();
    const std::shared_ptr<editor::InspectorSectionViewModel> content = vm.sections.get()[1];
    EXPECT_TRUE(content->expanded.get());

    content->toggle.execute();
    EXPECT_FALSE(vm.sections.get()[1]->expanded.get());
    EXPECT_TRUE(vm.sections.get()[1]->lines.get().empty());
    EXPECT_EQ(lines(*vm.sections.get()[0]).size(), 2u) << "only that section";

    selection.select(file.selection());
    panel.refresh();
    EXPECT_FALSE(vm.sections.get()[1]->expanded.get());
    EXPECT_EQ(vm.sections.get()[1], content) << "a section keeps its view-model";

    content->toggle.execute();
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"first"}));
}

TEST(InspectorPanel, AUiElementShowsItsComputedStyleAndRules) {
    Game game;
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.attach(game.world);
    engine::ui::inspector_select(game.world, engine::kPrimaryWindow,
            engine::ui::InspectorPick{.canvas = game.entity, .path = {0, 0}});
    selection.select(editor::UiElementSelection{});
    panel.refresh();

    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.title.get(), "Label #lab");
    EXPECT_EQ(vm.subtitle.get(), "UI element");
    EXPECT_EQ(headings(vm), (std::vector<std::string>{"Computed", "Rules"}));
    const std::vector<std::string> computed = lines(*vm.sections.get()[0]);
    EXPECT_NE(std::ranges::find(computed, "pseudo: (none)"), computed.end());
    const std::vector<std::string> rules = lines(*vm.sections.get()[1]);
    ASSERT_GE(rules.size(), 4u);
    EXPECT_EQ(rules.front().find("Label"), 0u) << rules.front();
    bool winner = false;
    for (const std::string& line : rules) {
        winner = winner || (line.find("#go > Label") != std::string::npos && line.find("winner") != std::string::npos);
    }
    EXPECT_TRUE(winner);

    // The probe moves to another element: the next refresh follows it without a new editor selection.
    engine::ui::inspector_select(game.world, engine::kPrimaryWindow,
            engine::ui::InspectorPick{.canvas = game.entity, .path = {0}});
    panel.refresh();
    EXPECT_EQ(vm.title.get(), "Button #go");
    EXPECT_EQ(lines(*vm.sections.get()[1]), (std::vector<std::string>{"No rule matches."}));

    panel.detach();
    EXPECT_FALSE(panel.attached());
    EXPECT_EQ(vm.title.get(), "Nothing selected");
    EXPECT_TRUE(vm.sections.get().empty());
}

TEST(InspectorPanel, AUiElementWithoutTheGameShowsNothing) {
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    selection.select(editor::UiElementSelection{});
    panel.refresh();
    EXPECT_EQ(panel.view_model()->title.get(), "Nothing selected");
}

TEST(InspectorPanel, DetachKeepsAFile) {
    const TextFile file("first\n");
    Game game;
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.attach(game.world);
    selection.select(file.selection());
    panel.refresh();
    panel.detach();
    EXPECT_EQ(panel.view_model()->title.get(), "notes.txt");
    EXPECT_EQ(headings(*panel.view_model()), (std::vector<std::string>{"File", "Content"}));
}

TEST(InspectorPanel, InspectsEntityInEditModeAndPlayModeIncludingUnknownComponents) {
    constexpr std::string_view kWorldToml = R"(
version = 1
active_camera = "hero"

[[entities]]
id = "squad"

[[entities]]
id = "hero"
parent = "squad"
[entities.transform]
position = [1.5, 2.0, 3.0]
scale = [1.0, 1.0, 1.0]
[entities.camera]
projection = "orthographic"
ortho_size = 12.0
[entities.sprite]
layer = 2
order_in_layer = 5
flip_x = true
[entities.health]
hp = 100
shield = 25.5
immortal = false
tags = ["player", "leader"]

[[entities]]
id = "weapon"
parent = "hero"
)";

    engine::ecs::World edit_world;
    const auto loaded = engine::ecs::load_world_from_toml(
            edit_world, kWorldToml, nullptr, nullptr, engine::ecs::UnknownComponentMode::Preserve);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    const engine::ecs::Entity hero = loaded->by_id.at("hero");

    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.set_edit_world(&edit_world);
    selection.select(editor::EntitySelection{.entity = hero});
    panel.refresh();

    const editor::InspectorViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.title.get(), "hero");
    EXPECT_EQ(vm.subtitle.get(), "Entity");
    EXPECT_EQ(headings(vm),
            (std::vector<std::string>{
                    "Entity",
                    "camera",
                    "sprite",
                    "transform",
                    "health (unregistered)",
            }));

    const std::vector<std::string> entity_lines = lines(*vm.sections.get()[0]);
    EXPECT_NE(std::ranges::find(entity_lines, "id: hero"), entity_lines.end());
    EXPECT_NE(std::ranges::find(entity_lines, "parent: squad"), entity_lines.end());
    EXPECT_NE(std::ranges::find(entity_lines, "children: 1"), entity_lines.end());
    EXPECT_NE(std::ranges::find(entity_lines, "active_camera: true"), entity_lines.end());

    const editor::InspectorSectionViewModel& camera = *vm.sections.get()[1];
    EXPECT_TRUE(lines(camera).empty());
    const std::shared_ptr<editor::InspectorFieldViewModel> projection = field_named(camera, "projection");
    const std::shared_ptr<editor::InspectorFieldViewModel> ortho = field_named(camera, "ortho_size");
    ASSERT_NE(projection, nullptr);
    ASSERT_NE(ortho, nullptr);
    EXPECT_EQ(projection->value.get(), "orthographic");
    EXPECT_EQ(projection->inputDisplay.get(), "flex");
    EXPECT_EQ(ortho->value.get(), "12");

    const editor::InspectorSectionViewModel& sprite = *vm.sections.get()[2];
    const std::shared_ptr<editor::InspectorFieldViewModel> layer = field_named(sprite, "layer");
    const std::shared_ptr<editor::InspectorFieldViewModel> order = field_named(sprite, "order_in_layer");
    const std::shared_ptr<editor::InspectorFieldViewModel> flip = field_named(sprite, "flip_x");
    ASSERT_NE(layer, nullptr);
    ASSERT_NE(order, nullptr);
    ASSERT_NE(flip, nullptr);
    EXPECT_EQ(layer->value.get(), "2");
    EXPECT_EQ(order->value.get(), "5");
    EXPECT_TRUE(flip->checked.get());
    EXPECT_EQ(flip->checkDisplay.get(), "flex");

    const editor::InspectorSectionViewModel& transform = *vm.sections.get()[3];
    const std::shared_ptr<editor::InspectorFieldViewModel> position = field_named(transform, "position");
    ASSERT_NE(position, nullptr);
    EXPECT_EQ(position->x.get(), "1.5");
    EXPECT_EQ(position->y.get(), "2");
    EXPECT_EQ(position->z.get(), "3");
    EXPECT_EQ(position->xDisplay.get(), "flex");
    EXPECT_EQ(position->wDisplay.get(), "none");
    EXPECT_EQ(vm.sections.get()[0]->removeDisplay.get(), "none");
    EXPECT_EQ(transform.removeDisplay.get(), "flex");
    EXPECT_EQ(vm.sections.get()[4]->removeDisplay.get(), "none");
    EXPECT_EQ(vm.addDisplay.get(), "flex");

    const std::vector<std::string> unknown_lines = lines(*vm.sections.get()[4]);
    EXPECT_NE(std::ranges::find(unknown_lines, "hp = 100"), unknown_lines.end());
    EXPECT_NE(std::ranges::find(unknown_lines, "shield = 25.5"), unknown_lines.end());
    EXPECT_NE(std::ranges::find(unknown_lines, "immortal = false"), unknown_lines.end());
    EXPECT_NE(std::ranges::find(unknown_lines, "tags = [player, leader]"), unknown_lines.end());

    // Play Mode: attaching a live game world inspects entities from that game world.
    engine::ecs::World game_world;
    const engine::ecs::Entity boss = game_world.create();
    game_world.emplace<engine::ecs::EntityName>(boss, engine::ecs::EntityName{.id = "boss"});
    game_world.emplace<engine::Transform>(boss, engine::Transform{.position = {10.0f, 20.0f, 0.0f}});

    panel.attach(game_world);
    selection.select(editor::EntitySelection{.entity = boss});
    panel.refresh();
    EXPECT_EQ(vm.title.get(), "boss");
    EXPECT_EQ(vm.subtitle.get(), "Entity");
    EXPECT_EQ(headings(vm), (std::vector<std::string>{"Entity", "transform"}));
    EXPECT_EQ(vm.addDisplay.get(), "none");
    EXPECT_EQ(vm.sections.get()[1]->removeDisplay.get(), "none");
    panel.edit_text("transform", "position", 0, "99");
    EXPECT_FLOAT_EQ(game_world.get<engine::Transform>(boss).position.x, 10.0f);

    // When the entity is destroyed, refreshing shows nothing selected.
    game_world.destroy(boss);
    panel.refresh();
    EXPECT_EQ(vm.title.get(), "Nothing selected");
}

TEST(InspectorPanel, ComponentEditsUndoAndRedo) {
    constexpr std::string_view kWorldToml = R"(
version = 1
[[entities]]
id = "hero"
transform = { position = [1.5, 2, 3] }
)";
    engine::ecs::World world;
    const auto loaded = engine::ecs::load_world_from_toml(
            world, kWorldToml, nullptr, nullptr, engine::ecs::UnknownComponentMode::Preserve);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    const engine::ecs::Entity hero = loaded->by_id.at("hero");

    editor::CommandHistory history;
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    int history_notes = 0;
    panel.set_edit_world(&world);
    panel.set_journal(&history, [&history_notes] { ++history_notes; });
    selection.select(editor::EntitySelection{.entity = hero});
    panel.refresh();

    const std::shared_ptr<editor::InspectorFieldViewModel> position =
            field_named(*panel.view_model()->sections.get()[1], "position");
    ASSERT_NE(position, nullptr);
    position->x = "4";
    position->x = "5";
    EXPECT_EQ(history_notes, 2);
    EXPECT_FLOAT_EQ(world.get<engine::Transform>(hero).position.x, 5.0f);
    EXPECT_TRUE(history.undo());
    panel.end_field_edit();
    panel.refresh();
    EXPECT_FLOAT_EQ(world.get<engine::Transform>(hero).position.x, 1.5f);
    EXPECT_EQ(field_named(*panel.view_model()->sections.get()[1], "position")->x.get(), "1.5");
    EXPECT_TRUE(history.redo());
    panel.end_field_edit();
    panel.refresh();
    EXPECT_FLOAT_EQ(world.get<engine::Transform>(hero).position.x, 5.0f);

    position->x = "abc";
    EXPECT_FLOAT_EQ(world.get<engine::Transform>(hero).position.x, 5.0f);

    panel.add_component("rigid_body_2d");
    EXPECT_NE(world.try_get<engine::RigidBody2D>(hero), nullptr);
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(world.try_get<engine::RigidBody2D>(hero), nullptr);
    EXPECT_TRUE(history.redo());
    EXPECT_NE(world.try_get<engine::RigidBody2D>(hero), nullptr);

    panel.remove_component("transform");
    EXPECT_EQ(world.try_get<engine::Transform>(hero), nullptr);
    EXPECT_TRUE(history.undo());
    const engine::Transform* restored = world.try_get<engine::Transform>(hero);
    ASSERT_NE(restored, nullptr);
    EXPECT_FLOAT_EQ(restored->position.x, 5.0f);
    EXPECT_FLOAT_EQ(restored->position.y, 2.0f);

    panel.remove_component("health (unregistered)");
    EXPECT_NE(world.try_get<engine::Transform>(hero), nullptr);
}

TEST(InspectorEuler, DegreesRoundTripThroughEulerXyz) {
    const glm::vec3 samples[] = {
            {0.0f, 0.0f, 0.0f},
            {90.0f, 0.0f, 0.0f},
            {0.0f, 90.0f, 0.0f},
            {0.0f, 0.0f, 90.0f},
            {10.0f, 20.0f, 30.0f},
            {-15.0f, 40.0f, -25.0f},
    };
    for (const glm::vec3& degrees : samples) {
        const glm::quat rotation = engine::euler_xyz(glm::radians(degrees));
        const glm::vec3 back = editor::euler_degrees_xyz(rotation);
        EXPECT_NEAR(back.x, degrees.x, 0.05f) << degrees.x << " " << degrees.y << " " << degrees.z;
        EXPECT_NEAR(back.y, degrees.y, 0.05f) << degrees.x << " " << degrees.y << " " << degrees.z;
        EXPECT_NEAR(back.z, degrees.z, 0.05f) << degrees.x << " " << degrees.y << " " << degrees.z;
    }
}

TEST(InspectorPanel, RotationEditsAsEulerAndColorKeepsAlpha) {
    engine::ecs::World world;
    const engine::ecs::Entity hero = world.create();
    world.emplace<engine::ecs::EntityName>(hero, engine::ecs::EntityName{.id = "hero"});
    world.emplace<engine::Transform>(hero);
    engine::render::Sprite sprite;
    sprite.color = {1.0f, 0.0f, 0.0f, 0.5f};
    world.emplace<engine::render::Sprite>(hero, std::move(sprite));

    editor::CommandHistory history;
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.set_edit_world(&world);
    panel.set_journal(&history, [] {});
    selection.select(editor::EntitySelection{.entity = hero});
    panel.refresh();

    const auto section_named = [&](std::string_view heading) -> const editor::InspectorSectionViewModel* {
        for (const auto& section : panel.view_model()->sections.get()) {
            if (section->heading.get() == heading) {
                return section.get();
            }
        }
        return nullptr;
    };
    const editor::InspectorSectionViewModel* transform = section_named("transform");
    const editor::InspectorSectionViewModel* sprite_section = section_named("sprite");
    ASSERT_NE(transform, nullptr);
    ASSERT_NE(sprite_section, nullptr);

    const std::shared_ptr<editor::InspectorFieldViewModel> rotation = field_named(*transform, "rotation");
    ASSERT_NE(rotation, nullptr);
    EXPECT_EQ(rotation->x.get(), "0");
    EXPECT_EQ(rotation->y.get(), "0");
    EXPECT_EQ(rotation->z.get(), "0");
    EXPECT_EQ(rotation->xDisplay.get(), "flex");
    EXPECT_EQ(rotation->wDisplay.get(), "none");

    const std::shared_ptr<editor::InspectorFieldViewModel> color = field_named(*sprite_section, "color");
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->x.get(), "1");
    EXPECT_EQ(color->y.get(), "0");
    EXPECT_EQ(color->z.get(), "0");
    EXPECT_EQ(color->wDisplay.get(), "none");

    panel.edit_text("transform", "rotation", 1, "90");
    const glm::quat turned = engine::euler_xyz(glm::radians(glm::vec3{0.0f, 90.0f, 0.0f}));
    const glm::quat stored = world.get<engine::Transform>(hero).rotation;
    EXPECT_NEAR(stored.x, turned.x, 1.0e-4f);
    EXPECT_NEAR(stored.y, turned.y, 1.0e-4f);
    EXPECT_NEAR(stored.z, turned.z, 1.0e-4f);
    EXPECT_NEAR(stored.w, turned.w, 1.0e-4f);

    panel.end_field_edit();
    panel.refresh();
    transform = section_named("transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(field_named(*transform, "rotation")->y.get(), "90");

    EXPECT_TRUE(history.undo());
    panel.refresh();
    const glm::quat identity = world.get<engine::Transform>(hero).rotation;
    EXPECT_NEAR(identity.w, 1.0f, 1.0e-4f);
    EXPECT_NEAR(identity.x, 0.0f, 1.0e-4f);
    transform = section_named("transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(field_named(*transform, "rotation")->y.get(), "0");

    panel.edit_text("sprite", "color", 1, "1");
    const engine::render::Sprite& tint = world.get<engine::render::Sprite>(hero);
    EXPECT_FLOAT_EQ(tint.color.x, 1.0f);
    EXPECT_FLOAT_EQ(tint.color.y, 1.0f);
    EXPECT_FLOAT_EQ(tint.color.z, 0.0f);
    EXPECT_FLOAT_EQ(tint.color.w, 0.5f);
}


TEST(InspectorPanel, InspectsAndEditsObjectFileWithUndoRedo) {
    const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "wind_inspector_obj_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const std::filesystem::path obj_path = dir / "sword.object";
    {
        std::ofstream out(obj_path, std::ios::binary);
        out << "type = \"sword\"\nname = \"blade\"\ncount = 3\nequipped = true\n";
    }

    editor::CommandHistory history;
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    int history_notes = 0;
    panel.set_journal(&history, [&history_notes] { ++history_notes; });

    const editor::AssetSelection asset_sel{
            .key = "assets/items/sword.object",
            .path = obj_path,
            .directory = false,
            .size = std::filesystem::file_size(obj_path),
    };

    selection.select(asset_sel);
    panel.refresh();

    EXPECT_EQ(panel.view_model()->title.get(), "sword.object");
    EXPECT_EQ(panel.view_model()->subtitle.get(), "Object");

    const auto section_named = [&](std::string_view heading) -> const editor::InspectorSectionViewModel* {
        for (const auto& section : panel.view_model()->sections.get()) {
            if (section->heading.get() == heading) {
                return section.get();
            }
        }
        return nullptr;
    };

    const editor::InspectorSectionViewModel* sword_sec = section_named("sword");
    ASSERT_NE(sword_sec, nullptr);



    const auto count_field = field_named(*sword_sec, "count");
    ASSERT_NE(count_field, nullptr);
    EXPECT_EQ(count_field->value.get(), "3");

    const auto name_field = field_named(*sword_sec, "name");
    ASSERT_NE(name_field, nullptr);
    EXPECT_EQ(name_field->value.get(), "blade");

    const auto eq_field = field_named(*sword_sec, "equipped");
    ASSERT_NE(eq_field, nullptr);
    EXPECT_TRUE(eq_field->checked.get());

    // Edit number field
    panel.edit_text("sword", "count", -1, "10");
    EXPECT_EQ(history_notes, 1);

    // Verify on disk
    std::string text_now;
    {
        std::ifstream in(obj_path, std::ios::binary);
        text_now.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    auto parsed = engine::ecs::parse_field_table(text_now);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed->at("count").as_int(), 10);

    // Edit bool field
    panel.edit_bool("sword", "equipped", false);
    EXPECT_EQ(history_notes, 2);

    {
        std::ifstream in(obj_path, std::ios::binary);
        text_now.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    parsed = engine::ecs::parse_field_table(text_now);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(*parsed->at("equipped").as_bool());

    // Undo bool edit
    EXPECT_TRUE(history.undo());
    {
        std::ifstream in(obj_path, std::ios::binary);
        text_now.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    parsed = engine::ecs::parse_field_table(text_now);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(*parsed->at("equipped").as_bool());

    // Undo text edit
    EXPECT_TRUE(history.undo());
    {
        std::ifstream in(obj_path, std::ios::binary);
        text_now.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    parsed = engine::ecs::parse_field_table(text_now);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed->at("count").as_int(), 3);

    // Redo text edit
    EXPECT_TRUE(history.redo());
    {
        std::ifstream in(obj_path, std::ios::binary);
        text_now.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    parsed = engine::ecs::parse_field_table(text_now);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed->at("count").as_int(), 10);

    std::filesystem::remove_all(dir);
}

TEST(InspectorPanel, HybridObjectSchemaPopulatesDefaultsAndEvictsAssetsDbCache) {
    const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "wind_inspector_hybrid_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    struct Weapon {
        std::string name = "steel";
        int count = 1;
        bool magical = false;
    };

    fakes::QuietFatal fatal;
    engine::AssetsDb db(fatal);
    db.register_object<Weapon>(
            "weapon",
            [](engine::ecs::ComponentReader& in, Weapon& w) {
                (void)in.field("name", w.name);
                (void)in.field("count", w.count);
                (void)in.field("magical", w.magical);
            },
            [](engine::ecs::ComponentWriter& out, const Weapon& w) {
                out.field("name", w.name);
                out.field("count", w.count);
                out.field("magical", w.magical);
            });

    // File only specifies name; count and magical should come from defaults!
    const std::filesystem::path obj_path = dir / "dagger.object";
    {
        std::ofstream out(obj_path, std::ios::binary);
        out << "type = \"weapon\"\nname = \"dagger\"\n";
    }

    editor::CommandHistory history;
    editor::EditorSelection selection;
    editor::InspectorPanel panel{selection};
    panel.set_assets(&db);
    panel.set_journal(&history, [] {});

    const editor::AssetSelection asset_sel{
            .key = "assets/items/dagger.object",
            .path = obj_path,
            .directory = false,
            .size = std::filesystem::file_size(obj_path),
    };

    selection.select(asset_sel);
    panel.refresh();

    const auto section_named = [&](std::string_view heading) -> const editor::InspectorSectionViewModel* {
        for (const auto& section : panel.view_model()->sections.get()) {
            if (section->heading.get() == heading) {
                return section.get();
            }
        }
        return nullptr;
    };

    const editor::InspectorSectionViewModel* weapon_sec = section_named("weapon");
    ASSERT_NE(weapon_sec, nullptr);



    // "name" came from file
    EXPECT_EQ(field_named(*weapon_sec, "name")->value.get(), "dagger");
    // "count" and "magical" populated from C++ default schema!
    EXPECT_EQ(field_named(*weapon_sec, "count")->value.get(), "1");
    EXPECT_FALSE(field_named(*weapon_sec, "magical")->checked.get());

    // Edit the defaulted field
    panel.edit_bool("weapon", "magical", true);

    std::string text_now;
    {
        std::ifstream in(obj_path, std::ios::binary);
        text_now.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    auto parsed = engine::ecs::parse_field_table(text_now);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(*parsed->at("magical").as_bool());
    EXPECT_EQ(*parsed->at("name").as_string(), "dagger");
    EXPECT_EQ(*parsed->at("count").as_int(), 1);

    std::filesystem::remove_all(dir);
}
