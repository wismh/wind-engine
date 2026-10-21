#include "ui/dirty.h"
#include "ui/element_index.h"
#include "ui/painter.h"

#include <engine/loc/catalog.h>
#include <engine/ui/document.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// The bind pass (docs/tech/modules/UI.md#bindings). The first pass over a document, and any pass after the tree's
// structure or the data context changed, walks the tree, reads every binding, and records one `BindSlot` for each
// that can change. Every other pass compares each slot's property version with the one it saw and reads only the
// slots that moved. A read that finds a different value marks what the field feeds dirty (`mark_dirty`).

namespace engine::ui {

    namespace {

        constexpr int kVirtualizationOverscanRows = 2;

        std::int64_t tr_number(float n) {
            if (!std::isfinite(n)) {
                return 0;
            }
            // 2^53 is a power of two, so float can hold it, and it still fits in int64. Counts past it are
            // not meaningful plurals; clamping keeps the cast defined.
            constexpr float kLimit = 9007199254740992.f;
            if (n >= kLimit) {
                return static_cast<std::int64_t>(kLimit);
            }
            if (n <= -kLimit) {
                return -static_cast<std::int64_t>(kLimit);
            }
            return static_cast<std::int64_t>(n);
        }

        // One apply_bindings call. A failure is reported and remembered, and the work goes on: one missing binding
        // must not leave the rest of the document unbound.
        struct BindPass {
            UiDocument *document = nullptr;
            IBindErrorSink *errors = nullptr;
            const engine::loc::Catalog *catalog = nullptr;
            const Stylesheet *sheet = nullptr;
            // True while the pass records slots: a table build (the whole tree) or a new row's subtree.
            bool collecting = false;
            std::optional<UiError> first_error;
            // Set when the pass changed the tree's structure, so the slots of the dropped elements go.
            bool structure_changed = false;

            [[nodiscard]] BindTable &table() const { return document->bind_table; }

            // A missing string key is an error only for a caller that listens (an error sink); a missing binding always
            // is.
            [[nodiscard]] bool counts(UiError error) const {
                return error != UiError::MissingString || errors != nullptr;
            }

            void fail(UiError error, std::string message) {
                if (errors != nullptr) {
                    errors->bind_error(message);
                }
                if (counts(error) && !first_error) {
                    first_error = error;
                }
                if (collecting) {
                    table().failures.push_back(BindFailure{error, std::move(message)});
                }
            }

            [[nodiscard]] std::string name_of(BindingId binding) const {
                if (const auto path = document->binding_paths.find(binding); path != document->binding_paths.end()) {
                    return '"' + path->second + '"';
                }
                return std::format("#{:08x}", binding.value);
            }

            void fail_missing(std::string_view what, BindingId binding) {
                if (errors == nullptr && !collecting) {
                    fail(UiError::MissingBinding, {});
                    return;
                }
                fail(UiError::MissingBinding,
                     std::format("UI {} binding {} is not registered on the data context", what, name_of(binding)));
            }

            // The name is registered, but its Bindable holds a type this attribute cannot read.
            void fail_type(std::string_view what, std::string_view expected, BindingId binding) {
                if (errors == nullptr && !collecting) {
                    fail(UiError::MissingBinding, {});
                    return;
                }
                fail(UiError::MissingBinding,
                     std::format("UI {} binding {} must be {}", what, name_of(binding), expected));
            }

            void count_read() const { ++document->counters.bound_reads; }
        };

        void add_slot(BindPass &pass, const Element &element, BindField field, std::uint16_t index, BindingId binding,
                      ViewModel &source) {
            if (!pass.collecting) {
                return;
            }
            BindSlot slot;
            slot.element = element.handle;
            slot.field = field;
            slot.index = index;
            slot.binding = binding;
            slot.source = &source;
            slot.version = source.property_version(binding);
            slot.seen = slot.version ? slot.version->read() : 0;
            slot.registration_seen = source.registration_version();
            pass.table().slots.push_back(slot);
        }

        // --- fields ----------------------------------------------------------------------------------------------
        // Each reads one binding into its element field and marks what changed. They report a missing or mistyped
        // property like the old walk did and are shared by the table build and a slot refresh.

        void bind_text(BindPass &pass, Element &element, ViewModel &vm, BindingId binding) {
            pass.count_read();
            if (vm.bind_property_string(binding, element.text) == ViewModel::StringBind::Changed) {
                mark_dirty(*pass.document, element, kDirtyLayout | kDirtyPaint);
            }
        }

        void bind_translation(BindPass &pass, Element &element, ViewModel &vm, bool in_template) {
            if (element.tr_key.empty() || in_template) {
                return;
            }
            pass.count_read();
            std::string text;
            const engine::loc::Catalog *catalog = pass.catalog;
            if (catalog == nullptr) {
                text = element.tr_key;
                if (pass.errors != nullptr || pass.collecting) {
                    pass.fail(UiError::MissingString, "missing string key \"" + element.tr_key + "\"");
                }
            } else {
                std::vector<std::string> held;
                std::vector<engine::loc::Arg> args;
                held.reserve(element.tr_args.size());
                args.reserve(element.tr_args.size());
                for (const TrArg &arg : element.tr_args) {
                    if (const auto number = vm.read_property_float(arg.binding)) {
                        args.push_back(engine::loc::Arg{arg.name, tr_number(*number)});
                    } else if (auto value = vm.read_property_string(arg.binding)) {
                        held.push_back(std::move(*value));
                        args.push_back(engine::loc::Arg{arg.name, std::string_view{held.back()}});
                    }
                }
                engine::loc::Translated translated = catalog->text(element.tr_key, args);
                if (translated.missing_from_source && (pass.errors != nullptr || pass.collecting)) {
                    pass.fail(UiError::MissingString, "missing string key \"" + element.tr_key + "\"");
                }
                text = std::move(translated.text);
            }
            if (element.text != text) {
                element.text = std::move(text);
                mark_dirty(*pass.document, element, kDirtyLayout | kDirtyPaint);
            }
        }

