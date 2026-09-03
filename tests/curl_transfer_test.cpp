#include <gtest/gtest.h>

#include "net/curl_api.h"
#include "net/curl_transfer.h"
#include "net/http_call_state.h"

#include <engine/net/http_request.h>

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

using WriteFunction = std::size_t (*)(char*, std::size_t, std::size_t, void*);
using HeaderFunction = WriteFunction;
using ProgressFunction = int (*)(void*, std::int64_t, std::int64_t, std::int64_t, std::int64_t);

// A libcurl that records the options it is given and, on perform, plays a scripted response through the
// callbacks the way libcurl does.
struct FakeCurl {
    struct Step {
        enum class Kind { Header, Body, Progress, Sleep } kind = Kind::Body;
        std::string text;
        std::int64_t downloaded = 0;
    };

    std::map<int, long> longs;
    std::map<int, std::string> texts;
    std::map<int, std::int64_t> offsets;
    std::vector<std::string> header_lines;
    WriteFunction write = nullptr;
    HeaderFunction header = nullptr;
    ProgressFunction progress = nullptr;
    void* write_data = nullptr;
    void* header_data = nullptr;
    void* progress_data = nullptr;

    std::vector<Step> script;
    int result = 0;
    // Cancels this call just before the step with that index.
    engine::HttpCallState* cancel_state = nullptr;
    int cancel_before_step = -1;
    // Set by the callbacks when they stop the transfer.
    bool write_refused = false;
    bool progress_refused = false;
    int cleanups = 0;
    int slist_frees = 0;
};

struct FakeList {
    std::vector<std::string> items;
};

FakeCurl* g_fake = nullptr;

void* fake_init() {
    return g_fake;
}

void fake_cleanup(void*) {
    ++g_fake->cleanups;
}

int fake_setopt(void*, int option, ...) {
    va_list args;
    va_start(args, option);
    if (option >= 30000) {
        g_fake->offsets[option] = va_arg(args, std::int64_t);
    } else if (option >= 20000) {
        void* function = va_arg(args, void*);
        if (option == engine::curl::kWriteFunction) {
            g_fake->write = reinterpret_cast<WriteFunction>(function);
        } else if (option == engine::curl::kHeaderFunction) {
            g_fake->header = reinterpret_cast<HeaderFunction>(function);
        } else if (option == engine::curl::kXferInfoFunction) {
            g_fake->progress = reinterpret_cast<ProgressFunction>(function);
        }
    } else if (option >= 10000) {
        void* pointer = va_arg(args, void*);
        if (option == engine::curl::kWriteData) {
            g_fake->write_data = pointer;
        } else if (option == engine::curl::kHeaderData) {
            g_fake->header_data = pointer;
        } else if (option == engine::curl::kXferInfoData) {
            g_fake->progress_data = pointer;
        } else if (option == engine::curl::kHttpHeader) {
            g_fake->header_lines = static_cast<FakeList*>(pointer)->items;
        } else if (pointer != nullptr && option != engine::curl::kPostFields) {
            g_fake->texts[option] = static_cast<const char*>(pointer);
        } else if (option == engine::curl::kPostFields) {
            g_fake->texts[option] = "<body>";
        }
    } else {
        g_fake->longs[option] = va_arg(args, long);
    }
    va_end(args);
    return 0;
}

int fake_perform(void*) {
    int index = 0;
    for (const FakeCurl::Step& step : g_fake->script) {
        if (index++ == g_fake->cancel_before_step && g_fake->cancel_state != nullptr) {
            g_fake->cancel_state->cancel();
        }
        switch (step.kind) {
        case FakeCurl::Step::Kind::Sleep:
            std::this_thread::sleep_for(std::chrono::milliseconds(step.downloaded));
            break;
        case FakeCurl::Step::Kind::Header:
            g_fake->header(const_cast<char*>(step.text.data()), 1, step.text.size(), g_fake->header_data);
            break;
        case FakeCurl::Step::Kind::Body:
            if (g_fake->write(const_cast<char*>(step.text.data()), 1, step.text.size(), g_fake->write_data) !=
                    step.text.size()) {
                g_fake->write_refused = true;
                return 23;
            }
            break;
        case FakeCurl::Step::Kind::Progress:
            if (g_fake->progress(g_fake->progress_data, 0, step.downloaded, 0, 0) != 0) {
                g_fake->progress_refused = true;
                return 42;
            }
            break;
        }
    }
    return g_fake->result;
}

