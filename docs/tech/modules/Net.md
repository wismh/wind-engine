# Net

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

The client belongs to the process, not to an `ecs::World`. A result posted as an event would need an owner world, would be lost when that world goes away, and could reach a different world allocated at the same address. A call owned by the requester has none of that: it dies with its owner, and in the editor the game's calls die with the game module on Stop ([Principles](../architecture/Principles.md) #13). A game that wants an event sends one from its own system after `take()`.

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

## Not in scope yet

Retries, resuming, and writing files are the game's: the engine gives it the sink, `Range`, and the error codes to do them. Connections are not reused between requests on any backend yet.

## Tests

`tests/http_test.cpp`: call ownership and cancel, delivery only on `deliver`, results from another thread, abort once, the worker pool (off the main thread, cancelled while queued, cancelled while running), the body path (sink only for 2xx, abort, progress, queue order and backpressure, cancel waking a paused transfer, cancel waiting for a running sink, a `Main` sink seeing the body before the result), `StallWatch`, Content-Length parsing, `curl_transfer` against a fake libcurl (headers and redirect blocks, sink and 404, option mapping, redirect policy, cancel, idle timeout, error codes), URL and header parsing, `InvalidUrl` and `Unsupported` through `HttpClient`, and `init` per platform. No test opens a socket. The Windows backend was also run by hand against a local server (a 20 MiB download on both sink threads, redirect on and off, 404, stall, DNS and TLS errors), and `curl_transfer` was run the same way through Git for Windows' real libcurl 8.18 (the option numbers and callbacks are the same on every platform). The `dlopen` loader and the Linux and macOS client branch were only syntax-checked, with clang, not run.

## See also

- [Core](Core.md)
- [Runtime Loop](../architecture/Runtime%20Loop.md)