        void bind_source(BindPass &pass, Element &element, ViewModel &vm, bool in_template) {
            pass.count_read();
            if (const auto value = vm.read_property_asset_id(element.source_binding)) {
                if (!element.source || *element.source != *value) {
                    element.source = *value;
                    mark_dirty(*pass.document, element, kDirtyPaint);
                }
            } else if (!in_template) {
                pass.fail_missing("source (AssetId)", element.source_binding);
            }
        }

        void bind_slider_value(BindPass &pass, Element &element, ViewModel &vm, bool in_template) {
            // The value places the parts: `--fraction` (0..1) on the slider and both parts, for the stylesheet.
            pass.count_read();
            if (const auto value = vm.read_property_float(element.drag_binding)) {
                const std::string fraction = std::to_string(slider_fraction(element, *value));
                const auto set_fraction = [&](Element &target) {
                    std::string &slot = target.custom_properties["fraction"];
                    if (slot != fraction) {
                        slot = fraction;
                        mark_dirty(*pass.document, target, kDirtyStyle | kDirtyPaint, true);
                    }
                };
                set_fraction(element);
                for (Element &part : element.children) {
                    set_fraction(part);
                }
            } else if (!in_template) {
                pass.fail_type("value", "a Bindable<float> (or another number)", element.drag_binding);
            }
        }

        void bind_if(BindPass &pass, Element &element, ViewModel &vm, bool in_template) {
            pass.count_read();
            if (auto value = vm.read_property_float(element.if_binding)) {
                const bool hidden = *value == 0.0f;
                if (element.hidden_by_if != hidden) {
                    element.hidden_by_if = hidden;
                    // apply_layout_style folds it into display_none, so the element restyles and, being out of or back
                    // in the flow, its parent is laid out again.
                    mark_dirty(*pass.document, element, kDirtyStyle | kDirtyLayout | kDirtyPaint, true);
                }
            } else if (!in_template) {
                // The template itself is bound against the list's owner, not a row: its rows resolve the name.
                pass.fail_type("if", "a Bindable<bool> (or another number)", element.if_binding);
            }
        }

        void bind_camera(BindPass &pass, Element &element, ViewModel &vm, BindField field) {
            pass.count_read();
            switch (field) {
                case BindField::PanX:
                    if (auto value = vm.read_property_float(element.pan_x_binding); value && element.pan_x != *value) {
                        element.pan_x = *value;
                        mark_dirty(*pass.document, element, kDirtyFrame);
                    }
                    break;
                case BindField::PanY:
                    if (auto value = vm.read_property_float(element.pan_y_binding); value && element.pan_y != *value) {
                        element.pan_y = *value;
                        mark_dirty(*pass.document, element, kDirtyFrame);
                    }
                    break;
                default:
                    if (auto value = vm.read_property_float(element.zoom_binding)) {
                        const float zoom =
                                *value > 0.0f ? std::clamp(*value, kViewportMinZoom, kViewportMaxZoom) : 1.0f;
                        if (element.zoom != zoom) {
                            element.zoom = zoom;
                            mark_dirty(*pass.document, element, kDirtyFrame);
                        }
                    }
                    break;
            }
        }

        // Clamped to the extent the last layout found, as layout clamps it again: ItemsControl virtualization
        // reads scroll_y before this frame's layout, and a view-model that asks for "the end" with a value past
        // it would otherwise generate a window of rows past the last one. The view-model keeps its value, so a
        // list that grew is clamped to its new extent on the next frame.
        void bind_scroll(BindPass &pass, Element &element, ViewModel &vm, BindField field) {
            pass.count_read();
            const bool vertical = field == BindField::ScrollY;
            const BindingId binding = vertical ? element.scroll_y_binding : element.scroll_x_binding;
            if (auto value = vm.read_property_float(binding)) {
                float &scroll = vertical ? element.scroll_y : element.scroll_x;
                const float next = std::clamp(*value, 0.0f, vertical ? element.max_scroll_y : element.max_scroll_x);
                if (scroll != next) {
                    scroll = next;
                    mark_dirty(*pass.document, element, kDirtyFrame);
                    pass.document->windows_dirty = true;
                }
            }
        }

        void bind_checked(BindPass &pass, Element &element, ViewModel &vm) {
            pass.count_read();
            if (auto value = vm.read_property_float(element.checked_binding)) {
                set_pseudo(*pass.document, element, kPseudoChecked, *value != 0.0f, pass.sheet);
            }
        }

        void bind_open(BindPass &pass, Element &element, ViewModel &vm) {
            pass.count_read();
            if (auto value = vm.read_property_float(element.open_binding)) {
                const bool open = *value != 0.0f;
                if (element.open != open) {
                    element.open = open;
                    mark_dirty(*pass.document, element, kDirtyPaint);
                }
            }
        }

        void bind_custom_property(BindPass &pass, Element &element, ViewModel &vm, std::size_t index) {
            pass.count_read();
            const CustomPropertyBinding &custom = element.custom_property_bindings[index];
            const auto existing = element.custom_properties.find(custom.name);
            bool changed = false;
            if (existing == element.custom_properties.end()) {
                std::string value;
                if (vm.assign_property_string(custom.binding, value)) {
                    element.custom_properties.emplace(custom.name, std::move(value));
                    changed = true;
                }
            } else {
                const ViewModel::StringBind result = vm.bind_property_string(custom.binding, existing->second);
                if (result == ViewModel::StringBind::Missing) {
                    element.custom_properties.erase(existing);
                    changed = true;
                } else {
                    changed = result == ViewModel::StringBind::Changed;
                }
            }
            if (changed) {
                // var(--name) feeds a declaration that may be any property: the element restyles, and the geometry
                // it ends up with says whether layout has anything to do.
                mark_dirty(*pass.document, element, kDirtyStyle | kDirtyPaint, true);
            }
        }