void* fake_slist_append(void* list, const char* text) {
    auto* fake_list = list != nullptr ? static_cast<FakeList*>(list) : new FakeList;
    fake_list->items.emplace_back(text);
    return fake_list;
}

void fake_slist_free(void* list) {
    ++g_fake->slist_frees;
    delete static_cast<FakeList*>(list);
}

engine::CurlApi fake_api() {
    return engine::CurlApi{
            .easy_init = &fake_init,
            .easy_cleanup = &fake_cleanup,
            .easy_setopt = &fake_setopt,
            .easy_perform = &fake_perform,
            .slist_append = &fake_slist_append,
            .slist_free_all = &fake_slist_free,
    };
}

FakeCurl::Step header(std::string text) {
    return {FakeCurl::Step::Kind::Header, std::move(text), 0};
}
FakeCurl::Step body(std::string text) {
    return {FakeCurl::Step::Kind::Body, std::move(text), 0};
}
FakeCurl::Step pause_for(std::int64_t milliseconds) {
    return {FakeCurl::Step::Kind::Sleep, {}, milliseconds};
}
FakeCurl::Step progress(std::int64_t downloaded) {
    return {FakeCurl::Step::Kind::Progress, {}, downloaded};
}

class CurlTransfer : public ::testing::Test {
protected:
    void SetUp() override {
        g_fake = &fake;
    }
    void TearDown() override {
        g_fake = nullptr;
    }

    engine::HttpResult run(engine::HttpRequest request) {
        return engine::curl_transfer(fake_api(), state, request);
    }

    FakeCurl fake;
    engine::HttpCallState state;
};

}

