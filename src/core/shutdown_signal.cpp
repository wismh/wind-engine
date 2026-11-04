#include "core/shutdown_signal.h"

#include <atomic>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#endif

namespace engine {

namespace {

std::atomic<bool> s_requested{false};
static_assert(std::atomic<bool>::is_always_lock_free);

#if defined(_WIN32)

// Set once the loop has finished, so a closing console waits for on_quit instead of ending the process mid-tick.
HANDLE s_finished = nullptr;

BOOL WINAPI console_handler(DWORD event) {
    if (s_requested.exchange(true)) {
        return FALSE;  // the second one: the default handler ends the process
    }
    switch (event) {
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            // Windows ends the process when this returns, and after about 5 s in any case.
            WaitForSingleObject(s_finished, 4500);
            return TRUE;
        default:
            return TRUE;
    }
}

#else

constexpr int kStopSignals[] = {SIGINT, SIGTERM, SIGHUP};
struct sigaction s_previous[3];
struct sigaction s_previous_pipe;

void signal_handler(int signal_number) {
    if (s_requested.exchange(true)) {
        // The second stop signal, of any kind: its default effect, which ends the process. Both calls are
        // async-signal-safe, and the raised signal is delivered when this handler returns.
        std::signal(signal_number, SIG_DFL);
        std::raise(signal_number);
    }
}

#endif

}

ShutdownSignals::ShutdownSignals() {
    s_requested.store(false);
#if defined(_WIN32)
    if (s_finished == nullptr) {
        s_finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    ResetEvent(s_finished);
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    struct sigaction action {};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < 3; ++i) {
        sigaction(kStopSignals[i], &action, &s_previous[i]);
    }
    // A log reader that went away (`server | tee`) must not end the server.
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGPIPE, &ignore, &s_previous_pipe);
#endif
}

ShutdownSignals::~ShutdownSignals() {
#if defined(_WIN32)
    SetConsoleCtrlHandler(console_handler, FALSE);
    // The handle stays open: a handler may still be waiting on it, and the next instance reuses it.
    SetEvent(s_finished);
#else
    for (int i = 0; i < 3; ++i) {
        sigaction(kStopSignals[i], &s_previous[i], nullptr);
    }
    sigaction(SIGPIPE, &s_previous_pipe, nullptr);
#endif
}

bool shutdown_requested() noexcept {
    return s_requested.load();
}

void raise_shutdown_signal_for_test() {
#if defined(_WIN32)
    console_handler(CTRL_C_EVENT);
#else
    std::raise(SIGTERM);
#endif
}

}