        // A bound command's `disabled` follows `can_execute()`, except a TextInput's: Enter-to-submit must stay
        // typeable when the command is not yet executable (see bind_command).
        void set_disabled_from(BindPass &pass, Element &element, bool executable) {
            if (element.kind == ElementKind::TextInput) {
                return;
            }
            set_pseudo(*pass.document, element, kPseudoDisabled, !executable, pass.sheet);
        }

        void bind_command(BindPass &pass, Element &element, ViewModel &vm, BindSlot *slot) {
            ICommand *command = vm.find_command(element.command_binding);
            element.command = command;
            if (command == nullptr) {
                pass.fail_missing("command", element.command_binding);
                return;
            }
            // A TextInput commonly carries a command only for Enter-to-submit (canvas.cpp handle_key's
            // Return case). Disabling the field whenever that command cannot execute yet (for example
            // because this same field is still empty) makes it permanently untypeable: handle_text_input
            // and handle_key both bail out on a disabled element, so it can never receive the keystroke
            // that would make the command executable. Buttons still grey out; only TextInput is exempt.
            const bool executable = command->can_execute();
            if (slot != nullptr) {
                slot->executable = executable;
            }
            set_disabled_from(pass, element, executable);
        }

        void bind_paint(BindPass &pass, Element &element, ViewModel &vm) {
            IPaint *found = vm.find_paint(element.paint_binding);
            if (found != element.paint) {
                element.paint = found;
                mark_dirty(*pass.document, element, kDirtyPaint);
            }
            if (element.paint == nullptr) {
                pass.fail_missing("paint", element.paint_binding);
            }
        }

        // --- the whole tree ----------------------------------------------------------------------------------------

        void bind_element(BindPass &pass, Element &element, ViewModel &vm, bool in_template,
                          const Element *scroll_context);

        // False (and reported) when `binding` is set but `vm` has no such property: the caller skips it.
        bool require_property(BindPass &pass, ViewModel &vm, bool in_template, BindingId binding,
                              std::string_view what) {
            if (!is_bound(binding) || in_template || vm.has_property(binding)) {
                return true;
            }
            pass.fail_missing(what, binding);
            return false;
        }

        // A cloned row passes its item to descendants. An ItemTemplate is a blueprint: stamping it with the
        // outer row's view-model makes a nested list's TextInput and Checkbox write through that outer model,
        // where the property is not registered, so the keystroke never reaches the inner item.
        void inherit_owner(const Element &parent, Element &child) {
            if (parent.kind == ElementKind::ItemTemplate || child.kind == ElementKind::ItemTemplate) {
                return;
            }
            if (parent.generated_owner != nullptr && child.generated_owner == nullptr) {
                child.generated_owner = parent.generated_owner;
                child.generated_serial = parent.generated_serial;
            }
        }

        void clear_row_owner(Element &element) {
            element.generated_owner = nullptr;
            element.generated_serial = 0;
            for (Element &child : element.children) {
                clear_row_owner(child);
            }
        }

        // The ItemsControl: clones its template into `generated_items`, one row per item, and generates only the rows
        // near the visible part of its scroller when it can (see docs/tech/modules/UI.md's ItemsControl section).
        // `refresh` runs it from a slot, with the bound rows already carrying their slots.
        void reconcile_items(BindPass &pass, Element &element, ViewModel &vm, const Element *scroll_context);

