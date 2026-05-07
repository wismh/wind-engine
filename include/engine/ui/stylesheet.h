#pragma once

// docs/tech/features/UI Markup.md

#include <engine/core/export.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::ui {

enum class CssError {
    InvalidSyntax,
};

enum class CssSelectorType {
    Element,
    Class,
    Id,
    ElementClass,
};

enum class CssCombinator {
    Descendant,
    Child,
};

struct CssSelector {
    CssSelectorType type = CssSelectorType::Element;
    std::string element;
    std::string class_name;
    std::string id;
    std::string pseudo;
};

struct CssDeclaration {
    std::string property;
    std::string value;
};

enum class MediaFeature {
    MinWidth,
    MinHeight,
};

struct MediaQuery {
    MediaFeature feature = MediaFeature::MinWidth;
    float px = 0.0f;
};

// The pseudo-classes a selector can test, one bit each.
inline constexpr std::uint8_t kPseudoHover = 1;
inline constexpr std::uint8_t kPseudoPressed = 2;
inline constexpr std::uint8_t kPseudoDisabled = 4;
inline constexpr std::uint8_t kPseudoFocus = 8;
inline constexpr std::uint8_t kPseudoChecked = 16;

// What a declaration can change on screen. Every declaration restyles the element (`kDirtyStyle`); the geometry and
// text-shaping properties (width, height, min/max, margin, padding, border width, gap, display, visibility, position
// and insets, flex properties, overflow, font, line height, white space) also lay it out (`kDirtyLayout`); every
// other property is paint only (`kDirtyPaint`: color, background, opacity, transform, border colour and radius, ...).
// A custom property (`--x`) or a `var()` in a value could feed any property, so it is all three.
inline constexpr std::uint8_t kDirtyStyle = 1;
inline constexpr std::uint8_t kDirtyLayout = 2;
inline constexpr std::uint8_t kDirtyPaint = 4;
// Only the live parameters of a repaint boundary changed (its scroll offset, camera, opacity, transform, scrollbar):
// its frame is recorded again, its recorded drawing is kept.
inline constexpr std::uint8_t kDirtyFrame = 8;

struct CssRule {
    CssSelector selector;
    std::vector<CssSelector> ancestors;
    std::vector<CssCombinator> combinators;
    std::vector<CssDeclaration> declarations;
    std::optional<MediaQuery> media;
    // Recorded by `parse_css` (`analyze_rule` repeats it for a rule built by hand): the pseudo-class bits on the
    // subject compound, the bits on any ancestor compound (`A:hover B`), and the `kDirty*` bits of the declarations.
    std::uint8_t subject_pseudo = 0;
    std::uint8_t ancestor_pseudo = 0;
    std::uint8_t dirties = 0;
    // The line of the selector in the source text (1-based); 0 for a rule built by hand. A warning names it.
    std::uint32_t line = 0;
};

struct KeyframeStop {
    float offset = 0.0f;
    std::vector<CssDeclaration> declarations;
};

// The numeric tracks of one `@keyframes` block (`src/ui/style_anim.cpp`): the declarations are parsed once, not on
// every sample.
struct CompiledKeyframes;

struct Keyframes {
    std::string name;
    std::vector<KeyframeStop> stops;
    // Built on the first sample and built again when the number of stops or of their declarations differs from what it
    // was built for. A value edited in place is not noticed (a parsed sheet is not edited; one built by hand and
    // changed after its first sample needs a new `Keyframes`). A copy shares it: the content is the same.
    mutable std::shared_ptr<const CompiledKeyframes> compiled;
};

// Bumped once per freshly default-constructed Stylesheet, preserved (not re-bumped) by copy/move
// since those carry the same content forward. Element's compute_style() cache (document.h's
// StyleCacheEntry) keys on this alongside the Stylesheet* itself: some reload paths (e.g.
// systems.cpp's run_bind merging extra_stylesheets) move-assign a freshly parsed Stylesheet into
// an already-engaged std::optional<Stylesheet> living inside a long-lived UiInstance component,
// which leaves the pointer identical across a real content change — generation catches that.
// The counter lives in src/ui/stylesheet.cpp, so engine.dll and a game module share one sequence.
[[nodiscard]] ENGINE_API std::uint64_t next_stylesheet_generation() noexcept;

// What a stylesheet's pseudo-class rules can do, derived from its rules (`style_analysis`).
struct StyleAnalysis {
    // The `generation` and rule count it was computed for.
    std::uint64_t generation = 0;
    std::size_t rule_count = 0;
    bool valid = false;
    // Rules with a pseudo-class on any compound, and among them the ones that also lay out (`dirties` has
    // `kDirtyLayout`): only these can change an element's layout style under a pseudo-class.
    std::vector<std::uint32_t> pseudo_rules;
    std::vector<std::uint32_t> layout_pseudo_rules;
    // Per rule (same index as `rules`): 1 when it has a pseudo-class and lays out, so the layout style reads the real
    // pseudo-state for it; 0 otherwise (the layout style never sees a pseudo-class rule that only paints).
    std::vector<std::uint8_t> layout_pseudo;
    // The pseudo bits that any rule tests on an ancestor compound. 0: no element's style depends on an ancestor's
    // pseudo-state, so a pointer over a container restyles nothing below it.
    std::uint8_t ancestor_pseudo_mask = 0;
    // A rule with a pseudo-class declares `transform` (or something a `var()` could feed it): an element can be
    // transformed by a pointer that the layout style does not see.
    bool pseudo_transform = false;
};

struct Stylesheet {
    std::uint64_t generation = next_stylesheet_generation();
    std::vector<CssRule> rules;
    std::vector<Keyframes> keyframes;
    // Values the host supplies, keyed without the leading `--` (`safe-top`). `var(--name)` reads them after the
    // element's own and cascaded custom properties, before the fallback. They are not inherited, so a rule on any
    // element reads them. `set_environment` is the only writer.
    std::unordered_map<std::string, std::string> environment;
    // Computed on first use by `style_analysis`. A sheet whose `rules` were edited after that must be analysed again
    // (a fresh `Stylesheet`, or `style_analysis` with a changed rule count does it).
    mutable StyleAnalysis analysis;

    // Sets `environment[name]`. A changed value takes a new `generation`, so every style and layout cache keyed on the
    // sheet is rebuilt; an equal value changes nothing.
    ENGINE_API void set_environment(std::string_view name, std::string_view value);
};

// The `kPseudo*` bit of a pseudo-class name (`hover`, `pressed`, `disabled`, `focus`, `checked`); 0 for any other.
[[nodiscard]] std::uint8_t pseudo_class_bit(std::string_view pseudo) noexcept;

// The `kDirty*` bits of one declaration.
[[nodiscard]] std::uint8_t declaration_dirties(const CssDeclaration &declaration) noexcept;

// Fills `subject_pseudo`, `ancestor_pseudo`, and `dirties` of `rule` from its selectors and declarations.
void analyze_rule(CssRule &rule) noexcept;

// The pseudo-class facts of `sheet`, computed on first use and again when its rule list changed.
[[nodiscard]] const StyleAnalysis &style_analysis(const Stylesheet &sheet);

[[nodiscard]] std::expected<Stylesheet, CssError> parse_css(std::string_view css, std::vector<std::string>& warnings);

}
