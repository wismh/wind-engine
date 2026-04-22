#include <gtest/gtest.h>

#include "net/http_call_state.h"
#include "net/http_completions.h"
#include "net/http_parse.h"
#include "net/http_worker_pool.h"

#include <engine/net/http_call.h>
#include <engine/net/http_client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace {

engine::HttpResult ok(int status, std::string body = {}) {
    return engine::HttpResponse{.status = status, .headers = {}, .body = std::move(body)};
}

// Delivers until `state` has a result or two seconds pass.
bool deliver_until_done(engine::HttpCompletions& completions, const engine::HttpCallState& state) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        completions.deliver();
        if (state.result.has_value()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// A transfer that blocks until the test opens the gate or a cancel aborts it.
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = false;
    bool aborted = false;
    // Transfers that registered their abort.
    std::atomic<int> started{0};

    engine::HttpResult run(engine::HttpCallState& state) {
        if (!state.begin_transfer([this] {
                const std::scoped_lock lock(mutex);
                aborted = true;
                changed.notify_all();
            })) {
            return std::unexpected(engine::HttpError::Network);
        }
        ++started;
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [this] { return open || aborted; });
        }
        const bool was_aborted = state.end_transfer();
        return was_aborted ? engine::HttpResult{std::unexpected(engine::HttpError::Network)} : ok(200, "done");
    }

    void release() {
        const std::scoped_lock lock(mutex);
        open = true;
        changed.notify_all();
    }
};

}

TEST(HttpCall, EmptyCallIsNotPendingAndHasNoResult) {
    engine::HttpCall call;
    EXPECT_FALSE(call.pending());
    EXPECT_FALSE(call.take().has_value());
    call.cancel();
}

TEST(HttpCall, ResolvedCallHandsItsResultOnce) {
    engine::HttpCall call = engine::HttpCall::resolved(ok(204));
    EXPECT_FALSE(call.pending());
    const std::optional<engine::HttpResult> result = call.take();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ((*result)->status, 204);
    EXPECT_FALSE(call.take().has_value()) << "the call is empty after take";
}

TEST(HttpCall, ResultFromAnotherThreadAppearsOnlyOnDeliver) {
    engine::HttpCompletions completions;
    auto state = std::make_shared<engine::HttpCallState>();
    engine::HttpCall call{state};
    EXPECT_TRUE(call.pending());

    std::thread worker([&completions, state] { completions.push(state, ok(200, "hello")); });
    worker.join();
    EXPECT_TRUE(call.pending()) << "nothing is visible before deliver";
    EXPECT_FALSE(call.take().has_value());

    completions.deliver();
    EXPECT_FALSE(call.pending());
    const std::optional<engine::HttpResult> result = call.take();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ((*result)->body, "hello");
}

TEST(HttpCall, CancelledCallDropsALateResult) {
    engine::HttpCompletions completions;
    auto state = std::make_shared<engine::HttpCallState>();
    engine::HttpCall call{state};
    call.cancel();
    EXPECT_FALSE(call.pending());
    EXPECT_TRUE(state->cancelled());

    completions.push(state, ok(200));
    completions.deliver();
    EXPECT_FALSE(state->result.has_value());
    EXPECT_FALSE(call.take().has_value());
}

TEST(HttpCall, DestroyingAPendingCallCancelsIt) {
    auto state = std::make_shared<engine::HttpCallState>();
    {
        engine::HttpCall call{state};
    }
    EXPECT_TRUE(state->cancelled());
}

TEST(HttpCall, DestroyingAFinishedCallDoesNotCancel) {
    auto state = std::make_shared<engine::HttpCallState>();
    state->result = ok(200);
    {
        engine::HttpCall call{state};
    }
    EXPECT_FALSE(state->cancelled());
}

TEST(HttpCall, MoveAssigningOverAPendingCallCancelsTheOldOne) {
    auto first = std::make_shared<engine::HttpCallState>();
    auto second = std::make_shared<engine::HttpCallState>();
    engine::HttpCall call{first};
    call = engine::HttpCall{second};
    EXPECT_TRUE(first->cancelled());
    EXPECT_FALSE(second->cancelled());
    EXPECT_TRUE(call.pending());
}

