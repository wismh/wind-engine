---
tags: [file, core]
aliases: [src/cli/cli_server.h]
---

# `src/cli/cli_server.h`

Module: [[modules/Core]]

Private request and response types, `parse_request`, and `execute`. Under `ENGINE_CLI_SERVER` it also declares `start`, `stop`, `begin_frame`, `drain`, and `wait_for_request`. Without the macro the first four are empty inlines. Not on the public include path. See [[features/CLI]].

Repo path: `src/cli/cli_server.h`
