# asset_codegen

Read-only build tool: emit `asset_ids.h` + cooked catalog (and `component_io.h` with `--components`, covering `WIND_COMPONENT` and `WIND_SERIALIZABLE`); fail if `.meta` is missing. An `importer = "object"` file must be a TOML table with a non-empty string `type`. Never writes `.meta`.