TEST_F(CurlTransfer, ReadsStatusHeadersAndBody) {
    fake.script = {header("HTTP/1.1 200 OK\r\n"), header("Content-Type: text/plain\r\n"),
            header("Content-Length: 11\r\n"), header("\r\n"), body("hello "), body("world")};
    const engine::HttpResult result = run({.url = "https://example.com/a"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 200);
    EXPECT_EQ(result->body, "hello world");
    EXPECT_EQ(result->header("content-type"), "text/plain");
    ASSERT_TRUE(state.progress().total.has_value());
    EXPECT_EQ(*state.progress().total, 11u);
    EXPECT_EQ(state.progress().received, 11u);
    EXPECT_EQ(fake.cleanups, 1);
}

TEST_F(CurlTransfer, RedirectBlocksReplaceEachOther) {
    fake.script = {header("HTTP/1.1 302 Found\r\n"), header("Location: /b\r\n"), header("\r\n"),
            header("HTTP/2 200 \r\n"), header("Content-Length: 2\r\n"), header("\r\n"), body("ok")};
    const engine::HttpResult result = run({.url = "http://example.com/a"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 200);
    EXPECT_FALSE(result->header("Location").has_value()) << "the headers are the last response's";
    EXPECT_EQ(result->body, "ok");
}

TEST_F(CurlTransfer, ResponseWithoutBodyStillHasItsHeaders) {
    fake.script = {header("HTTP/1.1 204 No Content\r\n"), header("ETag: \"v2\"\r\n"), header("\r\n")};
    const engine::HttpResult result = run({.method = engine::HttpMethod::Head, .url = "http://example.com/"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 204);
    EXPECT_EQ(result->header("etag"), "\"v2\"");
    EXPECT_EQ(fake.longs[engine::curl::kNoBody], 1);
}

TEST_F(CurlTransfer, CompressedBodyHasNoTotal) {
    fake.script = {header("HTTP/1.1 200 OK\r\n"), header("Content-Encoding: gzip\r\n"),
            header("Content-Length: 5\r\n"), header("\r\n"), body("decoded text")};
    const engine::HttpResult result = run({.url = "http://example.com/"});
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(state.progress().total.has_value());
}

TEST_F(CurlTransfer, SinkTakesOnlyASuccessBody) {
    std::string sunk;
    state.set_sink(
            [&sunk](std::span<const std::byte> chunk) {
                sunk.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
                return true;
            },
            engine::SinkRoute::Worker);
    fake.script = {header("HTTP/1.1 206 Partial Content\r\n"), header("\r\n"), body("ab"), body("cd")};
    const engine::HttpResult ok = run({.url = "http://example.com/"});
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(sunk, "abcd");
    EXPECT_TRUE(ok->body.empty());

    sunk.clear();
    fake.script = {header("HTTP/1.1 404 Not Found\r\n"), header("\r\n"), body("missing")};
    const engine::HttpResult missing = run({.url = "http://example.com/"});
    ASSERT_TRUE(missing.has_value());
    EXPECT_EQ(missing->status, 404);
    EXPECT_EQ(missing->body, "missing");
    EXPECT_TRUE(sunk.empty());
}

TEST_F(CurlTransfer, SinkRefusalEndsWithSinkAborted) {
    state.set_sink([](std::span<const std::byte>) { return false; }, engine::SinkRoute::Worker);
    fake.script = {header("HTTP/1.1 200 OK\r\n"), header("\r\n"), body("x")};
    const engine::HttpResult result = run({.url = "http://example.com/"});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::HttpError::SinkAborted);
    EXPECT_TRUE(fake.write_refused);
}

TEST_F(CurlTransfer, CancelledCallNeverStarts) {
    state.cancel();
    fake.script = {header("HTTP/1.1 200 OK\r\n"), header("\r\n"), body("a")};
    const engine::HttpResult result = run({.url = "http://example.com/"});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(fake.cleanups, 1);
    EXPECT_FALSE(fake.write_refused);
}

TEST_F(CurlTransfer, CancelStopsTheTransferAtTheNextProgressCall) {
    fake.script = {header("HTTP/1.1 200 OK\r\n"), header("\r\n"), body("a"), progress(1), body("never")};
    fake.cancel_state = &state;
    fake.cancel_before_step = 3;
    const engine::HttpResult result = run({.url = "http://example.com/"});
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(fake.progress_refused);
    EXPECT_EQ(state.received(), 1u) << "the body after the cancel never arrives";
}

TEST_F(CurlTransfer, QuietConnectionTimesOutThroughTheProgressCallback) {
    fake.script = {progress(0), pause_for(80), progress(0)};
    const engine::HttpResult result = run({.url = "http://example.com/", .timeout_seconds = 0.05f});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::HttpError::Timeout);
}

TEST_F(CurlTransfer, ProgressKeepsTheTimeoutAway) {
    fake.script = {progress(10), pause_for(80), progress(20), pause_for(80), progress(30)};
    EXPECT_TRUE(run({.url = "http://example.com/", .timeout_seconds = 0.05f}).has_value());

    fake.script = {progress(0), pause_for(80), progress(0)};
    EXPECT_TRUE(run({.url = "http://example.com/", .timeout_seconds = 0.f}).has_value()) << "0 waits forever";
}

TEST_F(CurlTransfer, OptionsFollowTheRequest) {
    engine::HttpRequest request{.method = engine::HttpMethod::Post,
            .url = "https://example.com/upload#frag",
            .headers = {{"Range", "bytes=10-"}, {"X-Token", "t"}},
            .body = "payload",
            .timeout_seconds = 7.f};
    request.max_redirects = 3;
    request.min_bytes_per_second = 1000;
    request.stall_seconds = 2.5f;
    fake.script = {header("HTTP/1.1 200 OK\r\n"), header("\r\n")};
    ASSERT_TRUE(run(request).has_value());

    EXPECT_EQ(fake.texts[engine::curl::kUrl], "https://example.com/upload");
    EXPECT_EQ(fake.texts[engine::curl::kCustomRequest], "POST");
    EXPECT_EQ(fake.offsets[engine::curl::kPostFieldSizeLarge], 7);
    EXPECT_EQ(fake.longs[engine::curl::kFollowLocation], 1);
    EXPECT_EQ(fake.longs[engine::curl::kMaxRedirs], 3);
    EXPECT_EQ(fake.longs[engine::curl::kRedirProtocols], engine::curl::kProtoHttps) << "no https to http";
    EXPECT_EQ(fake.longs[engine::curl::kConnectTimeoutMs], 7000);
    EXPECT_EQ(fake.longs[engine::curl::kLowSpeedLimit], 1000);
    EXPECT_EQ(fake.longs[engine::curl::kLowSpeedTime], 3);
    EXPECT_EQ(fake.longs[engine::curl::kNoSignal], 1);
    ASSERT_EQ(fake.header_lines.size(), 2u);
    EXPECT_EQ(fake.header_lines[0], "Range: bytes=10-");
    EXPECT_EQ(fake.header_lines[1], "X-Token: t");
    EXPECT_EQ(fake.slist_frees, 1);
}

TEST_F(CurlTransfer, RedirectPolicy) {
    fake.script = {header("HTTP/1.1 200 OK\r\n"), header("\r\n")};

    engine::HttpRequest insecure{.url = "https://example.com/"};
    insecure.allow_insecure_redirect = true;
    ASSERT_TRUE(run(insecure).has_value());
    EXPECT_EQ(fake.longs[engine::curl::kRedirProtocols], engine::curl::kProtoHttp | engine::curl::kProtoHttps);

    engine::HttpRequest none{.url = "https://example.com/"};
    none.max_redirects = 0;
    ASSERT_TRUE(run(none).has_value());
    EXPECT_EQ(fake.longs[engine::curl::kFollowLocation], 0);

    ASSERT_TRUE(run({.url = "http://example.com/"}).has_value());
    EXPECT_EQ(fake.longs[engine::curl::kRedirProtocols], engine::curl::kProtoHttp | engine::curl::kProtoHttps);
    EXPECT_EQ(fake.texts.count(engine::curl::kCustomRequest), 0u) << "a plain GET names no method";
}

TEST_F(CurlTransfer, ErrorCodesMapToEngineErrors) {
    EXPECT_EQ(engine::curl_error(6), engine::HttpError::Dns);
    EXPECT_EQ(engine::curl_error(28), engine::HttpError::Timeout);
    EXPECT_EQ(engine::curl_error(47), engine::HttpError::RedirectRefused);
    EXPECT_EQ(engine::curl_error(60), engine::HttpError::Tls);
    EXPECT_EQ(engine::curl_error(35), engine::HttpError::Tls);
    EXPECT_EQ(engine::curl_error(56), engine::HttpError::ConnectionReset);
    EXPECT_EQ(engine::curl_error(52), engine::HttpError::ConnectionReset);
    EXPECT_EQ(engine::curl_error(7), engine::HttpError::Network);
    EXPECT_EQ(engine::curl_error(999), engine::HttpError::Network);

    fake.result = 6;
    const engine::HttpResult result = run({.url = "http://nope.invalid/"});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::HttpError::Dns);
}

TEST_F(CurlTransfer, InvalidUrlNeverReachesLibcurl) {
    const engine::HttpResult result = run({.url = "ftp://example.com/"});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::HttpError::InvalidUrl);
    EXPECT_EQ(fake.cleanups, 0);
}
