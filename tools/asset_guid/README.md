# asset_guid

Dev-only tool: walk an assets tree and write a sidecar `.meta` for each file that does not have one. New GUID + default importer (and audio defaults for `.wav`). A file with an unknown extension gets `importer = "data"` (`TextAsset`). Never overwrites an existing `.meta`. Do not run this in CI; commit the new files, then `asset_codegen` can emit ids.