        void bind_element(BindPass &pass, Element &element, ViewModel &vm, bool in_template,
                          const Element *scroll_context) {
            const bool has_text = require_property(pass, vm, in_template, element.text_binding, "text");
            const bool has_content = require_property(pass, vm, in_template, element.content_binding, "content");
            const bool has_source = require_property(pass, vm, in_template, element.source_binding, "source");
            const bool has_items_source =
                    require_property(pass, vm, in_template, element.items_source_binding, "items_source");
            const bool has_if = require_property(pass, vm, in_template, element.if_binding, "if");
            const bool has_drag = require_property(pass, vm, in_template, element.drag_binding,
                                                   element.kind == ElementKind::Slider ? "value" : "drag");
            (void) require_property(pass, vm, in_template, element.pan_x_binding, "pan-x");
            (void) require_property(pass, vm, in_template, element.pan_y_binding, "pan-y");
            (void) require_property(pass, vm, in_template, element.zoom_binding, "zoom");
            (void) require_property(pass, vm, in_template, element.checked_binding, "checked");
            (void) require_property(pass, vm, in_template, element.open_binding, "open");
            for (const CustomPropertyBinding &custom : element.custom_property_bindings) {
                (void) require_property(pass, vm, in_template, custom.binding, "var-" + custom.name);
            }
            if (!in_template) {
                for (const TrArg &arg : element.tr_args) {
                    (void) require_property(pass, vm, in_template, arg.binding, "{tr} argument");
                }
            }

            const bool slots = pass.collecting && !in_template;
            if (is_bound(element.command_binding) && !in_template) {
                BindSlot slot;
                bind_command(pass, element, vm, &slot);
                if (slots && element.command != nullptr) {
                    slot.element = element.handle;
                    slot.field = BindField::Command;
                    slot.binding = element.command_binding;
                    slot.source = &vm;
                    pass.table().slots.push_back(slot);
                }
            }
            if (is_bound(element.paint_binding) && !in_template) {
                bind_paint(pass, element, vm);
            }
            if (is_bound(element.text_binding) && has_text) {
                bind_text(pass, element, vm, element.text_binding);
                if (slots) {
                    add_slot(pass, element, BindField::Text, 0, element.text_binding, vm);
                }
            }
            if (is_bound(element.content_binding) && has_content) {
                bind_text(pass, element, vm, element.content_binding);
                if (slots) {
                    add_slot(pass, element, BindField::Content, 0, element.content_binding, vm);
                }
            }
            if (!element.tr_key.empty() && !in_template) {
                bind_translation(pass, element, vm, in_template);
                if (slots) {
                    for (std::size_t i = 0; i < element.tr_args.size(); ++i) {
                        add_slot(pass, element, BindField::TrArg, static_cast<std::uint16_t>(i),
                                 element.tr_args[i].binding, vm);
                    }
                }
            }
            if (is_bound(element.source_binding) && !in_template && has_source) {
                bind_source(pass, element, vm, in_template);
                if (slots) {
                    add_slot(pass, element, BindField::Source, 0, element.source_binding, vm);
                }
            }
            if (element.kind == ElementKind::Slider && is_bound(element.drag_binding) && has_drag) {
                bind_slider_value(pass, element, vm, in_template);
                if (slots) {
                    add_slot(pass, element, BindField::SliderValue, 0, element.drag_binding, vm);
                }
            }
            if (is_bound(element.if_binding) && has_if) {
                bind_if(pass, element, vm, in_template);
                if (slots) {
                    add_slot(pass, element, BindField::If, 0, element.if_binding, vm);
                }
            }
            const auto camera = [&](BindField field, BindingId binding) {
                if (!is_bound(binding)) {
                    return;
                }
                bind_camera(pass, element, vm, field);
                if (slots) {
                    add_slot(pass, element, field, 0, binding, vm);
                }
            };
            camera(BindField::PanX, element.pan_x_binding);
            camera(BindField::PanY, element.pan_y_binding);
            camera(BindField::Zoom, element.zoom_binding);
            const auto scroll = [&](BindField field, BindingId binding) {
                if (!is_bound(binding)) {
                    return;
                }
                bind_scroll(pass, element, vm, field);
                if (slots) {
                    add_slot(pass, element, field, 0, binding, vm);
                }
            };
            scroll(BindField::ScrollX, element.scroll_x_binding);
            scroll(BindField::ScrollY, element.scroll_y_binding);
            if (is_bound(element.checked_binding)) {
                bind_checked(pass, element, vm);
                if (slots) {
                    add_slot(pass, element, BindField::Checked, 0, element.checked_binding, vm);
                }
            }
            if (is_bound(element.open_binding)) {
                bind_open(pass, element, vm);
                if (slots) {
                    add_slot(pass, element, BindField::Open, 0, element.open_binding, vm);
                }
            }
            for (std::size_t i = 0; i < element.custom_property_bindings.size(); ++i) {
                bind_custom_property(pass, element, vm, i);
                if (slots) {
                    add_slot(pass, element, BindField::Var, static_cast<std::uint16_t>(i),
                             element.custom_property_bindings[i].binding, vm);
                }
            }

            const bool nested_template = in_template || element.kind == ElementKind::ItemTemplate;
            const Element *child_scroll_context = is_scrollable_y(element) ? &element : scroll_context;
            for (Element &child : element.children) {
                inherit_owner(element, child);
                bind_element(pass, child, vm, nested_template, child_scroll_context);
            }

            if (element.kind == ElementKind::ItemsControl && is_bound(element.items_source_binding) && !in_template &&
                has_items_source) {
                if (slots) {
                    add_slot(pass, element, BindField::ItemsSource, 0, element.items_source_binding, vm);
                }
                reconcile_items(pass, element, vm, scroll_context);
            }
        }

        // Reads the heights the rows of `element` measured in the last layout into its `row_heights`, and builds the
        // tops of every item from them. False while nothing was measured yet (the first bind: every row is generated,
        // then measured, and the window narrows on the next pass).
        bool measure_rows(Element &element, const std::vector<ViewModel *> &items) {
            if (!element.row_heights) {
                element.row_heights.emplace();
            }
            Element::RowHeights &heights = *element.row_heights;
            for (const Element &row : element.generated_items) {
                if (row.generated_owner == nullptr || row.is_virtualization_spacer || row.display_none ||
                    row.layout_rect.h <= 0.0f) {
                    continue;
                }
                const auto [it, inserted] = heights.measured.try_emplace(row.generated_owner, row.layout_rect.h);
                if (inserted) {
                    heights.measured_sum += row.layout_rect.h;
                    ++heights.epoch;
                } else if (it->second != row.layout_rect.h) {
                    heights.measured_sum += static_cast<double>(row.layout_rect.h) - it->second;
                    it->second = row.layout_rect.h;
                    ++heights.epoch;
                }
            }
            if (heights.measured.empty()) {
                return false;
            }
            const float gap = element.gap.value;
            const bool same_items = heights.items.size() == items.size() &&
                                    std::equal(items.begin(), items.end(), heights.items.begin(),
                                               [](const ViewModel *a, const void *b) { return a == b; });
            if (heights.tops_epoch == heights.epoch && heights.tops_gap == gap && same_items) {
                return true;
            }
            if (heights.measured.size() > items.size() * 2 + 64) {
                // Rows of items that left the list.
                std::unordered_map<const void *, float> kept;
                heights.measured_sum = 0.0;
                for (const ViewModel *item : items) {
                    if (const auto it = heights.measured.find(item); it != heights.measured.end()) {
                        kept.emplace(item, it->second);
                        heights.measured_sum += it->second;
                    }
                }
                heights.measured = std::move(kept);
                if (heights.measured.empty()) {
                    return false;
                }
            }
            const float estimate =
                    static_cast<float>(heights.measured_sum / static_cast<double>(heights.measured.size()));
            heights.items.assign(items.begin(), items.end());
            heights.tops.assign(items.size() + 1, 0.0f);
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto it = heights.measured.find(items[i]);
                heights.tops[i + 1] = heights.tops[i] + (it != heights.measured.end() ? it->second : estimate) + gap;
            }
            heights.tops_epoch = heights.epoch;
            heights.tops_gap = gap;
            return true;
        }