TEST(HttpCallState, CancelRunsTheRegisteredAbortOnce) {
    engine::HttpCallState state;
    int aborts = 0;
    ASSERT_TRUE(state.begin_transfer([&aborts] { ++aborts; }));
    state.cancel();
    state.cancel();
    EXPECT_EQ(aborts, 1);
    EXPECT_TRUE(state.end_transfer()) << "the backend learns its handle was closed for it";
}

TEST(HttpCallState, FinishedTransferIsNotAborted) {
    engine::HttpCallState state;
    int aborts = 0;
    ASSERT_TRUE(state.begin_transfer([&aborts] { ++aborts; }));
    EXPECT_FALSE(state.end_transfer());
    state.cancel();
    EXPECT_EQ(aborts, 0);
}

TEST(HttpCallState, CancelledCallRefusesToStart) {
    engine::HttpCallState state;
    state.cancel();
    EXPECT_FALSE(state.begin_transfer([] {}));
}

TEST(HttpWorkerPool, RunsTransfersOffTheMainThread) {
    engine::HttpCompletions completions;
    const std::thread::id main_thread = std::this_thread::get_id();
    std::atomic<bool> off_main{false};
    engine::HttpWorkerPool pool(
            [&](engine::HttpCallState&, const engine::HttpRequest& request) {
                off_main = std::this_thread::get_id() != main_thread;
                return ok(200, request.url);
            },
            completions, 2);

    auto state = std::make_shared<engine::HttpCallState>();
    pool.submit(state, engine::HttpRequest{.url = "http://example.test/a"});
    ASSERT_TRUE(deliver_until_done(completions, *state));
    ASSERT_TRUE(state->result->has_value());
    EXPECT_EQ((*state->result)->body, "http://example.test/a");
    EXPECT_TRUE(off_main);
}

TEST(HttpWorkerPool, CallCancelledWhileQueuedNeverStarts) {
    engine::HttpCompletions completions;
    Gate gate;
    engine::HttpWorkerPool pool(
            [&gate](engine::HttpCallState& state, const engine::HttpRequest&) { return gate.run(state); },
            completions, 1);

    auto blocking = std::make_shared<engine::HttpCallState>();
    auto queued = std::make_shared<engine::HttpCallState>();
    pool.submit(blocking, {});
    pool.submit(queued, {});
    queued->cancel();
    gate.release();

    ASSERT_TRUE(deliver_until_done(completions, *blocking));
    pool.stop();
    completions.deliver();
    EXPECT_EQ(gate.started.load(), 1);
    EXPECT_FALSE(queued->result.has_value());
}

