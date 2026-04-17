#include "net/android_http.h"

#if defined(__ANDROID__)

#include "net/http_call_state.h"
#include "net/http_parse.h"

#include <SDL3/SDL_system.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace engine {
namespace {

jstring new_string(JNIEnv* env, std::string_view text) {
    return env->NewStringUTF(std::string(text).c_str());
}

std::string to_string(JNIEnv* env, jstring text) {
    if (text == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(text, nullptr);
    std::string result = chars != nullptr ? std::string(chars) : std::string();
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(text, chars);
    }
    return result;
}

jint timeout_ms(float seconds) {
    if (seconds <= 0.f) {
        return 0;
    }
    const double ms = std::ceil(static_cast<double>(seconds) * 1000.0);
    return static_cast<jint>(std::clamp(ms, 1.0, static_cast<double>(INT_MAX)));
}

jclass global_class(JNIEnv* env, const char* name) {
    jclass local = env->FindClass(name);
    if (local == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }
    auto* global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
}

}

AndroidHttp::~AndroidHttp() {
    release();
}

bool AndroidHttp::resolve() {
    release();
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) {
        return false;
    }
    url_class_ = global_class(env, "java/net/URL");
    connection_class_ = global_class(env, "java/net/HttpURLConnection");
    input_class_ = global_class(env, "java/io/InputStream");
    output_class_ = global_class(env, "java/io/OutputStream");
    timeout_class_ = global_class(env, "java/net/SocketTimeoutException");
    malformed_class_ = global_class(env, "java/net/MalformedURLException");
    if (url_class_ == nullptr || connection_class_ == nullptr || input_class_ == nullptr || output_class_ == nullptr ||
            timeout_class_ == nullptr || malformed_class_ == nullptr) {
        release();
        return false;
    }

    url_init_ = env->GetMethodID(url_class_, "<init>", "(Ljava/lang/String;)V");
    open_connection_ = env->GetMethodID(url_class_, "openConnection", "()Ljava/net/URLConnection;");
    set_request_method_ = env->GetMethodID(connection_class_, "setRequestMethod", "(Ljava/lang/String;)V");
    set_connect_timeout_ = env->GetMethodID(connection_class_, "setConnectTimeout", "(I)V");
    set_read_timeout_ = env->GetMethodID(connection_class_, "setReadTimeout", "(I)V");
    set_request_property_ =
            env->GetMethodID(connection_class_, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
    set_do_output_ = env->GetMethodID(connection_class_, "setDoOutput", "(Z)V");
    get_output_stream_ = env->GetMethodID(connection_class_, "getOutputStream", "()Ljava/io/OutputStream;");
    get_response_code_ = env->GetMethodID(connection_class_, "getResponseCode", "()I");
    get_input_stream_ = env->GetMethodID(connection_class_, "getInputStream", "()Ljava/io/InputStream;");
    get_error_stream_ = env->GetMethodID(connection_class_, "getErrorStream", "()Ljava/io/InputStream;");
    get_header_field_key_ = env->GetMethodID(connection_class_, "getHeaderFieldKey", "(I)Ljava/lang/String;");
    get_header_field_ = env->GetMethodID(connection_class_, "getHeaderField", "(I)Ljava/lang/String;");
    disconnect_ = env->GetMethodID(connection_class_, "disconnect", "()V");
    input_read_ = env->GetMethodID(input_class_, "read", "([B)I");
    input_close_ = env->GetMethodID(input_class_, "close", "()V");
    output_write_ = env->GetMethodID(output_class_, "write", "([B)V");
    output_close_ = env->GetMethodID(output_class_, "close", "()V");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        release();
        return false;
    }
    return true;
}

void AndroidHttp::release() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    for (jclass* cls : {&url_class_, &connection_class_, &input_class_, &output_class_, &timeout_class_,
                 &malformed_class_}) {
        if (*cls != nullptr && env != nullptr) {
            env->DeleteGlobalRef(*cls);
        }
        *cls = nullptr;
    }
}

HttpError AndroidHttp::error_from_exception(JNIEnv* env) const {
    jthrowable thrown = env->ExceptionOccurred();
    env->ExceptionClear();
    HttpError error = HttpError::Network;
    if (thrown != nullptr) {
        if (env->IsInstanceOf(thrown, timeout_class_)) {
            error = HttpError::Timeout;
        } else if (env->IsInstanceOf(thrown, malformed_class_)) {
            error = HttpError::InvalidUrl;
        }
        env->DeleteLocalRef(thrown);
    }
    return error;
}

