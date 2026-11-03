# Net

HTTP requests ([API](#api)) and realtime networking with entity replication ([Realtime](#realtime)).

HTTP and HTTPS requests. One frontend, `IHttpClient`. The backend is chosen at compile time inside `HttpClient`. There is no `ENGINE_WITH_*` option and no third-party library.

## API

`include/engine/net/http_client.h`, `http_call.h`, `http_request.h`.

| Type | Role |
| --- | --- |
| `HttpRequest` | `method` (`Get`, `Post`, `Put`, `Patch`, `Delete`, `Head`), `url`, `headers`, `body` (raw bytes), `timeout_seconds` (30 by default), `max_redirects` (10), `allow_insecure_redirect`, `min_bytes_per_second` + `stall_seconds`, `sink` + `sink_thread` |
| `HttpResponse` | `status`, `headers` (names as the server sent them), `body`. `header(name)` finds the first header ignoring ASCII case |
| `HttpError` | No response at all: `InvalidUrl`, `Unsupported`, `Network`, `Timeout`, `TooLarge` (`kHttpMaxResponseBytes`, 64 MiB), `Dns`, `Tls`, `ConnectionReset`, `RedirectRefused`, `SinkAborted` |
| `HttpResult` | `std::expected<HttpResponse, HttpError>` |
| `HttpBodySink` | `bool(std::span<const std::byte>)`: one piece of a body, in order. `false` aborts |
| `HttpProgress` | `received` bytes and `total` (from Content-Length, if any) |
| `HttpCall` | One request in flight, owned by whoever sent it. `progress()` while pending |
| `IHttpClient` | `init`, `dispose`, `send(request) -> HttpCall`, `is_supported` |
| `HttpClient` | The engine implementation. Adds `poll()`, which only the game loop calls |

`EngineServices::http` is the client. A game keeps the `HttpCall` where it needs the answer (a component, a view-model, a game object) and asks it in any system:

```cpp
login_ = services.http.send({.method = engine::HttpMethod::Post, .url = url, .body = json});

if (std::optional<engine::HttpResult> result = login_.take()) {
    // *result is the response or the HttpError
}
```

### Contract

- `send` returns at once. The result reaches the call in `HttpClient::poll`, which `GameLoop::tick` runs after the presentation poll and before `simulate_worlds`. Every system of a frame sees the same state; a result never appears halfway through a frame.
- A 4xx or 5xx is an `HttpResponse` with its body, not an error.
- `take()` hands the result once, then the call is empty. `pending()` is true from `send` until the result arrives or the call is cancelled.
- `cancel()`, destroying a pending call, or move-assigning over it aborts the transfer. No result arrives afterwards. Keeping the call alive is how a request stays alive: a request whose call was dropped is cancelled.
- A malformed URL answers `InvalidUrl` on the next poll; so does any request on a platform without a backend (`Unsupported`). The game has one path for every outcome.
- `dispose` cancels every call still pending. `EngineHost` disposes the client with the other services.
- `HttpCall::resolved(result)` builds a finished call, for fakes of `IHttpClient`.

URLs: `http://` or `https://` (scheme case ignored), a non-empty host, an optional port 1–65535, IPv6 literals in brackets. User info, whitespace, and control characters are `InvalidUrl`. The fragment is dropped.

`timeout_seconds <= 0` waits forever. Windows and Android apply it to connect and to each read; Web to the whole request.

### Downloads: sink, progress, stall, redirects

The engine streams; the game decides what to do with the bytes (a `.part` file, a hash, a rename, resuming). A request with a `sink` gets the body of a **2xx** response in order, in chunks of at most 64 KiB, and `HttpResponse::body` stays empty; `kHttpMaxResponseBytes` does not apply. Any other status (a 404 page, a 3xx with `max_redirects = 0`) keeps its body in `HttpResponse::body`, so an error page never lands in the file. A sink that returns `false` aborts the transfer and the call ends with `SinkAborted`. Web delivers the whole body in one call (it loads into memory first).

`sink_thread` says where the sink runs:

| | `Worker` (default) | `Main` |
| --- | --- | --- |
| Thread | The transfer thread | The main thread, in `HttpClient::poll`, before any result of that frame |
| Buffering | None: a slow sink slows the download | A queue of 4 MiB (`kSinkQueueBytes`); a full queue pauses the transfer |
| Use for | Writing a file | Touching game state |
| Rule | Must not touch main-thread state or call into the engine | Stays within the frame budget: it gets up to 4 MiB per frame |

In both, the result is delivered only after the sink saw the whole body, and `cancel()` / destroying the call guarantees the sink is never called again once it returns. For `Worker` that means `cancel()` waits for a sink call that is running; a sink must never cancel its own call from its own thread. Web has no worker threads, so there both modes run on the main thread.

`HttpCall::progress()` is `{received, total}`: bytes counted after decompression, `total` from Content-Length (empty when the server sends none or the body is compressed). `Range` is an ordinary header; a resumed download answers 206, which is a success for the sink.

`min_bytes_per_second` with `stall_seconds` gives up with `Timeout` when the average speed over `stall_seconds` stays under the limit (the check of curl's `--speed-limit` / `--speed-time`). A connection that sends nothing at all is cut by `timeout_seconds`. Not on Web.

Redirects: `max_redirects` (default 10; 0 returns the 3xx as a response) and `allow_insecure_redirect` (https to http, off by default; a refused redirect is `RedirectRefused`). Windows sets WinHTTP's redirect policy and limit on every request, so the behavior is explicit and tested. Android follows redirects when `max_redirects > 0` up to the platform's own limit and does not follow a protocol switch. Web follows as the browser does.

Error codes: `Dns` (host did not resolve), `Tls` (certificate or handshake refused), `ConnectionReset` (an established connection dropped), `RedirectRefused`. Anything else stays `Network`, and so does a browser CORS refusal. A game that retries does it itself: `Timeout`, `ConnectionReset` and `Network` are worth another try, `Dns`, `Tls` and `InvalidUrl` are not.

### Why a call and not an event

The client belongs to the process, not to an `ecs::World`. A result posted as an event would need an owner world, would be lost when that world goes away, and could reach a different world allocated at the same address. A call owned by the requester has none of that: it dies with its owner, and in the editor the game's calls die with the game module on Stop ([Principles](../architecture/Principles.md) #13). A game that wants an event sends one from its own system after `take()`. `JobCall` (background jobs, [Core](Core.md#async-jobs)) follows the same rule.

## Backends

`src/net/http_client.cpp` picks one inside `Impl`:

| Build | Backend |
| --- | --- |
| `_WIN32` | WinHTTP (`winhttp_session.cpp`), blocking calls on `HttpWorkerPool` (4 threads). One session opened in `init` with the automatic proxy (falls back to the default proxy). gzip and deflate are decoded. A cancel closes the request handle, which makes the blocking call return. The body is read after `WinHttpQueryDataAvailable`: a plain `WinHttpReadData` waits for its whole buffer, which would hide progress and stalls on a slow body |
| `__ANDROID__` | `java.net.HttpURLConnection` through JNI (`android_http.cpp`) on the same pool. `init` caches the framework classes on the main thread. Workers get their `JNIEnv` from `SDL_GetAndroidJNIEnv`, which attaches the thread and detaches it when the thread exits. A 4xx/5xx body is read from `getErrorStream`. A cancel calls `disconnect()` |
| `__EMSCRIPTEN__` | `emscripten_fetch` (`web_fetch.cpp`) with `EMSCRIPTEN_FETCH_LOAD_TO_MEMORY`, on the main thread. A cancel closes the fetch. Status 0 is `Network`, or `Timeout` once the timeout has elapsed |
| Linux, macOS | The system libcurl, opened with `dlopen` (`curl_library.cpp`; `libcurl.so.4`, `libcurl.4.dylib`), on `HttpWorkerPool`. See below |
| Otherwise | None. `init` succeeds, `is_supported()` is false, and every request answers `Unsupported` |

`init` returning false means the backend failed to start (no WinHTTP session, JNI classes missing, no libcurl on the machine). `EngineHost::open_primary` logs a warning and goes on; requests then answer `Unsupported`.

Platform requirements:

- **Android.** The engine manifest (`cmake/android/app/src/main/AndroidManifest.xml`) has `android.permission.INTERNET`. From API 28 cleartext `http://` is blocked; a game that needs it allows it in its manifest overlay ([Game Consumer](../build/Game%20Consumer.md)). `PATCH` relies on the platform's `HttpURLConnection` accepting it.
- **Web.** `engine_target_web_link_options` links `-sFETCH=1`. The browser applies CORS: a server on another origin must send `Access-Control-Allow-Origin`, and a custom request header needs a preflight answer. A response header is visible only if the server exposes it (`Access-Control-Expose-Headers`). A CORS refusal is `Network`.
- **Windows.** `engine` links `winhttp` PUBLIC ([CMake](../build/CMake.md#what-links)).

### libcurl (Linux and macOS)

No libcurl header or library is needed to build: `src/net/curl_api.h` declares the six functions the backend calls (`curl_easy_init`, `_cleanup`, `_setopt`, `_perform`, `curl_slist_append`, `_free_all`) and the option numbers, which are libcurl's stable ABI, and `CurlLibrary` finds them with `dlopen`/`dlsym` when `HttpClient::init` runs. A machine without libcurl gets `init() == false` and `Unsupported`, as before, instead of a game that fails to start. Linux links `${CMAKE_DL_LIBS}` for glibc older than 2.34. TLS, certificates and (on macOS) the system proxy come from the library the OS ships.

`curl_transfer` (`curl_transfer.cpp`) does one blocking transfer on a worker and takes the function table as an argument, so it is built and tested on every platform with a fake libcurl. It maps the request onto options (`FOLLOWLOCATION`, `MAXREDIRS`, `REDIR_PROTOCOLS` for the https-to-http rule, `LOW_SPEED_LIMIT/TIME` for the stall check, `CONNECTTIMEOUT_MS`), reads the status and headers of the last response from the header callback, and feeds the body through `HttpCallState::accept`. Differences from the other backends:

- **Cancel** is noticed by the progress callback, which libcurl calls at least once a second: immediate while data flows, up to a second on a silent connection. libcurl handles are not touched from another thread.
- **`timeout_seconds`** covers the connect, and a transfer that moves no byte for that long ends with `Timeout` (the progress callback watches it). It is not libcurl's total-time limit, so a long download never trips it.
- **Redirects.** From an `https://` start a redirect to `http://` is refused unless `allow_insecure_redirect`; from an `http://` start both are allowed.
- Compressed bodies are decoded by libcurl; `total` is empty for them.

### Threads

`HttpCallState` (`src/net/http_call_state.h`) is shared by the call, the completion queue, and the backend. Every backend feeds the body through `accept` (progress, sink or `body`); `drain_sink` (main thread, from `poll`) and `finish_sink` (transfer thread, from `HttpWorkerPool` before it pushes the result) make a `Main` sink see all chunks before the result. `result` is touched on the main thread only. A backend brackets its blocking work with `begin_transfer(abort)` and `end_transfer()`; `cancel` runs the registered abort under the same lock, so a handle is closed exactly once. `HttpCompletions` (`CallCompletions` from `src/core/call_completions.h`, shared with `FileDialogCall`) takes results from any thread and hands them over in `deliver`, dropping cancelled ones. `HttpWorkerPool` skips a call cancelled while queued.

## Realtime

Realtime networking and entity replication live in `namespace engine::net`. The server is authoritative: it simulates on the fixed step and sends snapshots of replicated components; a client shows them interpolated, slightly in the past. Client input and prediction (wind-308), per-peer relevance and send budgets (wind-309), and a dedicated server run mode (wind-310) come later.

### Transport

`include/engine/net/network_transport.h`, `gns_transport.h`, `steam_transport.h`, `loopback_transport.h`, `loopback_bus.h`.

| Type | Role |
| --- | --- |
| `INetworkTransport` | `start_server`, `connect`, `disconnect`, `stop`, `send`, `poll_events`, `rtt_ms`. Main thread only |
| `GnsTransport` | IP/UDP through Valve's GameNetworkingSockets (prebuilt Windows `.lib`; elsewhere every call fails) |
| `SteamTransport` | Steam P2P through SteamNetworkingSockets, SteamID64 addresses |
| `LoopbackTransport`, `LoopbackBus` | In-process transport for tests: `send` queues the bytes on the other end at once, ignores `DeliveryMode`, and `rtt_ms` is 0 |
| `PeerId` | The transport's id of a connection. A client sees the server as `kServerPeerId` (1) |
| `DeliveryMode` | `Unreliable`, `UnreliableImmediate`, `ReliableOrdered` |
| `TransportEvent` | `Connected`, `Disconnected` (with `DisconnectReason`), `DataReceived` |

No delivery mode orders unreliable messages. `UnreliableImmediate` is `k_nSteamNetworkingSend_UnreliableNoNagle` on GNS: unreliable, sent without the Nagle delay. A receiver of unreliable data orders it itself; snapshots carry their tick. The `channel` argument is ignored by every transport.

A GNS unreliable message may be larger than one UDP packet (up to 512 KiB): GNS splits it, and when any piece is lost the whole message is lost. Nothing in the engine fragments messages yet (wind-309).

### Messages

`include/engine/net/message_dispatcher.h`, `net_messages.h`.

Every message starts with a little-endian `u16` `MessageId`. `MessageDispatcher` routes an incoming payload to the handler registered for its id (`Handler(PeerId from, ByteReader&)`, the reader positioned after the id); `MessageDispatcher::send` writes the id, then the payload. Ids `0x0100`-`0x01FF` (`kEngineMessageFirst`..`kEngineMessageLast`) are the engine's; `NetServer` and `NetClient` handle them and never pass them to their `dispatcher()`. A game registers its own messages outside that range on `world.ctx<NetServer>().dispatcher()` or `world.ctx<NetClient>().dispatcher()`.

`ByteWriter` / `ByteReader` (`byte_stream.h`) write and read little-endian primitives, enums, glm vectors and quaternions, `NetId`, length-prefixed strings and bytes (`write_bytes`), and raw bytes (`append`). The reader is non-allocating, bounds-checked, and latches its error (`is_ok`).

### Identity

`include/engine/net/network_identity.h`, `net_id.h`, `net_entity_map.h`.

| Type | Role |
| --- | --- |
| `NetId` | `using NetId = std::uint32_t`: the stable id of a replicated entity, the same on every machine. `kInvalidNetId` is 0 |
| `NetIdentity` | The component a replicated entity carries: `id` and `owner` (the server's `PeerId` of the owning client, `kServerPeerId` for server-owned) |
| `NetIdGenerator` | Monotonic id allocator (`next`, `reset`, `peek_next_value`); `NetServer` owns one |
| `NetEntityMap` | `NetId` to local `ecs::Entity` and back, in `world.ctx<NetEntityMap>()`. `unbind_dead` drops entries whose entity was destroyed |
| `bind_net_entity(world, e, id, owner)` / `unbind_net_entity` | Map entry plus `NetIdentity` |
| `find_entity(world, id)` | Through the map; nullopt for an unknown id or a dead entity |
| `find_net_id(world, e)` | The entity's `NetIdentity::id` |

On the server, `replicate(world, entity, owner = kServerPeerId)` takes the next free id from the server's generator and binds it. On a client the engine creates and binds replicated entities itself; game code does not bind ids there. A client knows which entities are its own by comparing `NetIdentity::owner` with `NetClient::local_peer()`.

### Replicated components

`include/engine/net/replication_registry.h`, `replica_writer.h`, `replica_reader.h`, `replicated_options.h`.

`world.ctx<ReplicationRegistry>()` lists the components a snapshot carries, in slot order. Its constructor registers `Transform` (slot 0) and `Parent` (slot 1). A game adds more:

```cpp
engine::net::register_replicated<Health>(
        world, "Health",
        [](engine::net::ReplicaWriter& out, const Health& h) { out.bytes().write_i32(h.value); },
        [](engine::net::ReplicaReader& in, Health& h) { return in.bytes().read(h.value); });
```

- `write(ReplicaWriter&, const T&)` and `read(ReplicaReader&, T&)` encode one value. `read` starts from `T{}` and returns false on bad input. `ReplicaWriter::write_entity` writes an entity reference as its `NetId`; `ReplicaReader::read_entity` maps it back through `NetEntityMap` (nullopt for 0 or an unknown id).
- `ReplicatedOptions<T>`: `interpolate(from, to, t)` (empty: the value steps), `apply(world, e, value)` (empty: assign or emplace), `remove(world, e)` (empty: `World::remove<T>`).
- `Transform` interpolates (`mix` position and scale, `slerp` rotation). `Parent` steps, is written as the parent's `NetId`, applied with `ecs::set_parent` and removed with `ecs::clear_parent`, so `Children` stays right. A `Parent` whose parent is not replicated arrives as 0 and is not applied.
- At most 32 components (`kMaxReplicated`). Registering a name twice, or a 33rd component, logs an error and is ignored (and asserts in Debug).
- The server and every client must register the same list in the same order. `schema_hash()` (FNV-1a over the names in slot order) travels in Welcome; a client whose hash differs logs an error and disconnects.

### Schedule

`include/engine/net/net_server.h`, `net_server_config.h`, `net_client.h`, `net_client_config.h`.

| Call | Registers |
| --- | --- |
| `install_net_server(world, transport, NetServerConfig{})` | `run_net_server_receive` on `Fixed`/`NetReceive`, `run_net_server_send` on `Fixed`/`NetSend` |
| `install_net_client(world, transport, NetClientConfig{})` | `run_net_client_receive`, then `run_net_client_interpolate`, on `Frame`/`NetReceive` |

`Phase::NetReceive` opens both schedules and `Phase::NetSend` closes `Fixed` ([ECS](ECS.md#systems-and-schedules)), so one server tick brackets the whole fixed step and a client's frame systems already see this frame's replicated state. The game owns the transport and keeps it alive while the world runs; `NetServer` and `NetClient` never touch it from their destructors. A second `install_*` call replaces the transport and config without adding systems again.

`world.ctx<NetServer>()`: `tick()`, `config()`, `dispatcher()`, `peers()`, `acked_tick(peer)`. `world.ctx<NetClient>()`: `welcomed()`, `local_peer()`, `newest_tick()`, `render_tick()`, `buffered_ticks()`, `dispatcher()`.

Events (each in its own header): `NetPeerConnectedEvent`, `NetPeerDisconnectedEvent` on both sides; on the client, `NetEntitySpawnedEvent{entity, net_id}` (replicated components already applied; the game adds sprite, mesh, sound) and `NetEntityDespawnedEvent{entity, net_id}` (sent as the entity is destroyed, so it is dead when read).

### Server ticks

`run_net_server_receive` polls the transport: a new peer is added and sent Welcome, a lost one is dropped, an ack is recorded, other messages go to `dispatcher()`. Then it advances the tick: the first fixed step is tick 1, `kNoTick` (0) means none yet. Physics and `Game` systems simulate that tick. `run_net_server_send` unbinds destroyed entities from `NetEntityMap`; on every `snapshot_interval`-th tick (default 2: 30 snapshots a second at the 60 Hz step) it captures every entity with `NetIdentity` once and sends each peer a snapshot tagged with the tick.

A world whose `Fixed` schedule does not run (paused, not stepping) neither polls the server transport nor sends snapshots; events wait in the transport. `Tick` is `u32`: it wraps after about 2.2 years at 60 Hz, which nothing handles.

### Snapshots, delta and ack

A snapshot is the full replicated state at a tick: every entity, its owner, and each registered component as encoded bytes. On the wire it is a delta against a baseline:

- Each peer has `acked`, the newest snapshot it acknowledged, and the snapshots it was sent within the last `kSnapshotWindowTicks` (128) ticks. The capture is shared by all peers (wind-309 makes it per peer).
- The baseline is the acked snapshot while it is inside that window; otherwise (no ack yet, or too old) the server sends a full snapshot (baseline `kNoTick`).
- The delta lists entities in the baseline and gone now, and for each entity that changed: new (with owner), owner changed, the slots whose bytes differ or are new, and the slots it lost. An unchanged entity is not written.
- The client decodes against the baseline in its buffer, stores the full state, and acks the tick (`SnapshotAck`, unreliable). It drops a snapshot whose baseline it does not hold, or that is malformed, without an ack; the server then keeps the older baseline, and falls back to a full snapshot once that leaves the window. The client keeps snapshots within the same 128 ticks of its newest, so a baseline the server may use is always still held.
- An ack counts only for a snapshot that peer was sent and the server still holds, and only when newer than the current one.

Snapshots are not compressed: delta encoding is the saving, and LZ4 on 50-1200 bytes of floats gains little for its cost per peer per tick. Values are not quantized. A snapshot above `kSnapshotMtuBudget` (1200 bytes, about 23 moving `Transform`s) is still sent; the server logs one warning per peer. GNS splits it and loses all of it when any piece is lost; a big full snapshot (a join) keeps being resent until one is acked. Budgets and fragmentation are wind-309.

### Client clock and interpolation

`SnapshotClock` (`src/net/snapshot_clock.h`) estimates the server time of the newest snapshot as of now. The first snapshot sets it; afterwards it runs with `Time::delta_time`, at 95%-105% speed, pulled toward each newer snapshot's `tick / tick_hz` (a smoothed error, gain 2/s). It never steps backward because of jitter. An error above 0.25 s (a stall, a server restart) snaps it.

`render_tick = (clock - interpolation_delay) * tick_hz`, with `NetClientConfig::interpolation_delay` 0.1 s by default (three snapshot gaps at 30 Hz). `SnapshotBuffer` (`src/net/snapshot_buffer.h`) keeps the decoded states sorted by tick, whatever order they arrived in; duplicates and states outside the window are dropped. Each frame `run_net_client_interpolate` samples it: `from` is the newest state at or before `render_tick`, `to` the next one. With no `to` the newest state holds (nothing is extrapolated); with no `from` yet nothing is applied. A lost snapshot just widens the gap between `from` and `to`.

Then, in this order:

1. Spawn: every entity of `from` exists. A missing one is created, bound, and given `NetIdentity`. An entity that was destroyed under the client (by a parent's cascade, or by game code) is made again.
2. Components, slot by slot, after every entity exists so `Parent` references resolve: a slot in both `from` and `to` with `interpolate` is interpolated at `t`, otherwise `from`'s value is applied; a slot the entity had and `from` lacks is removed.
3. Despawn: an entity that `from` no longer has gets `NetEntityDespawnedEvent`, is unbound and destroyed. This runs last so a child reparented away in the same snapshot is not destroyed with its old parent.

So spawn and despawn happen on the interpolation timeline: an entity appears when `render_tick` reaches the first snapshot that has it and disappears when it reaches the first one that does not. There are no spawn or despawn messages, so a snapshot can never arrive before its entity's spawn. Every replicated component is decoded and applied each frame; that is simple and fine at today's entity counts, and a typed cache can replace it later.

When the connection ends, the client despawns every replicated entity (with events), clears its buffer and clock, and forgets Welcome.

### Wire format

Little-endian. Every message starts with its `u16` id.

| Id | Message | Direction | Delivery | Fields after the id |
| --- | --- | --- | --- | --- |
| `0x0100` | Welcome | server to client | `ReliableOrdered` | `u16` protocol (`kNetProtocolVersion`, 1), `u32` schema hash, `u16` tick rate (60), `u16` snapshot interval, `u64` the client's `PeerId`, `u32` server tick. 20 bytes |
| `0x0101` | Snapshot | server to client | `UnreliableImmediate` | see below |
| `0x0102` | SnapshotAck | client to server | `Unreliable` | `u32` tick. 4 bytes |

A client ignores snapshots until Welcome; a protocol or schema mismatch disconnects.

Snapshot, after the id:

| Field | Size |
| --- | --- |
| tick | `u32` |
| baseline tick (`kNoTick`: full; otherwise older than tick) | `u32` |
| removed count, then each removed `NetId` | `u16` + 4 each |
| entity record count | `u16` |
| per record: `NetId` | `u32` |
| flags: bit 0 owner follows, bit 1 removed mask follows | `u8` |
| owner (a new entity always has it) | `u64` |
| changed slots mask | `u32` |
| removed slots mask | `u32` |
| per changed slot, ascending: byte length, bytes | `u16` + n |

A `Transform` is 40 bytes and a `Parent` 4. A moving entity whose `Parent` did not change is a 51-byte record; a new entity with both is 65. The client rejects the whole snapshot on truncation, trailing bytes, a slot outside its registry, a slot both changed and removed, an unknown removed id, or a new entity without an owner.

## Not in scope yet

Retries, resuming, and writing files are the game's: the engine gives it the sink, `Range`, and the error codes to do them. Connections are not reused between requests on any backend yet.

Realtime: no client input or prediction (wind-308), no per-peer relevance, send budget or fragmentation (wind-309), no dedicated server run mode and no Linux GameNetworkingSockets build (wind-310). Snapshots are neither compressed nor quantized.

## Tests

`tests/http_test.cpp`: call ownership and cancel, delivery only on `deliver`, results from another thread, abort once, the worker pool (off the main thread, cancelled while queued, cancelled while running), the body path (sink only for 2xx, abort, progress, queue order and backpressure, cancel waking a paused transfer, cancel waiting for a running sink, a `Main` sink seeing the body before the result), `StallWatch`, Content-Length parsing, `curl_transfer` against a fake libcurl (headers and redirect blocks, sink and 404, option mapping, redirect policy, cancel, idle timeout, error codes), URL and header parsing, `InvalidUrl` and `Unsupported` through `HttpClient`, and `init` per platform. No test opens a socket. The Windows backend was also run by hand against a local server (a 20 MiB download on both sink threads, redirect on and off, 404, stall, DNS and TLS errors), and `curl_transfer` was run the same way through Git for Windows' real libcurl 8.18 (the option numbers and callbacks are the same on every platform). The `dlopen` loader and the Linux and macOS client branch were only syntax-checked, with clang, not run.

Realtime, all deterministic over `LoopbackTransport`: `tests/fixtures/simulated_link.h` wraps it with virtual time, latency, seeded jitter (which reorders), scripted or seeded loss of unreliable messages, and holding messages back to release them in reverse; `tests/fixtures/net_harness.h` runs a server world and a client world over two such links, one 60 Hz frame per `step`.

- `tests/net_replication_test.cpp`: one tick per fixed step; Welcome, then a snapshot every interval; interpolation exactly at `render_tick` (tick - 5 at the defaults); snapshots arriving out of order land in tick order; dropped snapshots interpolate across the gap; spawn and despawn on the render timeline with their events; `Parent` resolved through `NetEntityMap` and cleared; reparent plus destroy of the old parent in one tick keeps the child entity; custom components (stepped and interpolated); schema mismatch disconnects; a lost connection despawns everything; owner and local peer; the server prunes destroyed entities; a soak with 20% loss, 50 ms latency and 30 ms jitter converges to the server state.
- `tests/net_delta_test.cpp`: full snapshots until an ack; a delta against the acked baseline carries only the changed entity; lost deltas recover from the older baseline; an ack older than the window falls back to full; a removed component and a removed entity reach the client.
- `tests/snapshot_codec_test.cpp`, `snapshot_buffer_test.cpp`, `snapshot_clock_test.cpp`: full and delta round trips, every truncation and trailing bytes rejected, slots beyond the registry, baseline mismatch, capture order and `Parent` as `NetId`, schema hash; buffer order, window and sampling; clock start, bounded rate, snap, monotonic under jitter.
- `tests/net_id_test.cpp`, `byte_stream_test.cpp`, `message_dispatcher_test.cpp`, `network_transport_test.cpp`: ids, map, owner, dead entries; byte round trips; message routing; the loopback transport. `gns_transport_test.cpp` and `steam_transport_test.cpp` open real sockets on Windows.

## See also

- [Core](Core.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
