// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <crypto/yespower-1.0.1/yespower.h>
#include <streams.h>
#include <stdexcept>
#include <tinyformat.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <span>
#include <sstream>

#include <atomic>
#include <cstdlib>
#ifdef BITCOINKERNEL_YESPOWER_POOL
#include <array>
#include <thread>
#elif defined(WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace {
// Own the native scratch allocation, including all failure paths.
struct YespowerLocal {
    yespower_local_t memory;
    YespowerLocal()
    {
        if (yespower_init_local(&memory)) throw std::runtime_error("YespowerSugar: failed to initialize scratch memory");
    }
    ~YespowerLocal() { yespower_free_local(&memory); }
    YespowerLocal(const YespowerLocal&) = delete;
    YespowerLocal& operator=(const YespowerLocal&) = delete;
};

#ifndef BITCOINKERNEL_YESPOWER_POOL
// Native destructor keys avoid C++ thread_local destructors. The published key
// has process lifetime so worker cleanup remains valid during static teardown.
#ifdef WIN32
using YespowerKey = DWORD;
void NTAPI DeleteYespowerLocal(void* value) noexcept
#else
using YespowerKey = pthread_key_t;
void DeleteYespowerLocal(void* value) noexcept
#endif
{
    delete static_cast<YespowerLocal*>(value);
}

std::atomic<bool> g_yespower_cleanup_has_run{false};
YespowerKey GetYespowerKey();

YespowerLocal* ReadYespowerLocal(YespowerKey key)
{
#ifdef WIN32
    return static_cast<YespowerLocal*>(FlsGetValue(key));
#else
    return static_cast<YespowerLocal*>(pthread_getspecific(key));
#endif
}

bool StoreYespowerLocal(YespowerKey key, YespowerLocal* value)
{
#ifdef WIN32
    return FlsSetValue(key, value) != 0;
#else
    return pthread_setspecific(key, value) == 0;
#endif
}

void CleanupYespowerCurrentThread() noexcept
{
    g_yespower_cleanup_has_run.store(true, std::memory_order_release);
    const auto key{GetYespowerKey()};
    if (auto* local{ReadYespowerLocal(key)}) {
        // Never leave a freed object for a later native thread-exit callback.
        if (!StoreYespowerLocal(key, nullptr)) std::abort();
        DeleteYespowerLocal(local);
    }
}

YespowerKey GetYespowerKey()
{
    static const YespowerKey key{[] {
        YespowerKey created{};
#ifdef WIN32
        created = FlsAlloc(DeleteYespowerLocal);
        if (created == FLS_OUT_OF_INDEXES) throw std::runtime_error("YespowerSugar: failed to create scratch key");
#else
        if (pthread_key_create(&created, DeleteYespowerLocal)) throw std::runtime_error("YespowerSugar: failed to create scratch key");
#endif
        if (std::atexit(CleanupYespowerCurrentThread)) {
            // Initialization may be retried, so release an unpublished key.
#ifdef WIN32
            if (!FlsFree(created)) std::abort();
#else
            if (pthread_key_delete(created)) std::abort();
#endif
            throw std::runtime_error("YespowerSugar: failed to register scratch cleanup");
        }
        return created;
    }()};
    return key;
}

YespowerLocal& GetYespowerLocal()
{
    const auto key{GetYespowerKey()};
    if (auto* local{ReadYespowerLocal(key)}) return *local;
    auto local{std::make_unique<YespowerLocal>()};
    // A later static destructor may hash after the first atexit cleanup.
    if (g_yespower_cleanup_has_run.load(std::memory_order_acquire) && std::atexit(CleanupYespowerCurrentThread)) {
        throw std::runtime_error("YespowerSugar: failed to register scratch cleanup");
    }
    if (!StoreYespowerLocal(key, local.get())) throw std::runtime_error("YespowerSugar: failed to store scratch memory");
    return *local.release();
}
#else // BITCOINKERNEL_YESPOWER_POOL
// No caller-thread callbacks survive a kernel call. This bounded module-owned
// pool retains at most eight workspaces; saturation uses temporary ownership.
struct YespowerSlot {
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    std::unique_ptr<YespowerLocal> local;
};
struct YespowerPool {
    std::array<YespowerSlot, 8> slots;
};
constinit std::atomic<YespowerPool*> g_yespower_pool{nullptr};
constinit std::atomic_flag g_yespower_pool_init = ATOMIC_FLAG_INIT;

