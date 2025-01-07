#pragma once

#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/document.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace engine::ui {

// Imperative frontend for the same `Element` tree `parse_xml` produces. Layout, bind, paint, and
// hit-test are unchanged. Not a widget graph: `add` moves a child `Node`; do not keep `Element*`
// across sibling `add` calls (`vector` may reallocate).
//
// Style is still CSS (class / id / `var`). There is no inline `width`/`color` here — those fields
// on `Element` are cascade outputs and would be overwritten on the next layout.
class Node {
public:
    Node& with_id(std::string_view id);
    Node& with_class(std::string_view class_name);
    Node& with_name(std::string_view name);

    Node& text(std::string_view value);
    Node& text_bind(BindingId id);
    Node& content(std::string_view value);
    Node& content_bind(BindingId id);
    Node& command_bind(BindingId id);
    Node& paint_bind(BindingId id);
    Node& drag_bind(BindingId id);
    Node& pan_x_bind(BindingId id);
    Node& pan_y_bind(BindingId id);
    Node& zoom_bind(BindingId id);
    Node& drag_orientation(StackDirection orientation);
    Node& source(AssetId id);
    Node& source_bind(BindingId id);
    Node& items_source_bind(BindingId id);
    Node& var(std::string_view name, BindingId id);

    Node& direction(StackDirection direction);
    Node& gap(float px);
    Node& slice(LengthInsets insets);
    Node& stylesheet(AssetId id);

    Node& add(Node child);

    [[nodiscard]] Element take() &&;

private:
    friend Node canvas();
    friend Node stack();
    friend Node label();
    friend Node button();
    friend Node image();
    friend Node items_control();
    friend Node item_template();
    friend Node line();
    friend Node component();
    friend Node viewport();
    friend std::expected<UiDocument, UiError> make_document(Node root, IFatalError* fatal);

    explicit Node(ElementKind kind);

    Element element_{};
    std::optional<AssetId> stylesheet_;
};

[[nodiscard]] Node canvas();
[[nodiscard]] Node stack();
[[nodiscard]] Node label();
[[nodiscard]] Node button();
[[nodiscard]] Node image();
[[nodiscard]] Node items_control();
[[nodiscard]] Node item_template();
[[nodiscard]] Node line();
[[nodiscard]] Node component();
[[nodiscard]] Node viewport();

[[nodiscard]] std::expected<UiDocument, UiError> make_document(Node root, IFatalError* fatal = nullptr);

}