        void reconcile_items(BindPass &pass, Element &element, ViewModel &vm, const Element *scroll_context) {
            const Element *tmpl = nullptr;
            for (const Element &child : element.children) {
                if (child.kind == ElementKind::ItemTemplate) {
                    tmpl = &child;
                    break;
                }
            }
            if (tmpl == nullptr) {
                return;
            }
            UiDocument &document = *pass.document;
            ElementArena &arena = document.arena;
            const Element *child_scroll_context = is_scrollable_y(element) ? &element : scroll_context;
            const std::size_t expected_count = tmpl->children.empty() ? 1 : tmpl->children.size();

            // --- Virtualization eligibility -------------------------------------------------
            // Generate only the visible window of rows (+overscan) plus up to two spacer
            // Elements standing in for the scrolled-past rows, instead of one (or
            // `expected_count`) Element per item for every item in items_source — see
            // docs/tech/modules/UI.md's ItemsControl section. Every condition below must hold or
            // this frame falls back to `window_first = 0, window_last = items.size() - 1` with no
            // spacers: the historical, byte-identical full generation.
            // The scrolled viewport this ItemsControl's rows are windowed against: itself when
            // it is directly scrollable (the wind-127 self-scrolling case), otherwise the
            // nearest scrollable ancestor threaded down through `scroll_context` (e.g. an
            // enclosing <ScrollView> that isn't this control's direct parent). `direction ==
            // Vertical` is required on the ancestor too: scroll_y / layout_rect.y only line up
            // with the row axis when the scrolling container also stacks vertically.
            const Element *effective_context = is_scrollable_y(element) ? &element : scroll_context;
            const bool has_scroll_context = effective_context != nullptr &&
                                            effective_context->direction == StackDirection::Vertical &&
                                            effective_context->layout_rect.h > 0.0f;
            std::optional<float> row_height_px;
            bool rows_out_of_flow = false;
            bool rows_grow = false;
            const bool eligible = !element.suppress_item_virtualization && expected_count == 1 &&
                                  element.direction == StackDirection::Vertical && has_scroll_context &&
                                  element.gap.unit == LengthUnit::Px && element.gap.calc.empty();
            if (eligible) {
                // Row height can't be read off the static ItemTemplate: apply_layout_style
                // (paint.cpp) never visits an ItemTemplate's own children, so tmpl's root never
                // gets a resolved `height` there — only a *generated* clone does, once
                // apply_layout_style has walked it on a previous frame. Sample the first
                // surviving real (non-spacer) row from last frame's generated_items and, when
                // found, treat it as this frame's source of truth and refresh
                // `virtualization_row_height_cache` with it. When last frame's window held no
                // real row at all — every generated item was a spacer, or there were none yet —
                // fall back to the cached value from whenever it was last sampled, instead of
                // declaring row height unknown: a wrapping ScrollView can scroll this
                // ItemsControl entirely out of view for several frames in a row (visible window
                // = zero real rows, one spacer standing in for all of them), and a live sample
                // alone would never find anything then, tipping eligibility over to full
                // generation for as long as the control stays offscreen. Only when neither a
                // live sample nor a prior cached value exists (first bind ever for this control)
                // does row height stay unknown this frame — self-correcting next frame, same as
                // the max_scroll_y / layout_rect.h staleness this eligibility check already
                // relies on above.
                // Rows placed by `position: absolute` (an inventory laid out by var-left / var-top) are not
                // stacked, so a window computed from row height and scroll offset would drop most of them:
                // never virtualize those, and forget any height cached from an earlier template.
                for (const Element &old : element.generated_items) {
                    if (old.generated_owner != nullptr && !old.display_none && old.position == PositionMode::Absolute) {
                        rows_out_of_flow = true;
                        element.virtualization_row_height_cache.reset();
                        break;
                    }
                    // A row that grows into the viewport is not a fixed stride, so a window built from its
                    // CSS height would not match the laid-out rows. Sampled from the previous frame, like
                    // the height itself.
                    if (old.generated_owner != nullptr && !old.display_none && old.flex_grow > 0.0f) {
                        rows_grow = true;
                        element.virtualization_row_height_cache.reset();
                        break;
                    }
                    if (old.generated_owner != nullptr && !old.display_none && old.height &&
                        old.height->unit == LengthUnit::Px && old.height->calc.empty() && old.height->value > 0.0f) {
                        row_height_px = old.height->value;
                        element.virtualization_row_height_cache = row_height_px;
                        break;
                    }
                }
                if (!row_height_px && !rows_out_of_flow && !rows_grow && element.virtualization_row_height_cache) {
                    row_height_px = element.virtualization_row_height_cache;
                }
            }

            // Decide the window before moving any Element. When generated_items is already that
            // window, the rows are bound in place. A different set still reconciles by
            // ViewModel* below, so a surviving row keeps its motion clocks.
            pass.count_read();
            const std::vector<ViewModel *> items = vm.read_item_source(element.items_source_binding);
            std::size_t window_first = 0;
            std::size_t window_last = items.empty() ? 0 : items.size() - 1;
            float leading_spacer_h = 0.0f;
            float trailing_spacer_h = 0.0f;
            // True when the visible row window is empty: the ItemsControl sits entirely outside
            // effective_context's viewport (Case B only — see below). When true, no real rows
            // are generated this frame at all, just one spacer standing in for every item.
            bool window_empty = false;
            if (row_height_px && !items.empty()) {
                const float gap_px = element.gap.value;
                const float row_stride = *row_height_px + gap_px;
                const auto n = static_cast<std::int64_t>(items.size());

                // Case A (effective_context == &element, i.e. this ItemsControl scrolls itself):
                // offset == 0, so scroll_y_effective == element.scroll_y and viewport_h ==
                // element.layout_rect.h — byte-identical to the wind-127 formula. Case B
                // (effective_context is an ancestor, not necessarily the direct parent):
                // layout_rect is always absolute/canvas-space at every nesting depth (each
                // layout_element/layout_stack positions children from its own already-absolute
                // content origin), so this one subtraction is correct however many intermediate
                // Stacks/padding/margins sit between `element` and `effective_context`, without
                // needing to check whether effective_context is element's direct parent.
                const float offset = (effective_context == &element)
                                             ? 0.0f
                                             : element.layout_rect.y - effective_context->layout_rect.y;
                const float viewport_h = effective_context->layout_rect.h;
                const float scroll_y_effective = effective_context->scroll_y - offset;

                std::int64_t first = static_cast<std::int64_t>(std::floor(scroll_y_effective / row_stride)) -
                                     kVirtualizationOverscanRows;
                std::int64_t last =
                        static_cast<std::int64_t>(std::ceil((scroll_y_effective + viewport_h) / row_stride)) +
                        kVirtualizationOverscanRows;

                // layout_stack packs N flow children with exactly (N-1) gaps total (a gap
                // *between* consecutive children, never a trailing one after the last) — so a
                // spacer standing in for `count` skipped rows must itself contribute
                // `count * row_height + (count-1) * gap`, not `count * row_stride`: the gap
                // between the spacer and its neighboring real row is already accounted for by
                // layout_stack's own inter-child gap once the spacer takes its place as one flow
                // child among (spacers + window) total. Getting this wrong would make the
                // virtualized max_scroll_y drift from the max_scroll_y full generation would
                // produce by up to 2 * gap.
                const auto spacer_height = [&](std::size_t count) -> float {
                    if (count == 0) {
                        return 0.0f;
                    }
                    return static_cast<float>(count) * *row_height_px + static_cast<float>(count - 1) * gap_px;
                };

                if (last < 0 || first >= n) {
                    // Entirely offscreen: only reachable in Case B, where scroll_y_effective can
                    // legitimately land outside [0, N*row_stride) in either direction (e.g. the
                    // wrapping ScrollView shows content before or after this ItemsControl while
                    // the list itself is fully scrolled past). Case A can never hit this branch:
                    // element.scroll_y is always clamped to [0, element.max_scroll_y] by the
                    // element's own scrolling, which keeps the window over at least one real row.
                    // Collapse to zero real rows plus one spacer covering every item. Critical
                    // invariant (same one wind-127 established for the two-spacer case): the
                    // summed used-height of this ItemsControl (spacers + any real rows) must
                    // always equal N*row_height + (N-1)*gap regardless of window state, because
                    // that sum is exactly what effective_context's own intrinsic_size /
                    // compute_used reads back when laying out content that follows this
                    // ItemsControl (e.g. a footer) and when computing its own max_scroll_y.
                    // spacer_height(N) is that sum by construction for N > 0, so this case is
                    // trivially exact.
                    window_empty = true;
                    window_first = 0;
                    window_last = 0;
                    leading_spacer_h = spacer_height(static_cast<std::size_t>(n));
                    trailing_spacer_h = 0.0f;
                } else {
                    first = std::clamp<std::int64_t>(first, 0, n - 1);
                    last = std::clamp<std::int64_t>(last, first, n - 1);
                    window_first = static_cast<std::size_t>(first);
                    window_last = static_cast<std::size_t>(last);
                    leading_spacer_h = spacer_height(window_first);
                    trailing_spacer_h = spacer_height(items.size() - 1 - window_last);
                }
            } else if (eligible && !rows_out_of_flow && !rows_grow && !items.empty() && measure_rows(element, items)) {
                // Rows of no fixed pixel height. Each row's top comes from the heights measured so far (a row that was
                // never measured counts as their mean), so the window is found by a search over the tops instead of
                // a division; the spacers stand in for the rows outside it.
                const std::vector<float> &tops = element.row_heights->tops;
                const float gap_px = element.gap.value;
                const auto n = static_cast<std::int64_t>(items.size());
                const float offset = (effective_context == &element)
                                             ? 0.0f
                                             : element.layout_rect.y - effective_context->layout_rect.y;
                const float scroll_y_effective = effective_context->scroll_y - offset;
                const float bottom = scroll_y_effective + effective_context->layout_rect.h;
                const auto row_at = [&](float y) {
                    const auto it = std::upper_bound(tops.begin(), tops.begin() + n, y);
                    return static_cast<std::int64_t>(it - tops.begin()) - 1;
                };
                std::int64_t first = row_at(scroll_y_effective) - kVirtualizationOverscanRows;
                std::int64_t last = row_at(bottom) + kVirtualizationOverscanRows;
                // The rows [from, to): their heights and the gaps between them.
                const auto spacer_height = [&](std::int64_t from, std::int64_t to) -> float {
                    if (from >= to) {
                        return 0.0f;
                    }
                    return tops[static_cast<std::size_t>(to)] - tops[static_cast<std::size_t>(from)] - gap_px;
                };
                if (last < 0 || first >= n || bottom < 0.0f) {
                    window_empty = true;
                    window_first = 0;
                    window_last = 0;
                    leading_spacer_h = spacer_height(0, n);
                    trailing_spacer_h = 0.0f;
                } else {
                    first = std::clamp<std::int64_t>(first, 0, n - 1);
                    last = std::clamp<std::int64_t>(last, first, n - 1);
                    window_first = static_cast<std::size_t>(first);
                    window_last = static_cast<std::size_t>(last);
                    leading_spacer_h = spacer_height(0, first);
                    trailing_spacer_h = spacer_height(last + 1, n);
                }
            }

            const auto is_spacer = [](const Element &row) {
                return row.is_virtualization_spacer && row.generated_owner == nullptr;
            };
            const auto generated_window_matches = [&] {
                std::size_t index = 0;
                const std::vector<Element> &rows = element.generated_items;
                if (leading_spacer_h > 0.0f) {
                    if (index >= rows.size() || !is_spacer(rows[index])) {
                        return false;
                    }
                    ++index;
                }
                if (!window_empty && !items.empty()) {
                    for (std::size_t i = window_first; i <= window_last; ++i) {
                        ViewModel *item = items[i];
                        if (item == nullptr) {
                            continue;
                        }
                        for (std::size_t n = 0; n < expected_count; ++n) {
                            if (index >= rows.size() || rows[index].is_virtualization_spacer ||
                                rows[index].generated_owner != item || rows[index].generated_serial != item->serial()) {
                                return false;
                            }
                            ++index;
                        }
                    }
                }
                if (trailing_spacer_h > 0.0f) {
                    if (index >= rows.size() || !is_spacer(rows[index])) {
                        return false;
                    }
                    ++index;
                }
                return index == rows.size();
            };
            const auto write_spacer_height = [&](Element &spacer, float height) {
                if (spacer.height && spacer.height->unit == LengthUnit::Px && spacer.height->calc.empty() &&
                    spacer.height->value == height) {
                    return;
                }
                spacer.height = Length{height, LengthUnit::Px};
                mark_dirty(document, spacer, kDirtyStyle | kDirtyLayout | kDirtyPaint, true);
            };
            if (generated_window_matches()) {
                std::size_t index = 0;
                if (leading_spacer_h > 0.0f) {
                    write_spacer_height(element.generated_items[index++], leading_spacer_h);
                }
                if (!window_empty && !items.empty()) {
                    for (std::size_t i = window_first; i <= window_last; ++i) {
                        ViewModel *item = items[i];
                        if (item == nullptr) {
                            continue;
                        }
                        for (std::size_t n = 0; n < expected_count; ++n) {
                            Element &row = element.generated_items[index++];
                            // A table build binds the rows in place. Otherwise they keep the slots they have.
                            if (pass.collecting) {
                                bind_element(pass, row, *item, false, child_scroll_context);
                            }
                        }
                    }
                }
                if (trailing_spacer_h > 0.0f) {
                    write_spacer_height(element.generated_items[index], trailing_spacer_h);
                }
                return;
            }

            // A changed set. An item still in the window reuses its Element (motion clocks
            // included). A new item is cloned from the template. Spacers are rebuilt; they
            // carry no runtime state.
            std::unordered_map<std::uint64_t, std::vector<Element>> previous_by_owner;
            for (Element &old : element.generated_items) {
                previous_by_owner[old.generated_serial].push_back(std::move(old));
            }
            element.generated_items.clear();

            const auto push_spacer = [&](float height) {
                Element spacer;
                spacer.kind = ElementKind::Canvas;
                spacer.height = Length{height, LengthUnit::Px};
                spacer.is_virtualization_spacer = true;
                index_subtree(arena, spacer, element.handle, 0, true, true);
                mark_new_subtree(document, spacer);
                element.generated_items.push_back(std::move(spacer));
            };
            // A cloned row is bound against its own item and, with its subtree, wants every pass.
            const auto bind_new = [&](Element &clone, ViewModel &item) {
                index_subtree(arena, clone, element.handle, 0, true, true);
                const bool was_collecting = pass.collecting;
                pass.collecting = true;
                bind_element(pass, clone, item, false, child_scroll_context);
                pass.collecting = was_collecting;
                mark_new_subtree(document, clone);
            };

            if (leading_spacer_h > 0.0f) {
                push_spacer(leading_spacer_h);
            }
            for (std::size_t i = window_first; !window_empty && !items.empty() && i <= window_last; ++i) {
                ViewModel *item = items[i];
                if (item == nullptr) {
                    continue;
                }
                if (const auto reused = previous_by_owner.find(item->serial());
                    reused != previous_by_owner.end() && reused->second.size() == expected_count) {
                    for (Element &clone : reused->second) {
                        // The reused row keeps its id and the slots it has, unless a table build is under way.
                        if (pass.collecting) {
                            bind_element(pass, clone, *item, false, child_scroll_context);
                        }
                        element.generated_items.push_back(std::move(clone));
                    }
                    previous_by_owner.erase(reused);
                    continue;
                }
                if (tmpl->children.empty()) {
                    Element clone = *tmpl;
                    clone.kind = ElementKind::Stack;
                    clone.children.clear();
                    clone.generated_owner = item;
                    clone.generated_serial = item->serial();
                    bind_new(clone, *item);
                    element.generated_items.push_back(std::move(clone));
                    continue;
                }
                for (const Element &node : tmpl->children) {
                    Element clone = node;
                    clone.generated_items.clear();
                    // Drop an owner copied off the template. inherit_owner then stamps this item onto the row.
                    clear_row_owner(clone);
                    clone.generated_owner = item;
                    clone.generated_serial = item->serial();
                    bind_new(clone, *item);
                    element.generated_items.push_back(std::move(clone));
                }
            }
            if (trailing_spacer_h > 0.0f) {
                push_spacer(trailing_spacer_h);
            }
            // What the window no longer holds is gone: its ids are freed, never handed out again, and its slots (which
            // point at view-models that may be gone with the row) are dropped with them. What it holds keeps the id it
            // had. Every row's place in the list is written down again.
            for (const auto &dropped : previous_by_owner) {
                for (const Element &row : dropped.second) {
                    release_subtree(arena, row);
                }
            }
            std::erase_if(document.bind_table.slots, [&](const BindSlot &slot) { return !arena.alive(slot.element); });
            for (std::size_t i = 0; i < element.generated_items.size(); ++i) {
                Element &row = element.generated_items[i];
                row.parent = element.handle;
                if (ElementArena::Slot *slot = arena.slot(row.handle)) {
                    slot->parent = element.handle;
                    slot->position = static_cast<std::uint32_t>(i);
                    slot->generated = true;
                }
            }
            // The rows are a different set: this control lays out again, and what its layout and hit-test read.
            mark_dirty(document, element, kDirtyLayout | kDirtyPaint);
            pass.structure_changed = true;
        }

