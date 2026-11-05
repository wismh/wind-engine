# Post process plan

Phases 1–3 are implemented. The as-built description is [Render](../modules/Render.md#post-process). This page is the decision record.

A camera with no stack still draws straight into the window back buffer (OpenGL framebuffer 0) or the swapchain (Vulkan). `Command` is `CmdSetView`, `CmdDrawMesh`, `CmdDrawParticles`, `CmdDrawUI`, and `CmdPostProcess`.

## Goal

A camera can run an ordered stack of fullscreen shaders over the world image, then the HUD draws on top. A camera with no stack draws as it does today, into the window, with no extra target. First consumer: a 3D scene that grades color or blooms. A 2D game that never adds a stack pays nothing.

One-pass filters on a capture frame are a material on a sprite that samples an `IDynamicTexture`. They do not need this plan. Saving a photo (readback, JPEG/PNG encode) stays in [Capture Plan](Capture%20Plan.md). The UI offscreen texture in [UI Performance Plan](UI%20Performance%20Plan.md) is a cached widget picture, not the frame.

## As built today

- `OpenGLCanvas::render` clears the window (color, stencil, depth) and `IRenderBackend::execute` consumes the whole buffer there. `VulkanCanvas` begins one render pass on the swapchain image and records the scene and the UI inside it.
- `run_render` pushes one `CmdSetView` per bound window, then the culled mesh and particle draws. `run_ui_render` appends `CmdDrawUI` to the same buffer. `CmdSetView` already carries `fog` and `time`.
- `render_world_viewport` pushes a `CmdSetView` with a sub-rectangle `viewport` into a caller-chosen buffer (the editor scene view). It copies no fog.
- `ITexture` and `IDynamicTexture` are sampled images filled from the CPU. No type is a color attachment.
- Framebuffers are not shared across GL contexts. VAOs already follow that rule: one per context, released when another window's context is current. Swapchain images are per canvas.
- Color is 8-bit UNORM. There is no sRGB framebuffer, no MSAA, no HDR target.
- `read_pixels` reads the back buffer after `render` and before `present`, so a pass that ends on the window is what a screenshot stores.

## Decisions

- The stack is data on the active camera, the same shape as `Fog`. No callback, no `CmdCustomDraw`, no `gl*` from a game.
- The scene target is per canvas, sized to that window's drawable. Windows of one world share `ActiveCamera` and so share the stack, and each canvas keeps its own targets because drawable sizes differ. A headless server has no canvas and allocates nothing.
- UI is not an input to the stack. Particles are: they are world draws.
- `render_world_viewport` does not copy the stack, as it does not copy fog. Editor scene views stay unprocessed until a later phase sizes a target to the viewport rectangle.
- The GPU pass stays out of `engine_tests` ([Boundaries](Boundaries.md)). The CPU contract (copy, order, empty stack, fatal shader) is a test with a recording backend.

## Public API (target shape)

`PostStack` on the `ActiveCamera` entity (`include/engine/render/post_process.h`).

```cpp
struct PostEffect {
    AssetId shader{};             // a .shader asset; sampled image is uScene
    glm::vec4 params{1, 1, 1, 1};
};

struct PostStack {
    bool hdr = false;                  // float scene target and a builtin tonemap after effects
    std::vector<PostEffect> effects;   // effects[0] reads the scene
};
```

The game puts `PostStack` on the `ActiveCamera` entity, in C++ or in the camera's `.world` table. `run_render` reads it every frame, the same way it reads `Fog`. No `PostStack` means the window is the target. `hdr == false` and an empty `effects` list is the same. `hdr == true` with an empty list still renders the world to a float target and tonemaps it; the tonemap is not an entry the game adds.

`run_render` resolves each `AssetId` to an `IShader` and copies `hdr` and the list into `CmdSetView::post`. A missing shader is fatal, the same as a sprite with no material. An effect that requires HDR (bloom) on a stack with `hdr == false` is fatal.

The game does not pick `R11F_G11F_B10F` or `RGBA16F`. A transparent window needs alpha, so the backend uses `RGBA16F`; every other HDR window uses `R11F_G11F_B10F`.

`.world` stores `hdr` and each effect as the shader GUID plus the four floats, next to `Fog` in the serializer.

A post shader is an ordinary `.shader` asset (GLSL 330 in CDATA, `shader_adapt` to 300 es, Slang for Vulkan). The vertex stage is fixed by the backend: a clip-space triangle, no `uModel` / `uView` / `uProjection`. The fragment stage reads:

| Uniform | Meaning |
| --- | --- |
| `sampler2D uScene` | The image the previous pass wrote. `(0, 0)` is the top-left of the window, `(1, 1)` the bottom-right, on both backends |
| `vec4 uParams` | `PostEffect::params` |
| `vec2 uResolution` | Drawable size in pixels |
| `float uTime` | `CmdSetView::time` |

No lights, no fog uniforms, no `uTexture`. A world shader used as a post shader fails visibly instead of sampling the wrong binding.

## Frame when the stack is empty

Unchanged. Clear the window, execute every command into it, present. No target is created.

## Frame when the stack is not empty

`run_render` pushes the `CmdSetView` (with `post`) and the world draws even when the culled list is empty, so a stack still runs on the clear color. It then pushes one `CmdPostProcess` marker. `run_ui_render` still appends `CmdDrawUI` after that.

Per canvas, two RGBA8 color textures (`scene_a`, `scene_b`), clamp-to-edge, linear, no mipmaps, plus a depth renderbuffer on the target the world draws into. Created on first use, recreated when the drawable size changes, dropped when the canvas dies. The framebuffer object is per GL context; the textures follow the sharing rules the backend already uses for window textures. Vulkan images are not shared across canvases.

1. World draws (`CmdDrawMesh`, `CmdDrawParticles`) go to `scene_a`. Clear color and alpha match the window clear (alpha 0 when the window is transparent). Depth test and write happen on that target's depth renderbuffer. The depth is not sampled.
2. Effect `i` samples the image the previous step wrote and writes the other scene texture. The last effect writes the window.
3. `CmdDrawUI` draws to the window, after the last effect, with the stencil the UI already uses.

A stack of one effect is world → `scene_a` → window. A longer stack ping-pongs `scene_a` and `scene_b`. The world never writes the window in this path, so the window depth buffer stays the one the canvas cleared; UI does not read it.

The post draw is a triangle the backend owns. The shader's `uScene` uv is top-left origin on GL and Vulkan; the backend flips the triangle's V where the framebuffer is bottom-up.

`read_pixels` stays where it is and therefore stores the graded image with UI on top.

A `CmdSetView` that carries a `viewport` keeps today's path (scissor, clear that rect, draw into the window) and ignores `post`. `run_render`'s view has no viewport, so the game window and a viewport in some other buffer do not interfere. A game that appends a viewport to a buffer that already ran a post stack draws that viewport on top of the graded window image.

## Phases

Task numbers are assigned when the tasks are created. Each phase is one branch `feat/wind-N-...` from `dev`.

### 1. LDR stack

- `PostStack`, `CmdSetView::post`, `CmdPostProcess`, serializer, `run_render` copy, `render_world_viewport` leaves `post` empty.
- OpenGL and Vulkan: the target pair, the pass split, the fullscreen triangle, UI after the last effect. Empty `post` does not allocate.
- One builtin, `builtin::shader_post_grade`: `FragColor.rgb = texture(uScene, vUV).rgb * uParams.rgb`, alpha copied from `uScene`. New GUID in `builtin_ids.h`; existing GUIDs stay.
- Tests: empty and absent stacks produce an empty `post`; order follows `effects`; a bad GUID is fatal; a viewport view has an empty `post`; the serializer round-trips; a recording backend binds world draws to the scene target and `CmdDrawUI` to the window only when `post` is non-empty.
- Done when: a windowed game with grade (`params` not white) then a second effect shows both, UI is ungraded, and removing the component matches a build without this phase. `Render.md` and the `wind-rendering` skill updated in that change.

### 2. HDR view

HDR is a flag on the camera (`PostStack::hdr`), not a consequence of adding bloom. The window stays 8-bit UNORM either way. A grade-only stack leaves the flag off and stays RGBA8: `shader_post_grade` multiplies an image that is already in the display range.

When the flag is on, every world fragment is stored in a float target, so a light above 1 survives until a builtin tonemap writes the window. The tonemap is the last pass of an HDR view; the game does not insert it. Desktop opaque windows use `R11F_G11F_B10F` (4 bytes per pixel, the same size as RGBA8, no alpha). A transparent window keeps alpha, so it uses `RGBA16F` (8 bytes). Bloom is one effect that requires the flag: threshold and intensity from `uParams`, a fixed downsample chain (half, quarter, eighth), an additive composite into the float target, then the tonemap. Bloom with `hdr` off is fatal.

GLES 3.0 and WebGL2 need a float color attachment (`EXT_color_buffer_float` / `EXT_color_buffer_half_float`). Where it is missing, the flag is ignored, one warning is logged, and the stack runs in LDR. An empty effect list still copies the scene color to the window; the tonemap does not run on that RGBA8 image.

Done when: a lit scene with `hdr` keeps a highlight above 1 until the tonemap; bloom on that scene blooms it; a grade-only stack allocates no float target; bloom with `hdr` off is fatal; the missing-extension path logs and still presents.

### 3. Sampled depth

Replace the world depth renderbuffer with a depth texture when the stack contains an effect that declares it needs depth. The effect reads `sampler2D uDepth`. Effects that do not need it leave the renderbuffer in place.

- Done when: one builtin that tints by depth runs on GL and Vulkan, and a stack with no such effect allocates no depth texture.

## Later

- A target the size of `CmdSetView::viewport`, so the editor scene view and an in-game picture-in-picture can run a stack without grading the whole window.
- Effects that need the depth texture (SSAO, depth of field). Phase 3 only makes the texture available.
- sRGB framebuffer. It would regrade every current game, so it is not part of phase 1 or 2.
- MSAA resolve. There is no MSAA path to resolve.
- Chaining a capture frame through these targets. A sprite material remains the capture filter.

## Risks

- Vulkan records UI inside the scene render pass today. The split has to end that pass before the post pass and start the swapchain pass before `CmdDrawUI`, or the HUD is graded.
- Two RGBA8 targets at 1080p are about 16 MB. `R11F_G11F_B10F` is the same 8 MB per target. `RGBA16F` is about 16 MB per target. The allocation is the small part: the world pass writes those bytes per shaded pixel, which is the mobile cost. Desktop at 1080p does not notice.
- A post shader that writes alpha changes a transparent window. `shader_post_grade` copies alpha so the clear (0 on a transparent window) survives.
- WebGL2 cannot sample a texture that is still the draw framebuffer. The ping-pong above is the rule on every backend, not only Vulkan.
