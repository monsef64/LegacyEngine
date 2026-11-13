#pragma once
#include <SDL3/SDL.h>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace EngineLog {
inline std::mutex outputMutex;
inline const char* severity(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_TRACE:
        return "TRACE";
    case SDL_LOG_PRIORITY_VERBOSE:
        return "VERBOSE";
    case SDL_LOG_PRIORITY_DEBUG:
        return "DEBUG";
    case SDL_LOG_PRIORITY_INFO:
        return "INFO";
    case SDL_LOG_PRIORITY_WARN:
        return "WARNING";
    case SDL_LOG_PRIORITY_ERROR:
        return "ERROR";
    case SDL_LOG_PRIORITY_CRITICAL:
        return "FATAL";
    default:
        return "LOG";
    }
}
inline void SDLCALL terminal(void*, int category, SDL_LogPriority priority, const char* message) {
    std::lock_guard<std::mutex> lock(outputMutex);
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char stamp[32]{};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
    std::fprintf(stderr,
                 "[%s] [LegacyEngine] [%s] [category:%d] %s\n",
                 stamp,
                 severity(priority),
                 category,
                 message ? message : "");
    std::fflush(stderr);
}
// Crash handlers must not allocate, lock, invoke SDL or attempt engine cleanup.
// Keep native signal/exception termination behavior so debuggers still work.
inline void raw(const char* message, unsigned size) {
#ifdef _WIN32
    const auto handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    if (handle && handle != INVALID_HANDLE_VALUE) {
        WriteFile(handle, message, size, &written, nullptr);
    }
#else
    const auto ignored = ::write(STDERR_FILENO, message, size);
    (void)ignored;
#endif
}
template <std::size_t N> inline void raw(const char (&message)[N]) {
    raw(message, unsigned(N - 1));
}
inline void fatalSignal(int signal) {
    switch (signal) {
    case SIGABRT:
        raw("[LegacyEngine] [FATAL] Crash: SIGABRT (abort)\n");
        break;
    case SIGSEGV:
        raw("[LegacyEngine] [FATAL] Crash: SIGSEGV (invalid memory access)\n");
        break;
    case SIGILL:
        raw("[LegacyEngine] [FATAL] Crash: SIGILL (illegal instruction)\n");
        break;
    case SIGFPE:
        raw("[LegacyEngine] [FATAL] Crash: SIGFPE (arithmetic fault)\n");
        break;
#ifdef SIGBUS
    case SIGBUS:
        raw("[LegacyEngine] [FATAL] Crash: SIGBUS (bus error)\n");
        break;
#endif
    default:
        raw("[LegacyEngine] [FATAL] Fatal signal\n");
        break;
    }
#ifdef _WIN32
    std::_Exit(128 + signal);
#else
    // SA_RESETHAND has already restored the default handler. The signal is
    // pending until we return; native termination/core-dump policy is preserved.
    ::kill(::getpid(), signal);
#endif
}
#ifdef _WIN32
inline LONG WINAPI fatalException(EXCEPTION_POINTERS* info) {
    raw("[LegacyEngine] [FATAL] Unhandled Windows exception: ");
    unsigned long code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    char value[] = "0x00000000\n";
    const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; ++i) {
        value[9 - i] = hex[(code >> (i * 4)) & 15];
    }
    raw(value);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
inline void initialize() {
    SDL_SetLogOutputFunction(terminal, nullptr);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    std::set_terminate([]() {
        try {
            if (auto error = std::current_exception()) {
                std::rethrow_exception(error);
            }
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                            "std::terminate called without an active exception");
        } catch (const std::exception& error) {
            SDL_LogCritical(
                SDL_LOG_CATEGORY_APPLICATION, "Unhandled C++ exception: %s", error.what());
        } catch (...) {
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Unhandled non-standard C++ exception");
        }
        std::abort();
    });
#ifdef _WIN32
    SetUnhandledExceptionFilter(fatalException);
    std::signal(SIGABRT, fatalSignal);
#else
    struct sigaction action{};
    action.sa_handler = fatalSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    for (int signal : {SIGABRT,
                       SIGSEGV,
                       SIGILL,
                       SIGFPE
#ifdef SIGBUS
                       ,
                       SIGBUS
#endif
         })
        sigaction(signal, &action, nullptr);
#endif
}
// Main-thread editor error state: print only changes, not one line per frame.
class Errors {
    std::map<std::string, std::string> last_;

  public:
    void report(const std::string& source, const std::string& message) {
        auto& previous = last_[source];
        if (previous == message) {
            return;
        }
        previous = message;
        if (!message.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", source.c_str(), message.c_str());
        }
    }
};
} // namespace EngineLog
