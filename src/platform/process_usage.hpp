#pragma once
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

// Process metrics, sampled at most twice a second, only while the profiler is open.
// CPU is normalized to all logical cores (100% means the entire machine).
struct ProcessUsage {
    double cpuPercent = 0;
    std::uint64_t residentBytes = 0;
    bool memoryAvailable = false, cpuAvailable = false;
    void sample() {
        const auto now = std::chrono::steady_clock::now();
        if (last_.time_since_epoch().count() && now - last_ < std::chrono::milliseconds(500)) {
            return;
        }
        double cpu = -1;
#ifdef _WIN32
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            auto seconds = [](FILETIME t) {
                return double((std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 1e7;
            };
            cpu = seconds(kernel) + seconds(user);
        }
        using ReadMemory = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
        static auto readMemory = reinterpret_cast<ReadMemory>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        memoryAvailable =
            readMemory && readMemory(GetCurrentProcess(), &counters, sizeof(counters));
        if (memoryAvailable) {
            residentBytes = counters.WorkingSetSize;
        }
#else
        const auto ticks = std::clock();
        if (ticks != std::clock_t(-1)) {
            cpu = double(ticks) / CLOCKS_PER_SEC;
        }
        std::ifstream stat("/proc/self/statm");
        std::uint64_t virtualPages = 0, residentPages = 0;
        const long pageSize = sysconf(_SC_PAGESIZE);
        memoryAvailable = bool(stat >> virtualPages >> residentPages) && pageSize > 0;
        if (memoryAvailable) {
            residentBytes = residentPages * std::uint64_t(pageSize);
        }
#endif
        cpuAvailable = cpu >= 0 && previousCpu_ >= 0 && last_.time_since_epoch().count();
        if (cpuAvailable) {
            const double elapsed = std::chrono::duration<double>(now - last_).count();
            const int cores = SDL_GetNumLogicalCPUCores();
            cpuPercent = 100 * (cpu - previousCpu_) / elapsed / (cores > 0 ? cores : 1);
            if (cpuPercent < 0) {
                cpuPercent = 0;
            }
            if (cpuPercent > 100) {
                cpuPercent = 100;
            }
        }
        previousCpu_ = cpu;
        last_ = now;
    }

  private:
    std::chrono::steady_clock::time_point last_{};
    double previousCpu_ = -1;
};