TEST(HttpWorkerPool, CancelAbortsARunningTransferAndDropsItsResult) {
    engine::HttpCompletions completions;
    Gate gate;
    engine::HttpWorkerPool pool(
            [&gate](engine::HttpCallState& state, const engine::HttpRequest&) { return gate.run(state); },
            completions, 1);

    auto state = std::make_shared<engine::HttpCallState>();
    pool.submit(state, {});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (gate.started.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(gate.started.load(), 1);

    state->cancel();
    pool.stop();
    completions.deliver();
    EXPECT_TRUE(gate.aborted);
    EXPECT_FALSE(state->result.has_value());
}

TEST(HttpParse, AcceptsHttpAndHttpsUrls) {
    const auto plain = engine::parse_http_url("http://example.com");
    ASSERT_TRUE(plain.has_value());
    EXPECT_FALSE(plain->secure);
    EXPECT_EQ(plain->host, "example.com");
    EXPECT_EQ(plain->port, 80);
    EXPECT_EQ(plain->target, "/");

    const auto secure = engine::parse_http_url("HTTPS://api.example.com:8443/v1/scores?top=10#frag");
    ASSERT_TRUE(secure.has_value());
    EXPECT_TRUE(secure->secure);
    EXPECT_EQ(secure->host, "api.example.com");
    EXPECT_EQ(secure->port, 8443);
    EXPECT_EQ(secure->target, "/v1/scores?top=10");
    EXPECT_EQ(secure->url, "HTTPS://api.example.com:8443/v1/scores?top=10");

    const auto query_only = engine::parse_http_url("https://example.com?x=1");
    ASSERT_TRUE(query_only.has_value());
    EXPECT_EQ(query_only->port, 443);
    EXPECT_EQ(query_only->target, "/?x=1");

    const auto ipv6 = engine::parse_http_url("http://[::1]:8080/ping");
    ASSERT_TRUE(ipv6.has_value());
    EXPECT_EQ(ipv6->host, "::1");
    EXPECT_EQ(ipv6->port, 8080);
}

TEST(HttpParse, RejectsWhatTheBackendsCannotSend) {
    for (const char* url : {"", "example.com", "ftp://example.com", "http://", "http:///path", "http://:80",
                 "http://example.com:", "http://example.com:0", "http://example.com:65536", "http://example.com:8o",
                 "http://user:pass@example.com", "http://exa mple.com", "http://[::1", "http://[::1]x",
                 "http://example.com/\n"}) {
        const auto parsed = engine::parse_http_url(url);
        ASSERT_FALSE(parsed.has_value()) << url;
        EXPECT_EQ(parsed.error(), engine::HttpError::InvalidUrl) << url;
    }
}

TEST(HttpParse, MethodNames) {
    EXPECT_EQ(engine::http_method_name(engine::HttpMethod::Get), "GET");
    EXPECT_EQ(engine::http_method_name(engine::HttpMethod::Post), "POST");
    EXPECT_EQ(engine::http_method_name(engine::HttpMethod::Put), "PUT");
    EXPECT_EQ(engine::http_method_name(engine::HttpMethod::Patch), "PATCH");
    EXPECT_EQ(engine::http_method_name(engine::HttpMethod::Delete), "DELETE");
    EXPECT_EQ(engine::http_method_name(engine::HttpMethod::Head), "HEAD");
}

TEST(HttpParse, RawHeadersSkipTheStatusLine) {
    const std::vector<engine::HttpHeader> headers = engine::parse_raw_headers(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Empty:\r\nbroken line\r\n"
            "Set-Cookie:  a=1 \r\n\r\n");
    ASSERT_EQ(headers.size(), 3u);
    EXPECT_EQ(headers[0].name, "Content-Type");
    EXPECT_EQ(headers[0].value, "application/json");
    EXPECT_EQ(headers[1].name, "X-Empty");
    EXPECT_EQ(headers[1].value, "");
    EXPECT_EQ(headers[2].value, "a=1");

    const std::vector<engine::HttpHeader> browser = engine::parse_raw_headers("content-length: 5\nlocation: /x\n");
    ASSERT_EQ(browser.size(), 2u);
    EXPECT_EQ(browser[1].value, "/x");
}

TEST(HttpResponse, HeaderLookupIgnoresCase) {
    const engine::HttpResponse response{
            .status = 200,
            .headers = {{"Content-Type", "text/plain"}, {"ETag", "\"v1\""}},
            .body = {},
    };
    EXPECT_EQ(response.header("content-type"), "text/plain");
    EXPECT_EQ(response.header("ETAG"), "\"v1\"");
    EXPECT_FALSE(response.header("Content").has_value());
}

TEST(HttpClient, InvalidUrlAnswersOnTheNextPoll) {
    engine::HttpClient client;
    engine::HttpCall call = client.send(engine::HttpRequest{.url = "ftp://example.com/file"});
    EXPECT_TRUE(call.pending());
    client.poll();
    const std::optional<engine::HttpResult> result = call.take();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->has_value());
    EXPECT_EQ(result->error(), engine::HttpError::InvalidUrl);
}

TEST(HttpClient, BeforeInitEveryRequestIsUnsupported) {
    engine::HttpClient client;
    EXPECT_FALSE(client.is_supported());
    engine::HttpCall call = client.send(engine::HttpRequest{.url = "https://example.com/"});
    client.poll();
    const std::optional<engine::HttpResult> result = call.take();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->has_value());
    EXPECT_EQ(result->error(), engine::HttpError::Unsupported);
}

TEST(HttpClient, InitStartsTheBackendWhereThereIsOne) {
    engine::HttpClient client;
    ASSERT_TRUE(client.init());
#if defined(_WIN32) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    EXPECT_TRUE(client.is_supported());
#else
    EXPECT_FALSE(client.is_supported());
#endif
    client.dispose();
    EXPECT_FALSE(client.is_supported());
}
