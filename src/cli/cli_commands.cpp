#include "cli/cli_server.h"

#include "ui/element_path.h"
#include "ui/painter.h"
#include "ui/profile.h"

#include <engine/ui/canvas.h>
#include <engine/ui/command.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace engine::cli {
    namespace {

        class Json {
        public:
            void begin_object() { begin('{'); }
            void end_object() { end('}'); }
            void begin_array() { begin('['); }
            void end_array() { end(']'); }

            void key(std::string_view name) {
                comma();
                raw_string(name);
                out_ += ':';
                suppress_ = true;
            }

            void string(std::string_view value) {
                comma();
                raw_string(value);
            }

            void boolean(bool value) {
                comma();
                out_ += value ? "true" : "false";
            }

            void null() {
                comma();
                out_ += "null";
            }

            void integer(std::int64_t value) {
                comma();
                out_ += std::to_string(value);
            }

            void number(double value) {
                comma();
                if (!std::isfinite(value)) {
                    out_ += "null";
                    return;
                }
                out_ += std::format("{:.6g}", value);
            }

            [[nodiscard]] std::string str() const { return out_; }

        private:
            void begin(char open) {
                comma();
                out_ += open;
                fresh_.push_back(true);
                suppress_ = false;
            }

            void end(char close) {
                out_ += close;
                if (!fresh_.empty()) {
                    fresh_.pop_back();
                }
                suppress_ = false;
            }

            void comma() {
                if (suppress_) {
                    suppress_ = false;
                    return;
                }
                if (!fresh_.empty() && !fresh_.back()) {
                    out_ += ',';
                }
                if (!fresh_.empty()) {
                    fresh_.back() = false;
                }
            }

            void raw_string(std::string_view value) {
                out_ += '"';
                for (const unsigned char c: value) {
                    switch (c) {
                        case '"':
                            out_ += "\\\"";
                            break;
                        case '\\':
                            out_ += "\\\\";
                            break;
                        case '\n':
                            out_ += "\\n";
                            break;
                        case '\r':
                            out_ += "\\r";
                            break;
                        case '\t':
                            out_ += "\\t";
                            break;
                        default:
                            if (c < 0x20) {
                                out_ += std::format("\\u{:04x}", static_cast<unsigned>(c));
                            } else {
                                out_ += static_cast<char>(c);
                            }
                            break;
                    }
                }
                out_ += '"';
            }

            std::string out_;
            std::vector<char> fresh_{true};
            bool suppress_ = false;
        };

        void skip_ws(std::string_view &in) {
            while (!in.empty() &&
                   (in.front() == ' ' || in.front() == '\t' || in.front() == '\n' || in.front() == '\r')) {
                in.remove_prefix(1);
            }
        }

        bool parse_string(std::string_view &in, std::string &out) {
            if (in.empty() || in.front() != '"') {
                return false;
            }
            in.remove_prefix(1);
            out.clear();
            while (!in.empty()) {
                const char c = in.front();
                in.remove_prefix(1);
                if (c == '"') {
                    return true;
                }
                if (c != '\\') {
                    out += c;
                    continue;
                }
                if (in.empty()) {
                    return false;
                }
                const char esc = in.front();
                in.remove_prefix(1);
                switch (esc) {
                    case '"':
                    case '\\':
                    case '/':
                        out += esc;
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u':
                        if (in.size() < 4) {
                            return false;
                        }
                        in.remove_prefix(4);
                        out += '?';
                        break;
                    default:
                        return false;
                }
            }
            return false;
        }

        bool parse_number(std::string_view &in, double &out) {
            const char *begin = in.data();
            const char *end = begin + in.size();
            auto [ptr, ec] = std::from_chars(begin, end, out);
            if (ec != std::errc{}) {
                return false;
            }
            in.remove_prefix(static_cast<std::size_t>(ptr - begin));
            return true;
        }

        bool skip_value(std::string_view &in) {
            skip_ws(in);
            if (in.empty()) {
                return false;
            }
            const char c = in.front();
            if (c == '"') {
                std::string ignored;
                return parse_string(in, ignored);
            }
            if (c == '-' || (c >= '0' && c <= '9')) {
                double ignored = 0.0;
                return parse_number(in, ignored);
            }
            if (in.starts_with("true")) {
                in.remove_prefix(4);
                return true;
            }
            if (in.starts_with("false")) {
                in.remove_prefix(5);
                return true;
            }
            if (in.starts_with("null")) {
                in.remove_prefix(4);
                return true;
            }
            if (c == '{' || c == '[') {
                const char open = c;
                const char close = c == '{' ? '}' : ']';
                int depth = 0;
                bool in_string = false;
                bool escape = false;
                for (std::size_t i = 0; i < in.size(); ++i) {
                    const char ch = in[i];
                    if (in_string) {
                        if (escape) {
                            escape = false;
                        } else if (ch == '\\') {
                            escape = true;
                        } else if (ch == '"') {
                            in_string = false;
                        }
                        continue;
                    }
                    if (ch == '"') {
                        in_string = true;
                        continue;
                    }
                    if (ch == open) {
                        ++depth;
                    } else if (ch == close) {
                        --depth;
                        if (depth == 0) {
                            in.remove_prefix(i + 1);
                            return true;
                        }
                    }
                }
                return false;
            }
            return false;
        }

        const char *kind_name(ui::ElementKind kind) {
            switch (kind) {
                case ui::ElementKind::Canvas:
                    return "Canvas";
                case ui::ElementKind::Stack:
                    return "Stack";
                case ui::ElementKind::Label:
                    return "Label";
                case ui::ElementKind::Button:
                    return "Button";
                case ui::ElementKind::Image:
                    return "Image";
                case ui::ElementKind::ItemsControl:
                    return "ItemsControl";
                case ui::ElementKind::ItemTemplate:
                    return "ItemTemplate";
                case ui::ElementKind::Line:
                    return "Line";
                case ui::ElementKind::Component:
                    return "Component";
                case ui::ElementKind::Viewport:
                    return "Viewport";
                case ui::ElementKind::TextInput:
                    return "TextInput";
                case ui::ElementKind::ScrollView:
                    return "ScrollView";
                case ui::ElementKind::Checkbox:
                    return "Checkbox";
                case ui::ElementKind::Math:
                    return "Math";
            }
            return "Element";
        }

        const char *position_name(ui::PositionMode mode) {
            switch (mode) {
                case ui::PositionMode::Static:
                    return "static";
                case ui::PositionMode::Relative:
                    return "relative";
                case ui::PositionMode::Absolute:
                    return "absolute";
            }
            return "static";
        }

        const char *overflow_name(ui::Overflow overflow) {
            switch (overflow) {
                case ui::Overflow::Visible:
                    return "visible";
                case ui::Overflow::Hidden:
                    return "hidden";
                case ui::Overflow::Scroll:
                    return "scroll";
                case ui::Overflow::Auto:
                    return "auto";
            }
            return "visible";
        }

        const char *align_name(ui::UiAlign align) {
            switch (align) {
                case ui::UiAlign::Start:
                    return "start";
                case ui::UiAlign::Center:
                    return "center";
                case ui::UiAlign::End:
                    return "end";
                case ui::UiAlign::SpaceBetween:
                    return "space-between";
            }
            return "start";
        }

        const char *motion_prop_name(ui::MotionProp prop) {
            switch (prop) {
                case ui::MotionProp::Color:
                    return "color";
                case ui::MotionProp::Background:
                    return "background";
                case ui::MotionProp::Opacity:
                    return "opacity";
                case ui::MotionProp::Visibility:
                    return "visibility";
                case ui::MotionProp::Display:
                    return "display";
                case ui::MotionProp::Width:
                    return "width";
                case ui::MotionProp::Height:
                    return "height";
                case ui::MotionProp::Padding:
                    return "padding";
                case ui::MotionProp::Margin:
                    return "margin";
                case ui::MotionProp::Transform:
                    return "transform";
                case ui::MotionProp::Stroke:
                    return "stroke";
                default:
                    return "property";
            }
        }

        void write_length(Json &json, const ui::Length &length) {
            if (!length.calc.empty()) {
                json.string("calc(...)");
                return;
            }
            const char *unit = "px";
            if (length.unit == ui::LengthUnit::Percent) {
                unit = "%";
            } else if (length.unit == ui::LengthUnit::Em) {
                unit = "em";
            }
            json.string(std::format("{:.4g}{}", length.value, unit));
        }

        void write_color(Json &json, glm::vec4 color) {
            json.begin_array();
            json.number(color.x);
            json.number(color.y);
            json.number(color.z);
            json.number(color.w);
            json.end_array();
        }

        void write_rect(Json &json, const render::Rect &rect) {
            json.begin_object();
            json.key("x");
            json.number(rect.x);
            json.key("y");
            json.number(rect.y);
            json.key("w");
            json.number(rect.w);
            json.key("h");
            json.number(rect.h);
            json.end_object();
        }

        void write_path(Json &json, const std::vector<std::size_t> &path) {
            json.begin_array();
            for (const std::size_t step: path) {
                json.integer(static_cast<std::int64_t>(step));
            }
            json.end_array();
        }

        struct Match {
            ecs::Entity canvas{};
            WindowId window = kPrimaryWindow;
            ui::UiCanvas *ui = nullptr;
            ui::UiInstance *instance = nullptr;
            ui::Element *element = nullptr;
            std::vector<std::size_t> path;
        };

        void walk_elements(ui::Element &element, std::vector<std::size_t> path,
                           const std::function<void(ui::Element &, const std::vector<std::size_t> &)> &visit) {
            visit(element, path);
            for (std::size_t i = 0; i < element.children.size(); ++i) {
                std::vector<std::size_t> child = path;
                child.push_back(i);
                walk_elements(element.children[i], std::move(child), visit);
            }
            for (std::size_t i = 0; i < element.generated_items.size(); ++i) {
                std::vector<std::size_t> child = path;
                child.push_back(i | ui::kGeneratedPathBit);
                walk_elements(element.generated_items[i], std::move(child), visit);
            }
        }

        struct CanvasRef {
            ecs::Entity entity{};
            WindowId window = kPrimaryWindow;
            int order = 0;
            std::uint32_t index = 0;
            ui::UiCanvas *ui = nullptr;
            ui::UiInstance *instance = nullptr;
        };

        std::vector<CanvasRef> canvases_on(ecs::World &world, WindowId window) {
            std::vector<CanvasRef> sources;
            auto view = world.view<ui::UiCanvas>();
            for (ecs::Entity entity: view) {
                ui::UiCanvas &canvas = view.get<ui::UiCanvas>(entity);
                if (canvas.window != window) {
                    continue;
                }
                ui::UiInstance *instance = world.try_get<ui::UiInstance>(entity);
                if (instance == nullptr) {
                    continue;
                }
                sources.push_back(CanvasRef{entity, canvas.window, canvas.order, entity.index, &canvas, instance});
            }
            std::stable_sort(sources.begin(), sources.end(), [](const CanvasRef &a, const CanvasRef &b) {
                if (a.order != b.order) {
                    return a.order < b.order;
                }
                return a.index < b.index;
            });
            return sources;
        }

        bool collect_ancestors(const ui::Element &current, const ui::Element &target,
                               std::vector<const ui::Element *> &ancestors) {
            if (&current == &target) {
                return true;
            }
            ancestors.push_back(&current);
            for (const ui::Element &child: current.children) {
                if (collect_ancestors(child, target, ancestors)) {
                    return true;
                }
            }
            for (const ui::Element &child: current.generated_items) {
                if (collect_ancestors(child, target, ancestors)) {
                    return true;
                }
            }
            ancestors.pop_back();
            return false;
        }

        void write_identity(Json &json, const Match &match) {
            json.key("window");
            json.integer(static_cast<std::int64_t>(static_cast<std::uint32_t>(match.window)));
            json.key("path");
            write_path(json, match.path);
            json.key("kind");
            json.string(kind_name(match.element->kind));
            json.key("id");
            json.string(match.element->id);
            json.key("classes");
            json.begin_array();
            for (const std::string &class_name: match.element->classes) {
                json.string(class_name);
            }
            json.end_array();
        }

        void write_rules(Json &json, ecs::World &world, const Match &match) {
            ui::Element &element = *match.element;
            std::vector<const ui::Element *> ancestors;
            collect_ancestors(match.instance->document.root, element, ancestors);
            const ui::Stylesheet *sheet = match.instance->stylesheet ? &*match.instance->stylesheet : nullptr;
            const ui::WindowSize size = ui::window_size_for(world, match.window);
            const std::vector<ui::MatchedRule> rules = ui::match_style_rules(
                    element, sheet, ancestors, static_cast<float>(size.width), static_cast<float>(size.height));
            json.key("rules");
            json.begin_array();
            for (std::size_t i = 0; i < rules.size(); ++i) {
                const ui::MatchedRule &rule = rules[i];
                json.begin_object();
                json.key("selector");
                json.string(rule.selector);
                json.key("specificity");
                json.integer(rule.specificity);
                json.key("winner");
                json.boolean(i + 1 == rules.size());
                json.key("declarations");
                json.begin_array();
                for (const ui::CssDeclaration &decl: rule.declarations) {
                    json.begin_object();
                    json.key("property");
                    json.string(decl.property);
                    json.key("value");
                    json.string(decl.value);
                    json.end_object();
                }
                json.end_array();
                json.end_object();
            }
            json.end_array();
        }

        void write_element(Json &json, ecs::World &world, const Match &match) {
            ui::Element &element = *match.element;
            json.begin_object();
            write_identity(json, match);
            json.key("text");
            json.string(element.text);
            json.key("display");
            json.string(element.display_none ? "none" : "shown");
            json.key("pseudo");
            json.begin_array();
            if (element.hovered) {
                json.string("hover");
            }
            if (element.pressed) {
                json.string("pressed");
            }
            if (element.disabled) {
                json.string("disabled");
            }
            if (element.focused) {
                json.string("focus");
            }
            if (element.checked) {
                json.string("checked");
            }
            json.end_array();

            const ui::LayoutBoxes boxes = ui::layout_boxes(match.instance->document.root, element);
            json.key("border");
            write_rect(json, boxes.border);
            json.key("margin");
            write_rect(json, boxes.margin);
            json.key("content");
            write_rect(json, boxes.content);

            if (element.style_cache_paint_.valid) {
                const ui::ComputedStyle &style = element.style_cache_paint_.style;
                json.key("computed");
                json.begin_object();
                json.key("color");
                write_color(json, style.color);
                json.key("background");
                write_color(json, style.background);
                json.key("opacity");
                json.number(style.opacity);
                json.key("font_size");
                write_length(json, style.font_size);
                if (style.width) {
                    json.key("width");
                    write_length(json, *style.width);
                }
                if (style.height) {
                    json.key("height");
                    write_length(json, *style.height);
                }
                json.key("padding");
                json.begin_array();
                write_length(json, style.padding.top);
                write_length(json, style.padding.right);
                write_length(json, style.padding.bottom);
                write_length(json, style.padding.left);
                json.end_array();
                json.key("margin");
                json.begin_array();
                write_length(json, style.margin.top);
                write_length(json, style.margin.right);
                write_length(json, style.margin.bottom);
                write_length(json, style.margin.left);
                json.end_array();
                json.key("z_index");
                json.integer(style.z_index);
                json.key("position");
                json.string(position_name(style.position));
                json.key("text_align");
                json.string(align_name(style.text_align));
                json.key("overflow_x");
                json.string(overflow_name(style.overflow_x));
                json.key("overflow_y");
                json.string(overflow_name(style.overflow_y));
                json.key("display");
                json.string(style.display_none ? "none" : "shown");
                json.key("visibility");
                json.string(style.visible ? "visible" : "hidden");
                json.key("border_width");
                write_length(json, style.border_width);
                json.key("border_color");
                write_color(json, style.border_color);
                json.end_object();
            } else {
                json.key("computed");
                json.null();
            }

            json.key("motion");
            json.begin_array();
            for (const ui::ShownMotion &shown: element.motion_shown) {
                if (!shown.running) {
                    continue;
                }
                json.begin_object();
                json.key("property");
                json.string(motion_prop_name(shown.prop));
                json.key("value");
                if (shown.prop == ui::MotionProp::Color || shown.prop == ui::MotionProp::Background ||
                    shown.prop == ui::MotionProp::BorderColor || shown.prop == ui::MotionProp::Stroke) {
                    write_color(json, shown.value.color);
                } else if (shown.prop == ui::MotionProp::Visibility || shown.prop == ui::MotionProp::Display) {
                    json.boolean(shown.value.flag);
                } else {
                    const float number = shown.value.px != 0.0f ? shown.value.px : shown.value.number;
                    json.number(number);
                }
                json.end_object();
            }
            json.end_array();

            json.key("bindings");
            json.begin_array();
            const auto bound = [&](const char *name, ui::BindingId id) {
                if (ui::is_bound(id)) {
                    json.string(name);
                }
            };
            bound("text", element.text_binding);
            bound("content", element.content_binding);
            bound("command", element.command_binding);
            bound("checked", element.checked_binding);
            bound("source", element.source_binding);
            bound("items", element.items_source_binding);
            bound("drag", element.drag_binding);
            bound("paint", element.paint_binding);
            bound("pan-x", element.pan_x_binding);
            bound("pan-y", element.pan_y_binding);
            bound("zoom", element.zoom_binding);
            bound("scroll-x", element.scroll_x_binding);
            bound("scroll-y", element.scroll_y_binding);
            json.end_array();
            write_rules(json, world, match);
            json.end_object();
        }

        std::string ok_element(ecs::World &world, const Match &match) {
            Json json;
            json.begin_object();
            json.key("ok");
            json.boolean(true);
            json.key("result");
            write_element(json, world, match);
            json.end_object();
            return json.str();
        }

        std::string ambiguous_json(ecs::World &world, const std::vector<Match> &matches) {
            Json json;
            json.begin_object();
            json.key("ok");
            json.boolean(false);
            json.key("error");
            json.string("ambiguous");
            json.key("candidates");
            json.begin_array();
            for (const Match &match: matches) {
                json.begin_object();
                write_identity(json, match);
                json.end_object();
            }
            json.end_array();
            json.end_object();
            (void) world;
            return json.str();
        }

        std::optional<std::vector<std::size_t>> parse_path(std::string_view selector, std::string &error) {
            if (!selector.starts_with("path:")) {
                return std::nullopt;
            }
            std::vector<std::size_t> path;
            std::string_view rest = selector.substr(5);
            if (rest.empty()) {
                return path;
            }
            while (!rest.empty()) {
                const std::size_t slash = rest.find('/');
                const std::string_view token = rest.substr(0, slash);
                std::size_t value = 0;
                const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
                if (ec != std::errc{} || ptr != token.data() + token.size()) {
                    error = "selector";
                    return std::vector<std::size_t>{};
                }
                path.push_back(value);
                if (slash == std::string_view::npos) {
                    break;
                }
                rest.remove_prefix(slash + 1);
            }
            return path;
        }

        std::vector<Match> resolve(ecs::World &world, const CliRequest &request, std::string &error) {
            std::vector<Match> matches;
            const std::vector<CanvasRef> sources = canvases_on(world, WindowId{request.window});
            if (request.selector.starts_with("path:")) {
                const std::optional<std::vector<std::size_t>> path = parse_path(request.selector, error);
                if (!error.empty() || !path) {
                    if (error.empty()) {
                        error = "selector";
                    }
                    return {};
                }
                for (const CanvasRef &source: sources) {
                    ui::Element *element = ui::resolve_element_path(source.instance->document.root, *path);
                    if (element == nullptr) {
                        continue;
                    }
                    matches.push_back(Match{source.entity, source.window, source.ui, source.instance, element, *path});
                }
                return matches;
            }

            const bool by_id = request.selector.starts_with('#');
            const bool by_class = request.selector.starts_with('.');
            if (!by_id && !by_class) {
                error = "selector";
                return {};
            }
            const std::string_view name = std::string_view{request.selector}.substr(1);
            if (name.empty()) {
                error = "selector";
                return {};
            }
            for (const CanvasRef &source: sources) {
                walk_elements(source.instance->document.root, {},
                              [&](ui::Element &element, const std::vector<std::size_t> &path) {
                                  bool hit = false;
                                  if (by_id) {
                                      hit = element.id == name;
                                  } else {
                                      hit = std::find(element.classes.begin(), element.classes.end(), name) !=
                                            element.classes.end();
                                  }
                                  if (hit) {
                                      matches.push_back(Match{source.entity, source.window, source.ui, source.instance,
                                                              &element, path});
                                  }
                              });
            }
            return matches;
        }

        std::string tree_json(ecs::World &world, WindowId window) {
            Json json;
            json.begin_object();
            json.key("ok");
            json.boolean(true);
            json.key("result");
            json.begin_object();
            json.key("nodes");
            json.begin_array();
            for (const CanvasRef &source: canvases_on(world, window)) {
                walk_elements(source.instance->document.root, {},
                              [&](ui::Element &element, const std::vector<std::size_t> &path) {
                                  Match match{source.entity, source.window, source.ui, source.instance, &element, path};
                                  json.begin_object();
                                  write_identity(json, match);
                                  json.key("display");
                                  json.string(element.display_none ? "none" : "shown");
                                  json.key("border");
                                  write_rect(json, ui::layout_boxes(source.instance->document.root, element).border);
                                  json.end_object();
                              });
            }
            json.end_array();
            json.end_object();
            json.end_object();
            return json.str();
        }

        std::string activate(const Match &match) {
            ui::Element &element = *match.element;
            if (element.disabled) {
                return "disabled";
            }
            bool did = false;
            if (element.kind == ui::ElementKind::Checkbox) {
                element.checked = !element.checked;
                if (ui::is_bound(element.checked_binding) && match.ui->data_context) {
                    ui::ViewModel *target =
                            element.generated_owner != nullptr
                                    ? static_cast<ui::ViewModel *>(const_cast<void *>(element.generated_owner))
                                    : match.ui->data_context.get();
                    target->write_property_float(element.checked_binding, element.checked ? 1.0f : 0.0f);
                }
                did = true;
            }
            if (element.kind != ui::ElementKind::TextInput) {
                ui::ICommand *command = element.command;
                if (command == nullptr && ui::is_bound(element.command_binding) && match.ui->data_context) {
                    command = match.ui->data_context->find_command(element.command_binding);
                }
                if (command == nullptr) {
                    return did ? "" : "no command";
                }
                if (!command->can_execute()) {
                    return did ? "" : "can_execute";
                }
                command->execute();
                return "";
            }
            return did ? "" : "no command";
        }

        CliResponse click_json(ecs::World &world, const Match &match, std::string_view reason) {
            Json json;
            json.begin_object();
            json.key("ok");
            json.boolean(true);
            json.key("result");
            json.begin_object();
            write_identity(json, match);
            json.key("executed");
            json.boolean(reason.empty());
            if (!reason.empty()) {
                json.key("reason");
                json.string(reason);
            }
            json.end_object();
            json.end_object();
            (void) world;
            return CliResponse{json.str(), false};
        }

        CliResponse hit_json(ecs::World &world, const CliRequest &request) {
            if (!request.has_x || !request.has_y) {
                return CliResponse{error_json("hit"), false};
            }
            const WindowId window{request.window};
            const std::vector<CanvasRef> sources = canvases_on(world, window);
            std::optional<CanvasRef> top;
            for (const CanvasRef &source: sources) {
                if (!ui::rect_contains(source.ui->rect, static_cast<float>(request.x), static_cast<float>(request.y))) {
                    continue;
                }
                if (!top || source.order > top->order || (source.order == top->order && source.index > top->index)) {
                    top = source;
                }
            }
            Json json;
            json.begin_object();
            json.key("ok");
            json.boolean(true);
            json.key("result");
            if (!top) {
                json.null();
                json.end_object();
                return CliResponse{json.str(), false};
            }
            const ui::UiCanvasSpace space =
                    ui::canvas_layout_space(top->ui->rect, top->ui->fit, top->ui->reference_size);
            const float local_x = (static_cast<float>(request.x) - space.offset.x) / space.scale;
            const float local_y = (static_cast<float>(request.y) - space.offset.y) / space.scale;
            const ui::VisualHit hit = ui::hit_test_visual(top->instance->document.root, local_x, local_y);
            if (hit.element == nullptr) {
                json.null();
                json.end_object();
                return CliResponse{json.str(), false};
            }
            Match match{top->entity, top->window, top->ui, top->instance, hit.element,
                        ui::find_element_path(top->instance->document.root, hit.element)};
            write_element(json, world, match);
            json.end_object();
            return CliResponse{json.str(), false};
        }

        CliResponse profile_response(ecs::World &world, const CliRequest &request) {
#if !defined(ENGINE_UI_PROFILER)
            (void) world;
            (void) request;
            return CliResponse{error_json("UI profiler is not in this build"), false};
#else
            if (request.stop) {
                ui::profiler_cli_set_capture(world, false);
                Json json;
                json.begin_object();
                json.key("ok");
                json.boolean(true);
                json.key("result");
                json.begin_object();
                json.key("capturing");
                json.boolean(false);
                json.end_object();
                json.end_object();
                return CliResponse{json.str(), false};
            }
            ui::profiler_cli_set_capture(world, true);
            if (!ui::profiler_cli_ready(world)) {
                return CliResponse{{}, true};
            }
            return CliResponse{std::string("{\"ok\":true,\"result\":") + ui::profiler_cli_json(world) + "}", false};
#endif
        }

    } // namespace

    std::string error_json(std::string_view message) {
        Json json;
        json.begin_object();
        json.key("ok");
        json.boolean(false);
        json.key("error");
        json.string(message);
        json.end_object();
        return json.str();
    }

    bool is_ui_command(std::string_view command) {
        return command == "tree" || command == "element" || command == "hit" || command == "click" ||
               command == "profile";
    }

    std::string execute_host(const CliCommands *host, const CliRequest &request) {
        if (!request.error.empty()) {
            return error_json(request.error);
        }
        if (host == nullptr || !host->handle) {
            return error_json("unknown command");
        }
        const std::optional<CliReply> reply = host->handle(CliCommand{request.command, request.path});
        if (!reply) {
            return error_json("unknown command");
        }
        if (!reply->ok) {
            return error_json(reply->error);
        }
        Json json;
        json.begin_object();
        json.key("ok");
        json.boolean(true);
        json.key("result");
        json.begin_object();
        for (const auto &[name, value]: reply->result) {
            json.key(name);
            std::visit(
                    [&json]<typename T>(const T &field) {
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            json.null();
                        } else if constexpr (std::is_same_v<T, bool>) {
                            json.boolean(field);
                        } else if constexpr (std::is_same_v<T, std::int64_t>) {
                            json.integer(field);
                        } else if constexpr (std::is_same_v<T, double>) {
                            json.number(field);
                        } else {
                            json.string(field);
                        }
                    },
                    value);
        }
        json.end_object();
        json.end_object();
        return json.str();
    }

    CliRequest parse_request(std::string_view body) {
        CliRequest request;
        skip_ws(body);
        if (body.empty() || body.front() != '{') {
            request.error = "invalid request";
            return request;
        }
        body.remove_prefix(1);
        while (true) {
            skip_ws(body);
            if (!body.empty() && body.front() == ',') {
                body.remove_prefix(1);
                continue;
            }
            if (!body.empty() && body.front() == '}') {
                break;
            }
            std::string key;
            if (!parse_string(body, key)) {
                request.error = "invalid request";
                return request;
            }
            skip_ws(body);
            if (body.empty() || body.front() != ':') {
                request.error = "invalid request";
                return request;
            }
            body.remove_prefix(1);
            skip_ws(body);
            if (key == "command" || key == "selector" || key == "path") {
                std::string value;
                if (!parse_string(body, value)) {
                    request.error = "invalid request";
                    return request;
                }
                if (key == "command") {
                    request.command = std::move(value);
                } else if (key == "selector") {
                    request.selector = std::move(value);
                } else {
                    request.path = std::move(value);
                }
                continue;
            }
            if (key == "window" || key == "x" || key == "y") {
                double value = 0.0;
                if (!parse_number(body, value) || !std::isfinite(value)) {
                    request.error = "invalid request";
                    return request;
                }
                if (key == "window") {
                    if (value < 0.0 || value > 4294967295.0 || std::floor(value) != value) {
                        request.error = "invalid request";
                        return request;
                    }
                    request.window = static_cast<std::uint32_t>(value);
                } else if (key == "x") {
                    request.x = value;
                    request.has_x = true;
                } else {
                    request.y = value;
                    request.has_y = true;
                }
                continue;
            }
            if (key == "stop") {
                if (body.starts_with("true")) {
                    body.remove_prefix(4);
                    request.stop = true;
                } else if (body.starts_with("false")) {
                    body.remove_prefix(5);
                    request.stop = false;
                } else {
                    request.error = "invalid request";
                    return request;
                }
                continue;
            }
            if (!skip_value(body)) {
                request.error = "invalid request";
                return request;
            }
        }
        return request;
    }

    CliResponse execute(ecs::World &world, const CliRequest &request) {
        if (!request.error.empty()) {
            return CliResponse{error_json(request.error), false};
        }
        if (request.command == "tree") {
            return CliResponse{tree_json(world, WindowId{request.window}), false};
        }
        if (request.command == "hit") {
            return hit_json(world, request);
        }
        if (request.command == "profile") {
            return profile_response(world, request);
        }
        if (request.command != "element" && request.command != "click") {
            return CliResponse{error_json("unknown command"), false};
        }
        std::string error;
        std::vector<Match> matches = resolve(world, request, error);
        if (!error.empty()) {
            return CliResponse{error_json(error), false};
        }
        if (matches.empty()) {
            return CliResponse{error_json("no element"), false};
        }
        if (matches.size() > 1) {
            return CliResponse{ambiguous_json(world, matches), false};
        }
        if (request.command == "element") {
            return CliResponse{ok_element(world, matches.front()), false};
        }
        const std::string reason = activate(matches.front());
        return click_json(world, matches.front(), reason);
    }

} // namespace engine::cli