HttpResult AndroidHttp::transfer(HttpCallState& state, const HttpRequest& request) const {
    const auto url = parse_http_url(request.url);
    if (!url) {
        return std::unexpected(url.error());
    }
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr || env->PushLocalFrame(16) != 0) {
        return std::unexpected(HttpError::Network);
    }

    jobject java_url = env->NewObject(url_class_, url_init_, new_string(env, url->url));
    if (env->ExceptionCheck()) {
        const HttpError error = error_from_exception(env);
        env->PopLocalFrame(nullptr);
        return std::unexpected(error);
    }
    jobject local_connection = env->CallObjectMethod(java_url, open_connection_);
    if (env->ExceptionCheck() || local_connection == nullptr) {
        const HttpError error = env->ExceptionCheck() ? error_from_exception(env) : HttpError::Network;
        env->PopLocalFrame(nullptr);
        return std::unexpected(error);
    }
    jobject connection = env->NewGlobalRef(local_connection);

    // Runs on the main thread, with that thread's env.
    const jmethodID disconnect = disconnect_;
    HttpResult result = std::unexpected(HttpError::Network);
    if (state.begin_transfer([connection, disconnect] {
            JNIEnv* main_env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
            if (main_env != nullptr) {
                main_env->CallVoidMethod(connection, disconnect);
                if (main_env->ExceptionCheck()) {
                    main_env->ExceptionClear();
                }
            }
        })) {
        result = exchange(env, state, connection, request);
        (void)state.end_transfer();
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteGlobalRef(connection);
    env->PopLocalFrame(nullptr);
    return result;
}

HttpResult AndroidHttp::exchange(
        JNIEnv* env, HttpCallState& state, jobject connection, const HttpRequest& request) const {
    const auto fail = [&]() -> HttpResult { return std::unexpected(error_from_exception(env)); };

    jstring method = new_string(env, http_method_name(request.method));
    env->CallVoidMethod(connection, set_request_method_, method);
    env->DeleteLocalRef(method);
    if (env->ExceptionCheck()) {
        return fail();
    }
    const jint ms = timeout_ms(request.timeout_seconds);
    env->CallVoidMethod(connection, set_connect_timeout_, ms);
    env->CallVoidMethod(connection, set_read_timeout_, ms);
    for (const HttpHeader& header : request.headers) {
        jstring name = new_string(env, header.name);
        jstring value = new_string(env, header.value);
        env->CallVoidMethod(connection, set_request_property_, name, value);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(value);
        if (env->ExceptionCheck()) {
            return fail();
        }
    }

    // setDoOutput turns a GET into a POST, so only a request with a body asks for the output stream.
    if (!request.body.empty()) {
        env->CallVoidMethod(connection, set_do_output_, JNI_TRUE);
        jobject output = env->CallObjectMethod(connection, get_output_stream_);
        if (env->ExceptionCheck() || output == nullptr) {
            return env->ExceptionCheck() ? fail() : HttpResult{std::unexpected(HttpError::Network)};
        }
        const auto size = static_cast<jsize>(request.body.size());
        jbyteArray bytes = env->NewByteArray(size);
        if (bytes == nullptr) {
            env->ExceptionClear();
            env->DeleteLocalRef(output);
            return std::unexpected(HttpError::TooLarge);
        }
        env->SetByteArrayRegion(bytes, 0, size, reinterpret_cast<const jbyte*>(request.body.data()));
        env->CallVoidMethod(output, output_write_, bytes);
        if (!env->ExceptionCheck()) {
            env->CallVoidMethod(output, output_close_);
        }
        env->DeleteLocalRef(bytes);
        env->DeleteLocalRef(output);
        if (env->ExceptionCheck()) {
            return fail();
        }
    }

    HttpResponse response;
    response.status = env->CallIntMethod(connection, get_response_code_);
    if (env->ExceptionCheck()) {
        return fail();
    }
    // Index 0 is the status line: a null key with a value. The list ends at the first null value.
    for (jint i = 0;; ++i) {
        auto* key = static_cast<jstring>(env->CallObjectMethod(connection, get_header_field_key_, i));
        auto* value = static_cast<jstring>(env->CallObjectMethod(connection, get_header_field_, i));
        if (env->ExceptionCheck()) {
            return fail();
        }
        const bool end = value == nullptr;
        if (!end && key != nullptr) {
            response.headers.push_back(HttpHeader{.name = to_string(env, key), .value = to_string(env, value)});
        }
        env->DeleteLocalRef(key);
        env->DeleteLocalRef(value);
        if (end) {
            break;
        }
    }
    if (request.method == HttpMethod::Head) {
        return response;
    }

    // getInputStream throws for 4xx and 5xx; their body is on the error stream, which may be null.
    jobject input = env->CallObjectMethod(connection, response.status >= 400 ? get_error_stream_ : get_input_stream_);
    if (env->ExceptionCheck()) {
        return fail();
    }
    if (input == nullptr) {
        return response;
    }
    constexpr jsize kChunk = 64 * 1024;
    jbyteArray buffer = env->NewByteArray(kChunk);
    HttpResult result = std::move(response);
    for (;;) {
        if (state.cancelled()) {
            result = std::unexpected(HttpError::Network);
            break;
        }
        const jint read = env->CallIntMethod(input, input_read_, buffer);
        if (env->ExceptionCheck()) {
            result = fail();
            break;
        }
        if (read < 0) {
            break;
        }
        std::string& body = result->body;
        if (body.size() + static_cast<std::size_t>(read) > kHttpMaxResponseBytes) {
            result = std::unexpected(HttpError::TooLarge);
            break;
        }
        const std::size_t offset = body.size();
        body.resize(offset + static_cast<std::size_t>(read));
        env->GetByteArrayRegion(buffer, 0, read, reinterpret_cast<jbyte*>(body.data() + offset));
    }
    env->CallVoidMethod(input, input_close_);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(buffer);
    env->DeleteLocalRef(input);
    return result;
}

}

#endif
