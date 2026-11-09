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

Realtime networking and entity replication live in `namespace engine::net`. The server is authoritative: it simulates on the fixed step and sends snapshots of replicated components; a client shows them interpolated, slightly in the past. A client steers the entities it owns with inputs and predicts them ahead of the server ([Input and prediction](#input-and-prediction)). Each peer is told only what is relevant to it ([Interest](#interest)), within a send budget ([Budget](#budget)), and large reliable messages go through a paced queue ([Reliable payloads](#reliable-payloads)). A dedicated server run mode (wind-310) comes later.

### Transport

`include/engine/net/network_transport.h`, `gns_transport.h`, `steam_transport.h`, `loopback_transport.h`, `loopback_bus.h`.

| Type | Role |
| --- | --- |
| `INetworkTransport` | `start_server`, `connect`, `disconnect`, `stop`, `send`, `poll_events`, `rtt_ms`, `pending_send_bytes`. Main thread only |
| `GnsTransport` | IP/UDP through Valve's GameNetworkingSockets. Needs `ENGINE_WITH_GNS` ([below](#gamenetworkingsockets-builds)); without it every call fails |
| `SteamTransport` | Steam P2P through SteamNetworkingSockets, SteamID64 addresses |
| `LoopbackTransport`, `LoopbackBus` | In-process transport for tests: `send` queues the bytes on the other end at once, ignores `DeliveryMode`, and `rtt_ms` is 0 |
| `PeerId` | The transport's id of a connection. A client sees the server as `kServerPeerId` (1) |
| `DeliveryMode` | `Unreliable`, `UnreliableImmediate`, `ReliableOrdered` |
| `TransportEvent` | `Connected`, `Disconnected` (with `DisconnectReason`), `DataReceived` |

No delivery mode orders unreliable messages. `UnreliableImmediate` is `k_nSteamNetworkingSend_UnreliableNoNagle` on GNS: unreliable, sent without the Nagle delay. A receiver of unreliable data orders it itself; snapshots carry their tick. The `channel` argument is ignored by every transport.

A GNS message of any mode may be up to 512 KiB (`k_cbMaxSteamNetworkingSocketsMessageSizeSend`), and `send` fails once 512 KiB wait unsent (`k_ESteamNetworkingConfig_SendBufferSize`). An unreliable message larger than one UDP packet is split, and when any piece is lost the whole message is lost, so snapshots stay within one packet ([Budget](#budget)). Unsent bytes go out in the order they were queued at GNS's send rate (256 KiB/s by default): whatever waits there delays the next snapshot. `pending_send_bytes` reports them (`m_cbPendingReliable + m_cbPendingUnreliable`; 0 on loopback), and the payload queue stays behind that ([Reliable payloads](#reliable-payloads)).

#### GameNetworkingSockets builds

| Build | GameNetworkingSockets |
| --- | --- |
| Windows, every build | The prebuilt in `external/GameNetworkingSockets`, with its DLLs. Its headers are byte-identical to upstream `v1.6.0` |
| Linux, the Server player and the preset `linux-server` | Built from the submodule `external/GameNetworkingSockets-src` at `v1.6.0`, with OpenSSL, static protobuf and OpenSSL, no P2P ([external/README.md](../../../external/README.md)) |
| Linux editor, Development, Shipping; macOS, Web, Android | None: every call of `GnsTransport` and `SteamTransport` fails |

Limits. A Linux desktop client has no network transport; server-only is deliberate for now, because linking protobuf and OpenSSL into `libengine.so` would reach every SDK user. A Windows client (prebuilt) against a Linux server (`v1.6.0`) has **not been verified**: the headers match, the wire compatibility of the two builds has not been run. The Linux build itself has only the CI job `linux-server` to run it. `ENABLE_ICE` is off there, so `SteamTransport`'s P2P fails at run time on Linux. On Windows a second server on a port in use starts anyway, with no error from `start_server` (seen with two processes, both bound, on the prebuilt), so a server cannot detect that.

`stop()` closes each connection before the listen socket, so a peer hears the close at once instead of waiting for its timeout.

A client that closes itself must hear that too. `NetClient` despawns replicated entities only from `Disconnected`, and a local `CloseConnection` does not come back as a status callback: GameNetworkingSockets moves it to the internal `FinWait` state, and the transport drops the connection from the callback registry before closing it. So a client's `disconnect` and `stop` enqueue `Disconnected` for `kServerPeerId` with `ClosedByPeer` themselves, and the next `poll_events` delivers it once. `stop` clears anything already queued, then enqueues that one event. A later `disconnect` or `stop`, or one before a connection exists, enqueues nothing. Loopback does the same on `disconnect` and `stop`.

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

A client owner means "this peer controls it with inputs". With a prediction channel (`register_prediction`), the server steps every entity a connected client owns with that client's inputs, and that client predicts it. There is no opt-in marker: an entity a client should not steer (its projectile, its pet) stays server-owned. Changing `NetIdentity::owner` on the server hands control over at the next snapshot.

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
- `SdfCollider2D` ([ECS](ECS.md#sdf-colliders)) is not registered and should not be: its field is kilobytes per chunk and every snapshot would carry it. Replicate destructible terrain as its edits instead: the server sends each carve or fill (shape and arguments) as a game message, every peer applies it to its own field in the same order and calls `mark_static_colliders_dirty_2d`, so all fields stay bit-identical (the edits are deterministic). A late joiner gets the field once, for example through a reliable payload of the world's `sdf_collider_2d` table, then the edits after it.
- At most 32 components (`kMaxReplicated`). Registering a name twice, or a 33rd component, logs an error and is ignored (and asserts in Debug).
- The server and every client must register the same list in the same order. `schema_hash()` (FNV-1a over the names in slot order) travels in Welcome; a client whose hash differs logs an error and disconnects.

### Schedule

`include/engine/net/net_server.h`, `net_server_config.h`, `net_client.h`, `net_client_config.h`.

| Call | Registers |
| --- | --- |
| `install_net_server(world, transport, NetServerConfig{})` | `run_net_server_receive` on `Fixed`/`NetReceive`, `run_net_server_send` on `Fixed`/`NetSend` |
| `install_net_client(world, transport, NetClientConfig{})` | `run_net_client_receive`, then `run_net_client_interpolate`, on `Frame`/`NetReceive`; `run_net_client_predict` on `Fixed`/`NetReceive`; `run_net_client_send_input` on `Fixed`/`NetSend` |

`Phase::NetReceive` opens both schedules and `Phase::NetSend` closes `Fixed` ([ECS](ECS.md#systems-and-schedules)), so one server tick brackets the whole fixed step and a client's frame systems already see this frame's replicated state. The game owns the transport and keeps it alive while the world runs; `NetServer` and `NetClient` never touch it from their destructors. A second `install_*` call replaces the transport and config without adding systems again.

`world.ctx<NetServer>()`: `tick()`, `config()`, `dispatcher()`, `peers()`, `acked_tick(peer)`, `input_stats(peer)`, `input_tick(peer)`, `set_peer_focus(peer, focuses)`, `peer_stats(peer)`, `send_payload`, `cancel_payload`, `set_payload_priority`, `payload_progress`, `queued_payload_bytes(peer)`. `world.ctx<NetClient>()`: `welcomed()`, `local_peer()`, `newest_tick()`, `render_tick()`, `buffered_ticks()`, `dispatcher()`, `predicted_tick()`, `replaying()`, `is_predicted(id)`, `input_slack()`, `server_input_tick()`, `prediction_stats()`.

The client's two `Fixed` systems do nothing without a prediction channel, so a client that only watches is as before: every replicated entity is interpolated.

Events (each in its own header): `NetPeerConnectedEvent`, `NetPeerDisconnectedEvent` on both sides; on the client, `NetEntitySpawnedEvent{entity, net_id}` (replicated components already applied; the game adds sprite, mesh, sound) and `NetEntityDespawnedEvent{entity, net_id}` (sent as the entity is destroyed, so it is dead when read); on the server, `NetPayloadDeliveredEvent{peer, id}`.

### Server ticks

`run_net_server_receive` polls the transport: a new peer is added and sent Welcome, a lost one is dropped, an ack is recorded, inputs are buffered ([Server input buffer](#server-input-buffer)), a payload ack becomes an event, other messages go to `dispatcher()`. Then it advances the tick: the first fixed step is tick 1, `kNoTick` (0) means none yet. With a prediction channel it steps every controlled entity with its input for that tick. Physics and `Game` systems simulate the rest of the tick. `run_net_server_send` unbinds destroyed entities from `NetEntityMap`; on every `snapshot_interval`-th tick (default 2: 30 snapshots a second at the 60 Hz step) it captures every entity with `NetIdentity` once and sends each peer a snapshot tagged with the tick. Then, on every tick, it sends each peer's queued payloads from what is left of its budget.

A world whose `Fixed` schedule does not run (paused, not stepping) neither polls the server transport nor sends snapshots; events wait in the transport. `Tick` is `u32`: it wraps after about 2.2 years at 60 Hz, which nothing handles.

### Snapshots and acks

A snapshot tells a peer what changed. `capture_snapshot` encodes every entity once per send tick (an entity whose bytes did not change shares the previous capture's blobs, so comparing it is one pointer compare), and each peer's message is built from that one capture: blobs are never serialized per peer. A message is at most `snapshot_budget_bytes`.

#### What a peer has

`PeerGhosts` (`src/net/peer_ghosts.h`), one per peer, tracks each relevant entity on its own. There is no whole-state baseline. wind-307 had one, and it cannot survive snapshots that leave entities out: with T0 and T1 both decoded against the acked B and T1 omitting what T0 wrote, the client's state at T1 takes those entities back to B, and an entity new in T0 despawns. With three snapshots in flight at 100 ms that happens constantly. Repeating in-flight writes until acked does not fix it either: a moving entity is always in flight, so it would never release its budget.

A slot's bytes are a whole value, so nothing needs a base to decode:

- The client derives `state(T)` = the state before T with T's records on top, in tick order. A record writes the slots it names and removes the slots it lost; everything else carries forward. So slots and existence are last-write-wins by tick, and an omitted entity simply keeps its value.
- A snapshot arriving late is put in its place and the states after it are derived again (`SnapshotBuffer`), so arrival order does not matter. A state leaving the 128-tick window (`kSnapshotWindowTicks`) lives on in the states after it; one older than that is dropped unacknowledged.
- A ghost holds `latest` (the entity as last written), `since[slot]` and `owner_since` (the tick each current value was first written) and `confirmed` (the newest acked snapshot that wrote this entity). **A slot is clean when `current == latest[slot]` and `confirmed >= since[slot]`.** Otherwise it is dirty, and a record writes all of the entity's dirty slots together.
- Every ack counts, whatever order it arrives in, if that peer was sent the snapshot and it is inside the window. A lost ack only costs rewrites until a later one lands.
- `since` is what makes a value that comes back safe. The server goes A, C, A; C reached the client but its ack was lost; comparing with the last acknowledged A would call the slot clean and leave the client on C. Here A's `since` is newer than `confirmed`, so A keeps being written until a snapshot with it is acked.
- A new ghost has `confirmed = 0`, so the whole entity with its owner is written until acked. Once `confirmed` covers the create, a record can leave the owner out, and the client must then hold the entity: an update without an owner for an unknown entity is refused on first insert (no ack). During a re-derive that record is skipped.
- A ghost that leaves relevance (or whose entity is destroyed) is listed in `removed` until a snapshot listing it is acked, then erased. One the peer never heard of is erased silently. A removed id the client does not hold is normal: its create may have been lost. Coming back before the ack starts over: all `since` move to now, so an ack from before says nothing.

The client acks every snapshot it holds (`SnapshotAck`, unreliable). `acked_tick(peer)` is the newest acknowledged.

Snapshots are not compressed: writing only what is dirty is the saving, and LZ4 on 50-1200 bytes of floats gains little for its cost per peer per tick. Values are not quantized.

### Interest

`include/engine/net/net_interest.h`, `net_always_relevant.h`; `src/net/relevance_grid.h`.

`NetServerConfig::interest` says which entities each peer is told about. The default tells everyone everything.

```cpp
engine::net::NetServerConfig config;
config.interest.cell_size = 16.0f;      // a chunk
config.interest.radius_cells = 6;
config.interest.leave_margin_cells = 1;
config.interest.relevant = [](const engine::ecs::World& w, engine::ecs::Entity e, engine::net::PeerId peer) { ... };
engine::net::install_net_server(world, transport, std::move(config));
```

An entity is relevant to a peer, at each send, when any of these holds:

1. **The peer owns it.** Prediction depends on it; nothing can turn this off.
2. It has `NetAlwaysRelevant`.
3. It passes the grid and `relevant`: with no grid (`cell_size <= 0`) only `relevant`, with neither everything. An entity whose root has no `Transform` has no place in the grid and passes it.
4. A relevant entity hangs from it (see below).

The grid is rebuilt once per send tick, from each entity's **root** `Transform.position` (a child's own `Transform` is local), so a tree moves as one. A peer looks from its focuses: the root position of everything it owns, plus `set_peer_focus(peer, points)` for a spectator camera or the like. An entity enters within `radius_cells` of a focus on every axis (Chebyshev), and once relevant stays `leave_margin_cells` further, so one on the edge does not flap. A peer with no focus sees only rules 1, 2, 4 and what has no place in the grid.

Entering and leaving fit the snapshot model unchanged: the entity appears in or disappears from that peer's states, so `NetEntitySpawnedEvent` and `NetEntityDespawnedEvent` happen on the render timeline as for any other. A reentering entity is written whole, with its current values.

**Parents.** A relevant child pulls in its replicated ancestors, whatever the filter says: without them the client would place the child in the wrong space. It does not go the other way, so `relevant` can keep a parent's attachments from a peer. A new child goes out in the same snapshot as any ancestor that was never written. A custom component's `write_entity` reference to an entity that is not relevant arrives unresolved, and resolves by itself when it becomes relevant, since components are applied every frame.

**Cost, per send tick.** Capture and grid rebuild once: O(E) serialization, O(E log E) sorting. Per peer: cell marking at the cheaper of `(2r+1)^3` probes or a scan of the occupied cells, per focus; an O(E) sweep of byte marks; `relevant` at most once per candidate; an O(ghosts) walk with a pointer compare for what did not change. A sort only when the snapshot does not fit.

### Budget

`NetServerConfig`: `snapshot_budget_bytes` (1200, `kSnapshotMtuBudget`: one UDP packet, about 23 moving `Transform`s), `peer_bytes_per_second` (192 KiB, under GNS's 256 KiB), `payload_min_share` (0.1), `payload_aging_per_second` (1), `transport_backlog_bytes` (8 KiB). `NetServer::peer_stats(peer)` gives `relevant`, `deferred`, `last_snapshot_bytes`, `payload_bytes_sent`.

Each fixed tick, per peer: `credit` earns `peer_bytes_per_second / 60` (capped at two snapshot intervals' worth, at least two fragments). On a snapshot tick the snapshot goes first and unconditionally. Then payload fragments go while `credit` covers them.

A snapshot may take `min(snapshot_budget_bytes, (1 - payload_min_share) * rate per tick * snapshot_interval)`. When everything dirty fits, all of it goes, as without a budget. Otherwise each dirty entity's accumulator grows by its weight (`NetInterest::priority`, or `1 / (1 + cells to the nearest focus)`, clamped to 0.01..1), and the snapshot is filled in this order:

1. Owned entities when a prediction channel exists. They are never deferred, because reconciliation reads their state at the newest tick. Without a channel an owned entity ranks at the top weight and can wait.
2. The entity with the largest accumulator (ties to the lowest id), whatever its size, so a large record cannot wait forever.
3. Removals, least recently listed first, up to a quarter of what is left while records wait.
4. The rest by accumulator, first-fit.

A written entity's accumulator goes back to 0. A snapshot goes over its budget only for 1 and 2; the server logs one warning per peer, and GNS splits it.

Guarantees:

- **Entities.** A dirty entity's accumulator grows at least 0.01 a send while anything written resets, and the top always goes, so it is written within O(N * 100) sends at worst, and in about N / (records per snapshot) sends with even weights. Under pressure, a write that may have been lost competes as any other; nothing is pinned until acked.
- **Snapshots** are never held back by payloads: first each tick, unconditional, and the transport's queue ahead of them is kept short ([Reliable payloads](#reliable-payloads)).
- **Payloads** always get `payload_min_share` of the rate: a snapshot is debited at most its allowance, even when mandatory records take it over, so in that one case the rate is exceeded. And a low priority ages past any stream of higher ones.

A deferred entity holds its last value and moves over one snapshot interval when written. It does not go back and does not flap.

### Reliable payloads

`NetServer::send_payload`, `<engine/net/net_payload_options.h>`, `net_payload_id.h`, `net_payload_progress.h`, `net_payload_delivered_event.h`; `src/net/payload_queue.h`, `payload_assembler.h`.

For large reliable data: chunks, a map. A payload is a game message of any size, under a game `MessageId`, and the client gets it through the same `dispatcher()` handler as any other message:

```cpp
// Server. Compress on a job, once for all peers.
auto chunk = std::make_shared<const std::vector<std::uint8_t>>(engine::compress(raw));
const engine::net::PayloadId id = net.send_payload(peer, kChunkMessage, chunk, {.priority = -distance, .compressed = true});
net.set_payload_priority(id, -new_distance);
net.cancel_payload(id);

// Client. The handler sees the raw bytes.
client.dispatcher().register_handler(kChunkMessage, [](engine::net::PeerId, engine::net::ByteReader& in) { ... });
```

- **Order.** Highest `priority + age * payload_aging_per_second` next, ties to the older. Re-evaluated per fragment, so a new or re-prioritized payload preempts at a fragment boundary. Payloads may finish in any order, relative to each other and to messages sent straight through the transport (which bypass the budget, so keep those small).
- **Fragments.** At most `kPayloadFragmentBytes` (1024) of data per reliable message. GNS's 512 KiB limit is not what forces this at chunk size: one large message would sit in the transport's queue and delay the snapshots behind it, could not be cancelled or preempted, and could not be paced. It also lifts the limit: up to 4 GiB, whatever the client's `max_payload_bytes` allows.
- **Backpressure.** No fragment goes while the transport holds `transport_backlog_bytes` unsent for that peer, so a slow or lossy link slows the terrain and not the movement. A `send` that fails (GNS's send buffer) is tried again later, with the same fragment.
- **Arrival.** The client acks after its handler ran; the server sends `NetPayloadDeliveredEvent{peer, id}`. `payload_progress(id)` is `{sent_bytes, total_bytes, awaiting_ack}`, nullopt afterwards. `queued_payload_bytes(peer)` lets a game pace what it queues.
- **Cancel.** A payload not started sends nothing; one partly sent sends PayloadCancel so the client frees the part. If the last byte has already gone, the client's handler may still run; no event follows.
- **Disconnect** drops everything on both sides, with no event: `NetPeerDisconnectedEvent` is the signal, and a new connection is a new peer.
- **Compression is the game's.** The engine never compresses: LZ4 on the main thread per peer is wasteful, and only the game knows its data. `compress` is callable from a job ([Core](Core.md#compression)). With `.compressed = true` the flag travels on the wire and the client decompresses, under `max_payload_bytes`, before the handler.
- **Malformed.** The stream is reliable and ordered, so a bad message means the two sides disagree: the client logs an error and disconnects. That covers truncation, a fragment of nothing or at the wrong offset, bytes beyond the declared size, a duplicate begin, an engine message id, unknown flags, a size over `NetClientConfig::max_payload_bytes` (default 64 MiB), unfinished bytes over it, more than `PayloadAssembler::kMaxOpen` unfinished, and bytes that do not decompress. Memory grows as bytes arrive, never from a claimed size. On the server, an ack from another peer or for an unknown id is ignored.

### Client clock and interpolation

`SnapshotClock` (`src/net/snapshot_clock.h`) estimates the server time of the newest snapshot as of now. The first snapshot sets it; afterwards it runs with `Time::delta_time`, at 95%-105% speed, pulled toward each newer snapshot's `tick / tick_hz` (a smoothed error, gain 2/s). It never steps backward because of jitter. An error above 0.25 s (a stall, a server restart) snaps it.

`render_tick = (clock - interpolation_delay) * tick_hz`, with `NetClientConfig::interpolation_delay` 0.1 s by default (three snapshot gaps at 30 Hz). `SnapshotBuffer` (`src/net/snapshot_buffer.h`) keeps the decoded states sorted by tick, whatever order they arrived in; duplicates and states outside the window are dropped. Each frame `run_net_client_interpolate` samples it: `from` is the newest state at or before `render_tick`, `to` the next one. With no `to` the newest state holds (nothing is extrapolated); with no `from` yet nothing is applied. A lost snapshot just widens the gap between `from` and `to`.

Then, in this order:

1. Spawn: every entity of `from` exists. A missing one is created, bound, and given `NetIdentity`. An entity that was destroyed under the client (by a parent's cascade, or by game code) is made again.
2. Components, slot by slot, after every entity exists so `Parent` references resolve: a slot in both `from` and `to` with `interpolate` is interpolated at `t`, otherwise `from`'s value is applied; a slot the entity had and `from` lacks is removed.
3. Despawn: an entity that `from` no longer has gets `NetEntityDespawnedEvent`, is unbound and destroyed. This runs last so a child reparented away in the same snapshot is not destroyed with its old parent.

So spawn and despawn happen on the interpolation timeline: an entity appears when `render_tick` reaches the first snapshot that has it and disappears when it reaches the first one that does not. There are no spawn or despawn messages, so a snapshot can never arrive before its entity's spawn. Every replicated component is decoded and applied each frame; that is simple and fine at today's entity counts, and a typed cache can replace it later.

When the connection ends, the client despawns every replicated entity (with events), clears its buffer and clock, stops predicting (rings cleared, `predicted_tick()` back to `kNoTick`, stats zeroed, `Time::fixed_rate` back to 1), and forgets Welcome.

### Input and prediction

`include/engine/net/prediction_desc.h`, `prediction.h`, `prediction_channel.h`, `net_prediction.h`, `prediction_stats.h`, `net_input_stats.h`.

A game describes its controlled entities once, generic over its own `Input` and `State`, and registers the same description on the server and on every client, before connecting:

```cpp
struct MoveInput { float move_x{0.0f}; bool jump{false}; float yaw{0.0f}; float pitch{0.0f}; };
struct PlayerState { glm::vec3 position{}; glm::vec3 velocity{}; bool grounded{false}; };

engine::net::register_prediction<MoveInput, PlayerState>(world, {
        .name = "MoveInput",
        .write_input = [](engine::net::ByteWriter& out, const MoveInput& in) { ... },
        .read_input = [](engine::net::ByteReader& in, MoveInput& out) { ...; return in.is_ok(); },
        .step = [](engine::ecs::World& w, engine::ecs::Entity e, const MoveInput& in) { ... one fixed tick ... },
        .collect = [&input = services.input](engine::ecs::World& w, engine::ecs::Entity) { ... },
        .capture = [](const engine::ecs::World& w, engine::ecs::Entity e) { ... },
        .restore = [](engine::ecs::World& w, engine::ecs::Entity e, const PlayerState& s) { ... },
});
```

| Hook | Side | Role |
| --- | --- | --- |
| `name` | both | Folded into the Welcome schema hash: a client registering a different name, or none, disconnects |
| `write_input`, `read_input` | both | One input on the wire. `read_input` starts from `Input{}` and must use every byte |
| `step` | both | One fixed tick of one entity with one input. The same function runs on the server, for each predicted tick, and for each replayed tick |
| `collect` | client | This tick's input, read from `InputSystem` through `ActionId`s ([Input Mapper](../features/Input%20Mapper.md#net-input)) |
| `capture`, `restore` | client | Read and write the entity's `State` |
| `matches` | client | Optional. Whether a prediction is close enough to the server's state. Empty: `operator==` if `State` has one, otherwise never (every server state replays) |
| `corrected` | client | Optional. Called after a replay with the state before and after it, for visual smoothing |

One input type per world: a second `register_prediction` logs an error and is ignored (asserts in Debug). `Prediction<Input, State>` is the class behind it; `world.ctx<NetPrediction>().channel` holds it as a `PredictionChannel`, the type-erased interface `NetServer` and `NetClient` drive.

Rules for the game:

- **`State` is everything `step` writes, and all of it is replicated.** The client learns the server's state by applying a snapshot's replicated components to the entity and calling `capture`. A component `step` writes that is not part of `State` is left at the snapshot's older value after a match. A replicated component outside `State` (health, a name) takes the newest snapshot's value on a predicted entity.
- **`step` is the only simulation of a controlled entity.** Anything else that changes its `State` (a server-side knockback, a teleport, `run_physics_2d`, `run_physics_3d`) is a misprediction the client corrects by replaying. Do not give a controlled entity a `RigidBody2D` or a `RigidBody3D`. Both systems run outside `step`. A kinematic body still integrates there, and a dynamic body is solved there.
- **`step` has no side effects while `NetClient::replaying()` is true.** No events, sounds, or spawns: a replayed tick already happened once.
- **Floats.** The server and the client must step the same way. Identical binaries on one platform reproduce exactly; across compilers or CPUs, give `matches` a tolerance, or every server state replays.

The engine calls `step` from `NetReceive`, on both sides: it is the one exception to "no gameplay in the net phases". A controlled entity is therefore stepped first in its tick, before `Physics` and the game's `Game` systems, whatever order those were registered in.

#### Ticks: one numbering, the client ahead

Client and server count the same ticks. The client's fixed step simulates tick `predicted_tick()`, ahead of the server; its input for tick t is the input the server applies at its own tick t. So the server's "jitter delay" is how early the client sends: its inputs arrive `input_buffer_ticks` (default 2) before the server needs them and wait in the server's buffer.

The client now has two timelines:

| Timeline | Where | What runs on it |
| --- | --- | --- |
| Render, `render_tick()` | behind the server by one-way latency plus `interpolation_delay` | Interpolated entities, spawn and despawn of every entity |
| Predicted, `predicted_tick()` | ahead of the server by one-way latency plus `input_buffer_ticks` | The entities this client owns |

They are independent: `SnapshotClock` is unchanged, and the predicted tick advances once per client fixed step.

#### Client lead

The first snapshot after Welcome starts the predicted tick at `newest + ceil(RTT / tick) + input_buffer_ticks + 1`, with RTT from `INetworkTransport::rtt_ms` (0 on loopback). After that a closed loop corrects it. There is no Ping/Pong and no clock sync: each snapshot carries `input_slack`, how many ticks early this client's input messages arrived (the smallest `newest input tick - the tick the server runs next` over the messages since its previous snapshot). `InputLead` (`src/net/input_lead.h`) turns the shortfall `input_buffer_ticks - input_slack` into a fixed-step rate:

- The shortfall is smoothed (factor 0.2, a single report clamped to ±8 ticks) and gives `Time::fixed_rate = 1 + clamp(0.05 * error, ±0.1)`: the client's world runs up to 10% more or fewer fixed steps per second ([Runtime Loop](../architecture/Runtime%20Loop.md)). A report is a round trip old, so the rate change made since the reported input was sent (a per-tick history, 256 ticks) is added to it; without that the loop swings around the target on a slow link. Between reports each fixed step subtracts what it closed.
- Two reports in a row more than 8 ticks short (inputs arriving far too late, after the latency grew) jump the predicted tick forward by the shortfall instead. The predicted states are dropped and the inputs stay: the next server state replays up to the new tick with the inputs the server ran, repeating the last one as it does. Reports from snapshots before the new predicted tick are ignored, since they predate the jump. A single late report (a burst released after a lag spike) only nudges the rate.
- It never jumps backward: tick numbers already sent would collide with inputs the server holds. A client too far ahead slows down at -10%; the server ignores inputs more than `kMaxInputLeadTicks` (64) ahead.
- Without a predicted entity the rate stays 1. Only the client world's net system changes `fixed_rate`, and a disconnect sets it back to 1.

#### Each client fixed step

`run_net_client_predict`, on `Fixed`/`NetReceive`:

1. With no channel, no Welcome, or no snapshot yet, nothing. Starts the predicted tick if needed.
2. The predicted set: live entities that the newest snapshot gives to `local_peer()`. A new one starts an empty ring; one this client no longer owns is forgotten and is interpolated again from the next frame (it drops back to the render timeline, a visible jump). An entity destroyed under the client and re-made starts a new ring.
3. Reconciles each predicted entity with the newest snapshot (below).
4. Advances the predicted tick to t; for each predicted entity, `collect`s the input for t, stores it, and `step`s.

`run_net_client_send_input`, on `Fixed`/`NetSend`: records each predicted entity's state at the end of t (the server captures its snapshot at the same point), then sends one Input message with the last `input_redundancy` (default 8) collected inputs of each, newest first. Redundancy is fixed, not trimmed by acks: about 100 bytes a tick upstream for a 10-byte input.

`run_net_client_interpolate` skips component application on predicted entities. Spawn and despawn stay on the render timeline for every entity, so the client's own entity appears when `render_tick` reaches its first snapshot: about `interpolation_delay` plus a round trip after the server created it.

#### Reconciliation

Each `Prediction` track is a ring of `kPredictionRingTicks` (128) slots `{tick, input, state}`. For the newest snapshot S, once per S:

1. `present = capture`. Apply S's replicated components to the entity (and remove the ones it lost), then `server = capture`.
2. S after the predicted tick (the client is behind): `server` is the present. The lead loop catches up.
3. The ring has the state predicted for S and `matches` it: `restore(present)`. Nothing is replayed (`PredictionStats::matched`).
4. S more than 127 ticks behind the predicted tick: the inputs are gone; `server` is the present and the ring is cleared (`snaps`).
5. Otherwise: `restore(server)`, then for each tick S+1 to the predicted tick, `step` with its stored input and record the state (`replays`, `replayed_ticks`). A tick with no stored input repeats the last input before it, or `Input{}` before any, exactly what the server did for it; such a stand-in is never sent. `replaying()` is true meanwhile. Then `corrected(before = present, after)`.

Edge cases: an old, duplicate, or out-of-order snapshot never reconciles (only a newest tick beyond the last reconciled one does); a lost snapshot waits for the next one; the first server state of a new track has no prediction and replays from it (with stand-ins), which matches what the server ran; a teleport or other large correction is just a replay, and `corrected` lets the game skip smoothing above its own threshold.

Smoothing is the game's, not the engine's (it does not know where a position is in `State`). The recommended pattern: render the player through a non-replicated child entity (`ecs::set_parent`); in `corrected`, add `before - after` to the child's local offset, unless it is larger than a teleport threshold; decay the offset on `Frame`.

#### Server input buffer

`InputBuffer` (`src/net/input_buffer.h`), one per controlled entity of each peer:

- An Input message is dropped whole (`malformed`) when it is truncated, over a cap, or `read_input` refuses one of its inputs. A section for an entity that does not exist or that this peer does not own is skipped (`rejected`, one warning per peer).
- Each input of a section is stored unless its tick already ran (`late` if that tick repeated an input, `duplicate` if it ran with this one), is more than `kMaxInputLeadTicks` ahead (`too_far_ahead`), or is held already (`duplicate`). The first copy wins.
- After advancing to tick S, every entity owned by a connected client is stepped once, in `NetId` order, with the input for S (`applied`), or the last one again (`repeated`), or `Input{}` before the first input arrives. An entity whose owner disconnected is not stepped; the game decides its fate on `NetPeerDisconnectedEvent`. A stream whose entity died or changed owner is dropped.

So each input tick is applied at most once and in tick order, and a controlled entity is stepped exactly once per server tick. Each snapshot tells its peer `input_tick` (the newest tick that ran with its own input) and `input_slack`.

### Wire format

Little-endian. Every message starts with its `u16` id.

| Id | Message | Direction | Delivery | Fields after the id |
| --- | --- | --- | --- | --- |
| `0x0100` | Welcome | server to client | `ReliableOrdered` | `u16` protocol (`kNetProtocolVersion`, 3), `u32` schema hash, `u16` tick rate (60), `u16` snapshot interval, `u64` the client's `PeerId`, `u32` server tick. 20 bytes |
| `0x0101` | Snapshot | server to client | `UnreliableImmediate` | see below |
| `0x0102` | SnapshotAck | client to server | `Unreliable` | `u32` tick. 4 bytes |
| `0x0103` | Input | client to server | `UnreliableImmediate` | see below |
| `0x0104` | PayloadBegin | server to client | `ReliableOrdered` | `u32` id, `u16` game message id, `u8` flags (bit 0 compressed), `u32` total bytes, then up to 1024 data bytes |
| `0x0105` | PayloadFragment | server to client | `ReliableOrdered` | `u32` id, `u32` offset (exactly the bytes received so far), then up to 1024 data bytes |
| `0x0106` | PayloadCancel | server to client | `ReliableOrdered` | `u32` id. 4 bytes |
| `0x0107` | PayloadAck | client to server | `ReliableOrdered` | `u32` id. 4 bytes |

A client ignores snapshots until Welcome; a protocol or schema mismatch disconnects. The schema hash is `ReplicationRegistry::schema_hash`, then, with a prediction channel, `"input:"` and its name folded in (FNV-1a).

Snapshot, after the id:

| Field | Size |
| --- | --- |
| tick | `u32` |
| input tick: the newest tick that ran with this peer's own input (`kNoTick`: none) | `u32` |
| input slack: how many ticks early this peer's inputs arrived, clamped to ±127 (`kNoInputSlack`, -128: none arrived since its previous snapshot) | `i8` |
| removed count, then each removed `NetId` | `u16` + 4 each |
| entity record count | `u16` |
| per record: `NetId` | `u32` |
| flags: bit 0 owner follows, bit 1 removed mask follows | `u8` |
| owner (always, until the server knows the client holds the entity) | `u64` |
| changed slots mask | `u32` |
| removed slots mask | `u32` |
| per changed slot, ascending: byte length, bytes | `u16` + n |

A `Transform` is 40 bytes and a `Parent` 4. A moving entity whose `Parent` did not change is a 51-byte record; a new entity with both is 65. The fields before the removed list are 9 bytes, so an empty message is 15 with its id and two counts. The client rejects the whole snapshot, without an ack, on truncation, trailing bytes, a slot outside its registry, a slot both changed and removed, an id both removed and written, or a record without an owner for an entity it does not hold.

Input, after the id (`src/net/input_message.h`):

| Field | Size |
| --- | --- |
| newest tick (not `kNoTick`) | `u32` |
| entity count, 1..`kMaxInputEntities` (8) | `u8` |
| per entity: `NetId` | `u32` |
| per entity: input count n, 1..`kMaxInputsPerMessage` (32), at most `newest` | `u8` |
| per input, newest first (input i is for tick `newest - i`): byte length (at most `kMaxInputBytes`, 256), bytes | `u16` + n |

## Not in scope yet

Retries, resuming, and writing files are the game's: the engine gives it the sink, `Range`, and the error codes to do them. Connections are not reused between requests on any backend yet.

Realtime: snapshots are neither compressed nor quantized. A dedicated server is [Core](Core.md#dedicated-server); the realtime layer does not know it runs in one.

Interest and budget, known follow-ups:

- **A deferred entity hops.** Under budget pressure it holds, then moves over one snapshot interval (33 ms), because interpolation runs between adjacent snapshots. It never goes back, but far entities visibly stutter. Gliding over the interpolation delay needs a per-entity value tick and a forward scan of the buffer.
- **Despawn has no reason.** `NetEntityDespawnedEvent` does not say whether the entity left relevance or was destroyed, so do not hang a death effect on it. A game signals death through its own replicated component (a `Dead` flag, seen before the despawn) or its own message.
- **Payloads go server to client only.** A client sends large data by splitting it into its own messages.
- GNS lanes are not used: pacing and the backlog limit keep snapshots ahead of payloads instead. `snapshot_interval` is the same for every peer, and the rate is configured rather than measured.

Prediction, known follow-ups:

- No sub-step visual interpolation: a predicted entity moves in 60 Hz steps, like every fixed-step entity today (nothing reads `Time::alpha`).
- No lag compensation: a replay rewinds only the predicted entity, never the others, and the server does not rewind the world to what a client saw when it shot.
- One input type per world; owned means controlled, with no opt-in marker.
- The client's own entity spawns on the render timeline, so it appears an interpolation delay plus a round trip after the server created it.
- The input buffer target is fixed (`input_buffer_ticks`), not adapted to measured jitter.

## Tests

`tests/http_test.cpp`: call ownership and cancel, delivery only on `deliver`, results from another thread, abort once, the worker pool (off the main thread, cancelled while queued, cancelled while running), the body path (sink only for 2xx, abort, progress, queue order and backpressure, cancel waking a paused transfer, cancel waiting for a running sink, a `Main` sink seeing the body before the result), `StallWatch`, Content-Length parsing, `curl_transfer` against a fake libcurl (headers and redirect blocks, sink and 404, option mapping, redirect policy, cancel, idle timeout, error codes), URL and header parsing, `InvalidUrl` and `Unsupported` through `HttpClient`, and `init` per platform. No test opens a socket. The Windows backend was also run by hand against a local server (a 20 MiB download on both sink threads, redirect on and off, 404, stall, DNS and TLS errors), and `curl_transfer` was run the same way through Git for Windows' real libcurl 8.18 (the option numbers and callbacks are the same on every platform). The `dlopen` loader and the Linux and macOS client branch were only syntax-checked, with clang, not run.

Realtime, all deterministic over `LoopbackTransport`: `tests/fixtures/simulated_link.h` wraps it with virtual time, latency, seeded jitter (which reorders), scripted or seeded loss of unreliable messages, and holding messages back to release them in reverse; `tests/fixtures/net_harness.h` runs a server world and a client world over two such links, one 60 Hz frame per `step`; `add_client` adds more clients.

- `tests/net_replication_test.cpp`: one tick per fixed step; Welcome, then a snapshot every interval; interpolation exactly at `render_tick` (tick - 5 at the defaults); snapshots arriving out of order land in tick order; dropped snapshots interpolate across the gap; spawn and despawn on the render timeline with their events; `Parent` resolved through `NetEntityMap` and cleared; reparent plus destroy of the old parent in one tick keeps the child entity; custom components (stepped and interpolated); schema mismatch disconnects; a lost connection despawns everything; owner and local peer; the server prunes destroyed entities; a soak with 20% loss, 50 ms latency and 30 ms jitter converges to the server state.
- `tests/net_delta_test.cpp`: every entity rewritten whole until acked; acked entities not rewritten; lost snapshots recover; lost acks only cost rewrites; a removed component and a removed entity reach the client; a removal is listed until acked; a value that came back while another was in flight reaches the client; peers ack independently.
- `tests/net_interest_test.cpp`: no interest replicates everything; grid enter and leave spawn and despawn on that peer only, on the render timeline; the leave margin stops flapping and does not let an entity in; owned entities are always relevant and are focuses; the filter hides from one peer and runs only on candidates; filter without a grid; always-relevant and non-spatial; a child uses its root's cell and brings its ancestors; an owned child pulls in far ancestors; a filtered child stays out; capture serializes once per tick with three peers; reentry carries fresh state; leaving and reentering with snapshots in flight ends consistent; destroyed out of view lists nothing; snapshots keep coming with nothing relevant.
- `tests/net_budget_test.cpp`: 200 moving entities never exceed the budget, configured budget and low rate apply; an oversized set converges exactly and arrives from a join; no entity starves at 100:1 weights; **values never go backward and nothing flaps** with snapshots in flight (the regression test of per-entity acks), also under loss and reorder; closer entities update more often; a record over the budget still goes; a predicted entity is in every snapshot among 200 others, with no snaps; an owned one without prediction ranks first; a child's create never precedes its parent's.
- `tests/net_payload_test.cpp`: small, empty, and 1 MiB payloads reassembled through the dispatcher and acknowledged; priority order; re-prioritizing preempts at a fragment boundary; cancel before, during, and after the last byte; a snapshot first on every due tick, with movement undisturbed; the rate holds over a window; a payload is not starved by an oversized set; a low priority ages past a stream of high ones; backlog pauses payloads and not snapshots; a refused send retries with nothing lost; compressed bytes decompressed before the handler, a decompression bomb disconnects; peer disconnect mid-payload clears both sides; refused ids and peers; shared bytes to several peers; a forged ack is ignored; 14 malformed streams each disconnect without running the handler.
- `tests/peer_ghosts_test.cpp`, `relevance_grid_test.cpp`, `payload_assembler_test.cpp`: writes until acked, unknown and out-of-window acks, ack order, removal listing, never-written ghosts, reentry, turns under a budget, weights, mandatory and oversized records, parents, removals not crowding out records; cell floor, Chebyshev, probing and scanning agree; interleaving, compression, limits, every malformed case.
- `tests/net_prediction_test.cpp`, over 50 ms links: each input tick applied once and in order with the input the client collected; no replay while predictions match; a server-side move replays once and converges; 20% loss both ways with 30 ms jitter, then a clean link, converges and stops replaying; dropped Input messages repeat the last input on the server; late inputs are never applied; input for an entity the peer does not own, too far ahead, or malformed is refused; owned entities predicted, others interpolated; the lead settles at the input buffer and follows a latency increase; a latency spike jumps the tick forward, never back, and recovers; ownership change starts and stops prediction; disconnect clears it; a prediction on one side only is a schema mismatch; input collected through `InputSystem` actions.
- `tests/prediction_test.cpp`, `input_buffer_test.cpp`, `input_lead_test.cpp`, `input_message_test.cpp`: match without stepping, replay of stored inputs, missing inputs repeat the last one and are not sent, accepted, snapped and skipped server states, the ring per entity, server-side decoding; first copy wins, late and duplicate, repeat, lead cap; bounded rate, no backward jump, jump after two far reports, convergence through the feedback delay; Input round trip, truncation, caps.
- `tests/snapshot_codec_test.cpp`, `snapshot_buffer_test.cpp`, `snapshot_clock_test.cpp`: round trips, records layered on the previous state, the per-peer header, every truncation and trailing bytes rejected, slots beyond the registry, unknown entities, capture order and `Parent` as `NetId`, capture sharing unchanged blobs, schema hash; buffer order, window and sampling, carrying forward, late snapshots derived back in, what left the window; clock start, bounded rate, snap, monotonic under jitter.

- `tests/net_id_test.cpp`, `byte_stream_test.cpp`, `message_dispatcher_test.cpp`, `network_transport_test.cpp`: ids, map, owner, dead entries; byte round trips; message routing; the loopback transport, including a local `disconnect` and `stop` reporting `Disconnected` to the client that closed. `gns_transport_test.cpp` and `steam_transport_test.cpp` open real sockets on Windows; the former also sends a 600 KiB payload through `NetServer` over real GNS, with `pending_send_bytes` from the real queue, and checks that a client's own `disconnect` and `stop` each report `Disconnected` once.

## See also

- [Core](Core.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
