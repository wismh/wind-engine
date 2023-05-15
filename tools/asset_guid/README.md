# asset_guid

Dev-only tool (SDD §10.4): walk an assets tree and write a sidecar `.meta` for each known file that does not have one. New GUID + default importer (and audio defaults for `.wav`). Never overwrites an existing `.meta`. Do not run this in CI; commit the new files, then `asset_codegen` can emit ids.
