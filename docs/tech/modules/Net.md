# Net

HTTP and HTTPS requests. One frontend, `IHttpClient`. The backend is chosen at compile time inside `HttpClient`. There is no `ENGINE_WITH_*` option and no third-party library.

## API

`include/engine/net/http_client.h`, `http_call.h`, `http_request.h`.

| Type | Role |
| --- | --- |
| `HttpRequest` | `method` (`Get`, `Post`, `Put`, `Patch`, `Delete`, `Head`), `url`, `headers`, `body` (raw bytes), `timeout_seconds` (30 by default) |
| `HttpResponse` | `status`, `headers` (names as the server sent them), `body`. `header(name)` finds the first header ignoring ASCII case |
| `HttpError` | No response at all: `InvalidUrl`, `Unsupported`, `Network`, `Timeout`, `TooLarge` (`kHttpMaxResponseBytes`, 64 MiB) |
| `HttpResult` | `std::expected<HttpResponse, HttpError>` |
| `HttpCall` | One request in flight, owned by whoever sent it |
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

### Why a call and not an event

The client belongs to the process, not to an `ecs::World`. A result posted as an event would need an owner world, would be lost when that world goes away, and could reach a different world allocated at the same address. A call owned by the requester has none of that: it dies with its owner, and in the editor the game's calls die with the game module on Stop ([Principles](../architecture/Principles.md) #13). A game that wants an event sends one from its own system after `take()`.

## Backends

`src/net/http_client.cpp` picks one inside `Impl`:

| Build | Backend |
| --- | --- |
| `_WIN32` | WinHTTP (`winhttp_session.cpp`), blocking calls on `HttpWorkerPool` (4 threads). One session opened in `init` with the automatic proxy (falls back to the default proxy). gzip and deflate are decoded. A cancel closes the request handle, which makes the blocking call return |
| `__ANDROID__` | `java.net.HttpURLConnection` through JNI (`android_http.cpp`) on the same pool. `init` caches the framework classes on the main thread. Workers get their `JNIEnv` from `SDL_GetAndroidJNIEnv`, which attaches the thread and detaches it when the thread exits. A 4xx/5xx body is read from `getErrorStream`. A cancel calls `disconnect()` |
| `__EMSCRIPTEN__` | `emscripten_fetch` (`web_fetch.cpp`) with `EMSCRIPTEN_FETCH_LOAD_TO_MEMORY`, on the main thread. A cancel closes the fetch. Status 0 is `Network`, or `Timeout` once the timeout has elapsed |
| Otherwise | None. `init` succeeds, `is_supported()` is false, and every request answers `Unsupported` |

`init` returning false means the backend failed to start (no WinHTTP session, JNI classes missing). `EngineHost::open_primary` logs a warning and goes on; requests then answer `Unsupported`.

Platform requirements:

- **Android.** The engine manifest (`cmake/android/app/src/main/AndroidManifest.xml`) has `android.permission.INTERNET`. From API 28 cleartext `http://` is blocked; a game that needs it allows it in its manifest overlay ([Game Consumer](../build/Game%20Consumer.md)). `PATCH` relies on the platform's `HttpURLConnection` accepting it.
- **Web.** `engine_target_web_link_options` links `-sFETCH=1`. The browser applies CORS: a server on another origin must send `Access-Control-Allow-Origin`, and a custom request header needs a preflight answer. A response header is visible only if the server exposes it (`Access-Control-Expose-Headers`). A CORS refusal is `Network`.
- **Windows.** `engine` links `winhttp` PUBLIC ([CMake](../build/CMake.md#what-links)).

### Threads

`HttpCallState` (`src/net/http_call_state.h`) is shared by the call, the completion queue, and the backend. `result` is touched on the main thread only. A backend brackets its blocking work with `begin_transfer(abort)` and `end_transfer()`; `cancel` runs the registered abort under the same lock, so a handle is closed exactly once. `HttpCompletions` (`CallCompletions` from `src/core/call_completions.h`, shared with `FileDialogCall`) takes results from any thread and hands them over in `deliver`, dropping cancelled ones. `HttpWorkerPool` skips a call cancelled while queued.

## Not in scope yet

Streaming bodies, download to a file, progress, and backends for Linux and macOS. See [Scope](../architecture/Scope.md).

## Tests

`tests/http_test.cpp`: call ownership and cancel, delivery only on `deliver`, results from another thread, abort once, the worker pool (off the main thread, cancelled while queued, cancelled while running), URL and header parsing, `InvalidUrl` and `Unsupported` through `HttpClient`, and `init` per platform. No test opens a socket.

## See also

- [Core](Core.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
