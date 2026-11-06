#pragma once

namespace engine::shader_cook {

// Installs the Slang cooker `codegen_scan` and `codegen_stage` call. The engine library does not call this.
void register_shader_cooker();

}
