// Copyright (c) 2026 The Sugarchain Komorebi developers
// Distributed under the MIT software license, see the accompanying file COPYING.

// Link into an isolated diagnostic executable with -Wl,--wrap=yespower.
// Never substitutes a result or changes the production verification path.
#include <crypto/yespower-1.0.1/yespower.h>
#include <util/string.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <ctime>

namespace {
std::atomic<unsigned long long> calls{0}, cpu_ns{0}, failed{0};
unsigned long long ThreadTime()
{
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t)) return 0;
    return static_cast<unsigned long long>(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}
struct Report {
    ~Report()
    {
        const std::string message{"YESPOWER_PROFILE {\"calls\":" + util::ToString(calls.load()) +
                                  ",\"cpu_ns\":" + util::ToString(cpu_ns.load()) +
                                  ",\"failed\":" + util::ToString(failed.load()) + "}\n"};
        std::fputs(message.c_str(), stderr);
    }
} report;
}

unsigned long long YespowerProfileCalls() { return calls.load(); }
unsigned long long YespowerProfileCPU() { return cpu_ns.load(); }

extern "C" int __real_yespower(yespower_local_t*, const uint8_t*, size_t, const yespower_params_t*, yespower_binary_t*);
extern "C" int __wrap_yespower(yespower_local_t* local, const uint8_t* input, size_t size,
                              const yespower_params_t* params, yespower_binary_t* output)
{
    const auto begin{ThreadTime()};
    const int result{__real_yespower(local, input, size, params, output)};
    cpu_ns += ThreadTime() - begin;
    ++calls;
    if (result) ++failed;
    return result;
}
