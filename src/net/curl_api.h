#pragma once

// libcurl is loaded at run time (curl_library.h), so the engine includes no curl header: the functions it
// calls and the option numbers it passes are declared here. The numbers are libcurl's stable ABI.

#include <cstdint>

// Linux and macOS use libcurl. Every other target has its own backend or none.
#if (defined(__linux__) || defined(__APPLE__)) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#define ENGINE_NET_CURL 1
#endif

namespace engine {

// The libcurl functions the backend calls. `CURL*` and `curl_slist*` are opaque here. The `...` of setopt
// takes a `long`, a pointer, a function pointer, or a 64-bit offset, depending on the option.
struct CurlApi {
    using EasyInit = void* (*)();
    using EasyCleanup = void (*)(void* easy);
    using EasySetopt = int (*)(void* easy, int option, ...);
    using EasyPerform = int (*)(void* easy);
    using SlistAppend = void* (*)(void* list, const char* text);
    using SlistFreeAll = void (*)(void* list);

    EasyInit easy_init = nullptr;
    EasyCleanup easy_cleanup = nullptr;
    EasySetopt easy_setopt = nullptr;
    EasyPerform easy_perform = nullptr;
    SlistAppend slist_append = nullptr;
    SlistFreeAll slist_free_all = nullptr;
};

namespace curl {

// CURLoption
inline constexpr int kWriteData = 10001;
inline constexpr int kUrl = 10002;
inline constexpr int kPostFields = 10015;
inline constexpr int kUserAgent = 10018;
inline constexpr int kHttpHeader = 10023;
inline constexpr int kHeaderData = 10029;
inline constexpr int kCustomRequest = 10036;
inline constexpr int kXferInfoData = 10057;
inline constexpr int kAcceptEncoding = 10102;
inline constexpr int kLowSpeedLimit = 19;
inline constexpr int kLowSpeedTime = 20;
inline constexpr int kNoProgress = 43;
inline constexpr int kNoBody = 44;
inline constexpr int kFollowLocation = 52;
inline constexpr int kMaxRedirs = 68;
inline constexpr int kNoSignal = 99;
inline constexpr int kBufferSize = 98;
inline constexpr int kConnectTimeoutMs = 156;
inline constexpr int kProtocols = 181;
inline constexpr int kRedirProtocols = 182;
inline constexpr int kWriteFunction = 20011;
inline constexpr int kHeaderFunction = 20079;
inline constexpr int kXferInfoFunction = 20219;
inline constexpr int kPostFieldSizeLarge = 30120;

// CURLPROTO_*
inline constexpr long kProtoHttp = 1;
inline constexpr long kProtoHttps = 2;

// CURLcode
inline constexpr int kOk = 0;
inline constexpr int kUnsupportedProtocol = 1;
inline constexpr int kCouldntResolveProxy = 5;
inline constexpr int kCouldntResolveHost = 6;
inline constexpr int kPartialFile = 18;
inline constexpr int kOperationTimedOut = 28;
inline constexpr int kSslConnectError = 35;
inline constexpr int kTooManyRedirects = 47;
inline constexpr int kGotNothing = 52;
inline constexpr int kSendError = 55;
inline constexpr int kRecvError = 56;
inline constexpr int kPeerFailedVerification = 60;
inline constexpr int kSslCertProblem = 58;
inline constexpr int kSslCipher = 59;
inline constexpr int kUseSslFailed = 64;
inline constexpr int kSslCaCertBadFile = 77;
inline constexpr int kSslCrlBadFile = 82;
inline constexpr int kSslIssuerError = 83;
inline constexpr int kSslEngineInitFailed = 66;
inline constexpr int kSslClientCert = 98;

// curl_global_init flag CURL_GLOBAL_DEFAULT (SSL | WIN32)
inline constexpr long kGlobalDefault = 3;

}

}