struct ReleaseYespowerFlag {
    void operator()(std::atomic_flag* flag) const noexcept { flag->clear(std::memory_order_release); }
};
using YespowerFlagLease = std::unique_ptr<std::atomic_flag, ReleaseYespowerFlag>;

void CleanupYespowerPool() noexcept
{
    auto* pool{g_yespower_pool.exchange(nullptr, std::memory_order_acq_rel)};
    if (!pool) return;
    // As with every library call, active proof calls must finish before unload.
    for (const auto& slot : pool->slots) {
        if (slot.busy.test(std::memory_order_relaxed)) std::abort();
    }
    delete pool;
}

YespowerPool& GetYespowerPool()
{
    if (auto* pool{g_yespower_pool.load(std::memory_order_acquire)}) return *pool;
    while (g_yespower_pool_init.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
    YespowerFlagLease init_lock{&g_yespower_pool_init};
    if (auto* pool{g_yespower_pool.load(std::memory_order_relaxed)}) return *pool;
    auto pool{std::make_unique<YespowerPool>()};
    if (std::atexit(CleanupYespowerPool)) throw std::runtime_error("YespowerSugar: failed to register scratch cleanup");
    g_yespower_pool.store(pool.get(), std::memory_order_release);
    return *pool.release();
}

class YespowerScratch {
    std::unique_ptr<YespowerLocal> m_fallback;
    YespowerFlagLease m_lease;
    YespowerLocal* m_local{nullptr};
public:
    YespowerScratch()
    {
        auto& pool{GetYespowerPool()};
        for (auto& slot : pool.slots) {
            if (slot.busy.test_and_set(std::memory_order_acquire)) continue;
            // Establish release ownership before any allocation can throw.
            m_lease.reset(&slot.busy);
            if (!slot.local) slot.local = std::make_unique<YespowerLocal>();
            m_local = slot.local.get();
            return;
        }
        m_fallback = std::make_unique<YespowerLocal>();
        m_local = m_fallback.get();
    }
    YespowerScratch(const YespowerScratch&) = delete;
    YespowerScratch& operator=(const YespowerScratch&) = delete;
    YespowerLocal& Get() const { return *m_local; }
};
#endif // BITCOINKERNEL_YESPOWER_POOL
} // namespace

uint256 CBlockHeader::GetHash() const
{
    return (HashWriter{} << *this).GetHash();
}

// Official Sugarchain YespowerSugar parameters. The block identifier remains SHA256d.
uint256 CBlockHeader::GetPoWHash() const
{
    static_assert(YESPOWER_SUGAR_VERSION == YESPOWER_1_0);
    static constexpr yespower_params_t params{YESPOWER_1_0, YESPOWER_SUGAR_N, YESPOWER_SUGAR_R,
        YESPOWER_SUGAR_PERSONALIZATION, sizeof(YESPOWER_SUGAR_PERSONALIZATION) - 1};
    DataStream serialized;
    serialized << *this;
    yespower_binary_t result;
#ifdef BITCOINKERNEL_YESPOWER_POOL
    YespowerScratch scratch;
    YespowerLocal& local{scratch.Get()};
#else
    YespowerLocal& local{GetYespowerLocal()};
#endif
    if (yespower(&local.memory, reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size(), &params, &result)) {
        throw std::runtime_error("YespowerSugar: failed to compute proof of work");
    }
    uint256 hash;
    std::copy(std::begin(result.uc), std::end(result.uc), hash.begin());
    return hash;
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