        // --- slots -------------------------------------------------------------------------------------------------

        // What an ItemsControl slot needs to know about the tree above it: the nearest strict ancestor that scrolls
        // vertically (what the full walk threads down as `scroll_context`), and whether it sits inside an ItemTemplate
        // (a template is never bound against a row).
        struct AboveItems {
            const Element *scroll_context = nullptr;
            bool in_template = false;
            bool resolved = false;
        };

        AboveItems look_above(UiDocument &document, const Element &element) {
            AboveItems above;
            thread_local std::vector<Element *> chain;
            if (!ancestor_chain(document, element, chain)) {
                return above;
            }
            above.resolved = true;
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                if (above.scroll_context == nullptr && is_scrollable_y(**it)) {
                    above.scroll_context = *it;
                }
                if ((*it)->kind == ElementKind::ItemTemplate) {
                    above.in_template = true;
                }
            }
            return above;
        }

        // Reads one slot again. False when the slot's element is gone (the slot is dropped).
        bool refresh_slot(BindPass &pass, BindSlot &slot) {
            UiDocument &document = *pass.document;
            const auto found = document.find(slot.element);
            if (!found) {
                return false;
            }
            Element &element = **found;
            ViewModel &vm = *slot.source;
            switch (slot.field) {
                case BindField::Text:
                case BindField::Content:
                    bind_text(pass, element, vm, slot.binding);
                    break;
                case BindField::TrArg:
                    bind_translation(pass, element, vm, false);
                    break;
                case BindField::Source:
                    bind_source(pass, element, vm, false);
                    break;
                case BindField::SliderValue:
                    bind_slider_value(pass, element, vm, false);
                    break;
                case BindField::If:
                    bind_if(pass, element, vm, false);
                    break;
                case BindField::PanX:
                case BindField::PanY:
                case BindField::Zoom:
                    bind_camera(pass, element, vm, slot.field);
                    break;
                case BindField::ScrollX:
                case BindField::ScrollY:
                    bind_scroll(pass, element, vm, slot.field);
                    break;
                case BindField::Checked:
                    bind_checked(pass, element, vm);
                    break;
                case BindField::Open:
                    bind_open(pass, element, vm);
                    break;
                case BindField::Var:
                    bind_custom_property(pass, element, vm, slot.index);
                    break;
                case BindField::ItemsSource: {
                    const AboveItems above = look_above(document, element);
                    if (above.resolved && !above.in_template) {
                        reconcile_items(pass, element, vm, above.scroll_context);
                    }
                    break;
                }
                case BindField::Command:
                case BindField::Paint:
                    break;
            }
            return true;
        }

        void build_table(BindPass &pass, ViewModel &data_context) {
            UiDocument &document = *pass.document;
            BindTable &table = document.bind_table;
            table.slots.clear();
            table.failures.clear();
            table.context = &data_context;
            table.catalog = pass.catalog;
            table.catalog_version = pass.catalog != nullptr ? pass.catalog->version() : 0;
            table.context_registration = data_context.registration_version();
            table.built = true;
            ++document.counters.table_builds;
            pass.collecting = true;
            bind_element(pass, document.root, data_context, false, nullptr);
            pass.collecting = false;
            document.windows_dirty = false;
        }

    } // namespace

    std::expected<void, UiError> apply_bindings(UiDocument &document, ViewModel &data_context, IBindErrorSink *errors,
                                                const engine::loc::Catalog *catalog, const Stylesheet *stylesheet) {
        if (!document.arena.alive(document.root.handle)) {
            // A tree built by hand and never indexed: the reconcile needs its ids.
            document.reindex();
            document.bind_table.built = false;
        }
        BindPass pass;
        pass.document = &document;
        pass.errors = errors;
        pass.catalog = catalog;
        pass.sheet = stylesheet;
        BindTable &table = document.bind_table;

        const std::uint64_t catalog_version = catalog != nullptr ? catalog->version() : 0;
        bool rebuild = !table.built || table.context != &data_context || table.catalog != catalog ||
                       table.catalog_version != catalog_version ||
                       table.context_registration != data_context.registration_version();

        if (rebuild) {
            build_table(pass, data_context);
            return pass.first_error ? std::expected<void, UiError>(std::unexpected(*pass.first_error))
                                    : std::expected<void, UiError>{};
        }

        // The failures of the build are reported again on every pass, like the whole walk did.
        for (const BindFailure &failure : table.failures) {
            if (errors != nullptr) {
                errors->bind_error(failure.message);
            }
            if (pass.counts(failure.error) && !pass.first_error) {
                pass.first_error = failure.error;
            }
        }

        // A quiet pass: one integer compare per slot. A slot whose version moved is read again. The table stays
        // valid unless a reconcile changed the tree; indexes, not iterators, because a refresh may append slots.
        const bool windows = document.windows_dirty;
        document.windows_dirty = false;
        for (std::size_t i = 0; i < table.slots.size(); ++i) {
            BindSlot &slot = table.slots[i];
            ++document.counters.slots_checked;
            if (slot.source != nullptr && slot.source->registration_version() != slot.registration_seen) {
                // A view-model registered a binding after the build, or replaced a paint: read everything again.
                build_table(pass, data_context);
                return pass.first_error ? std::expected<void, UiError>(std::unexpected(*pass.first_error))
                                        : std::expected<void, UiError>{};
            }
            if (slot.field == BindField::Command) {
                // A command has no version: `can_execute()` is polled.
                const auto found = document.find(slot.element);
                if (found && (*found)->command != nullptr) {
                    const bool executable = (*found)->command->can_execute();
                    if (executable != slot.executable) {
                        slot.executable = executable;
                        set_disabled_from(pass, **found, executable);
                    }
                }
                continue;
            }
            if (slot.field == BindField::Paint || !slot.version) {
                continue;
            }
            const std::uint64_t now = slot.version->read();
            // An ItemsControl's window and a scroll position clamped to the layout's extent depend on the layout, not
            // only on the property: they are read again after a layout pass (`windows_dirty`).
            const bool layout_bound = slot.field == BindField::ItemsSource || slot.field == BindField::ScrollX ||
                                      slot.field == BindField::ScrollY;
            if (now == slot.seen && !(layout_bound && windows)) {
                continue;
            }
            slot.seen = now;
            // A refresh may reconcile: it drops the slots of the rows that left and appends the new rows' slots. A
            // row's slots come after its ItemsControl's, so nothing at or before this index moves; copy it anyway, the
            // reference does not survive the append.
            BindSlot refreshed = slot;
            if (!refresh_slot(pass, refreshed)) {
                slot.version.reset(); // the element is gone; the slot never fires again and the next build drops it
            }
        }
        if (pass.first_error) {
            return std::unexpected(*pass.first_error);
        }
        return {};
    }

} // namespace engine::ui
