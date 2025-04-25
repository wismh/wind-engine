#pragma once

#if defined(__ANDROID__)

#include <engine/net/http_request.h>

#include <jni.h>

namespace engine {

struct HttpCallState;

// java.net.HttpURLConnection through JNI. `resolve` caches the framework classes on the main thread;
// `transfer` is blocking and runs on a worker (SDL attaches that thread to the JVM and detaches it when the
// thread exits). A cancel calls `disconnect()`, which makes the blocking read throw.
class AndroidHttp {
public:
    AndroidHttp() = default;
    AndroidHttp(const AndroidHttp&) = delete;
    AndroidHttp& operator=(const AndroidHttp&) = delete;
    ~AndroidHttp();

    [[nodiscard]] bool resolve();
    void release();

    [[nodiscard]] HttpResult transfer(HttpCallState& state, const HttpRequest& request) const;

private:
    [[nodiscard]] HttpResult exchange(JNIEnv* env, HttpCallState& state, jobject connection,
            const HttpRequest& request) const;
    [[nodiscard]] HttpError error_from_exception(JNIEnv* env) const;

    jclass url_class_ = nullptr;
    jclass connection_class_ = nullptr;
    jclass input_class_ = nullptr;
    jclass output_class_ = nullptr;
    jclass timeout_class_ = nullptr;
    jclass malformed_class_ = nullptr;
    jmethodID url_init_ = nullptr;
    jmethodID open_connection_ = nullptr;
    jmethodID set_request_method_ = nullptr;
    jmethodID set_connect_timeout_ = nullptr;
    jmethodID set_read_timeout_ = nullptr;
    jmethodID set_request_property_ = nullptr;
    jmethodID set_do_output_ = nullptr;
    jmethodID get_output_stream_ = nullptr;
    jmethodID get_response_code_ = nullptr;
    jmethodID get_input_stream_ = nullptr;
    jmethodID get_error_stream_ = nullptr;
    jmethodID get_header_field_key_ = nullptr;
    jmethodID get_header_field_ = nullptr;
    jmethodID disconnect_ = nullptr;
    jmethodID input_read_ = nullptr;
    jmethodID input_close_ = nullptr;
    jmethodID output_write_ = nullptr;
    jmethodID output_close_ = nullptr;
};

}

#endif
