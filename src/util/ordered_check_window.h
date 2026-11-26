// Copyright (c) 2026 The Sugarchain En developers
// Distributed under the MIT software license, see the accompanying file COPYING.

#ifndef BITCOIN_UTIL_ORDERED_CHECK_WINDOW_H
#define BITCOIN_UTIL_ORDERED_CHECK_WINDOW_H

#include <algorithm>
#include <array>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace util {

/** A bounded ordered stream of checks. The caller participates as a worker.
 * Next supplies jobs in order, under the queue mutex. An empty result ends the
 * stream. Each job returns no value on success, a null exception pointer for
 * failed validation, or a real exception pointer for a local failure.
 */
template <typename Job, size_t Capacity>
class OrderedCheckWindow
{
    static_assert(Capacity > 0);
    static_assert(std::is_nothrow_move_constructible_v<Job> && std::is_nothrow_destructible_v<Job>);

    struct Slot {
        std::optional<Job> job;
        bool passed{false};
    };

    using Next = std::function<std::optional<Job>()>;
    using Result = std::optional<std::exception_ptr>;
    using Lock = std::unique_lock<std::mutex>;

    const size_t m_limit;
    std::mutex m_call_mutex;
    std::mutex m_mutex;
    std::condition_variable m_work_available;
    std::condition_variable m_caller_changed;
    std::array<Slot, Capacity> m_slots;
    std::vector<std::thread> m_workers;
    Next m_next;
    size_t m_retired{0};
    size_t m_started{0};
    size_t m_admitted{0};
    size_t m_active{0};
    size_t m_worker_waiters{0};
    bool m_caller_waiting{false};
    Result m_failure;
    bool m_exhausted{true};
    bool m_shutdown{false};

    bool HasWork() const { return !m_failure && m_started < m_admitted; }
    bool Complete() const { return (m_exhausted || m_failure) && !HasWork() && m_active == 0; }

    void WakeForWork(size_t published)
    {
        if (!HasWork() || published == 0) return;
        // A waiting caller can also take a new job.
        if (m_caller_waiting) {
            m_caller_changed.notify_one();
            --published;
        }
        const size_t wakes{std::min(published, m_worker_waiters)};
        for (size_t i{0}; i < wakes; ++i) m_work_available.notify_one();
    }

    void Fail(std::exception_ptr error)
    {
        // Never turn a local error from a started job into peer misbehavior.
        if (!m_failure || (!*m_failure && error)) m_failure.emplace(std::move(error));
    }

    void Refill()
    {
        // Only the contiguous successful prefix frees capacity. An unresolved
        // check at k leaves at most m_limit - 1 later jobs admitted.
        while (!m_exhausted && !m_failure && m_admitted - m_retired < m_limit) {
            try {
                auto job{m_next()};
                if (!job) {
                    m_exhausted = true;
                    break;
                }
                auto& slot{m_slots[m_admitted % Capacity]};
                assert(!slot.job && !slot.passed);
                slot.job.emplace(std::move(*job));
                ++m_admitted;
            } catch (...) {
                Fail(std::current_exception());
            }
        }
    }

    void CheckOne(Lock& lock)
    {
        const size_t index{m_started++};
        auto& slot{m_slots[index % Capacity]};
        assert(slot.job && !slot.passed);
        Job job{std::move(*slot.job)};
        slot.job.reset();
        ++m_active;
        lock.unlock();
        Result result;
        try {
            result = job();
        } catch (...) {
            result.emplace(std::current_exception());
        }
        lock.lock();
        --m_active;

        const size_t previous_admitted{m_admitted};
        if (result) Fail(std::move(*result));
        if (!m_failure) {
            slot.passed = true;
            while (m_retired < m_admitted && m_slots[m_retired % Capacity].passed) {
                m_slots[m_retired % Capacity].passed = false;
                ++m_retired;
            }
            Refill();
        }
        WakeForWork(m_admitted - previous_admitted);
        if (m_caller_waiting && Complete()) m_caller_changed.notify_one();
    }

    void WorkerLoop()
    {
        Lock lock{m_mutex};
        while (!m_shutdown) {
            if (HasWork()) {
                CheckOne(lock);
            } else {
                ++m_worker_waiters;
                m_work_available.wait(lock, [this] { return m_shutdown || HasWork(); });
                --m_worker_waiters;
            }
        }
    }

    void StopWorkers()
    {
        {
            Lock lock{m_mutex};
            m_shutdown = true;
            m_work_available.notify_all();
        }
        for (auto& worker : m_workers) worker.join();
    }

public:
    explicit OrderedCheckWindow(size_t participants, const std::function<void(size_t)>& name_worker = {})
        : m_limit{std::clamp(participants, size_t{1}, Capacity)}
    {
        m_workers.reserve(m_limit - 1);
        try {
            for (size_t i{0}; i < m_limit - 1; ++i) {
                m_workers.emplace_back([this, name_worker, i] {
                    try {
                        if (name_worker) name_worker(i);
                    } catch (...) {
                        // Diagnostic naming must not prevent a worker from running.
                    }
                    WorkerLoop();
                });
            }
        } catch (...) {
            StopWorkers();
            throw;
        }
    }

    ~OrderedCheckWindow() { StopWorkers(); }

    /** All started work finishes before the input and its references are released. */
    bool Run(Next next)
    {
        const std::lock_guard call{m_call_mutex};
        Lock lock{m_mutex};
        assert(m_active == 0 && !m_next && next);
        m_next = std::move(next);
        m_exhausted = false;
        Refill();
        // Initially publish the whole first window to the workers.
        if (HasWork() && m_worker_waiters != 0) m_work_available.notify_all();
        while (!Complete()) {
            if (HasWork()) {
                CheckOne(lock);
            } else {
                m_caller_waiting = true;
                m_caller_changed.wait(lock, [this] { return HasWork() || Complete(); });
                m_caller_waiting = false;
            }
        }

        auto failure{std::exchange(m_failure, std::nullopt)};
        for (auto& slot : m_slots) {
            slot.job.reset();
            slot.passed = false;
        }
        m_retired = m_started = m_admitted = 0;
        m_exhausted = true;
        m_next = {};
        if (failure && *failure) std::rethrow_exception(*failure);
        return !failure;
    }
};

} // namespace util

#endif // BITCOIN_UTIL_ORDERED_CHECK_WINDOW_H
