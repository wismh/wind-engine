# asset_codegen

Read-only build tool: emit `asset_ids.h` + cooked catalog, rewriting a file only when its content changes (and `component_io.h` with `--components`, covering `WIND_COMPONENT` and `WIND_SERIALIZABLE`); fail if `.meta` is missing. An `importer = "object"` file must be a TOML table with a non-empty string `type`. `--stage` instead writes only a staged runtime tree. Never writes `.meta`.
