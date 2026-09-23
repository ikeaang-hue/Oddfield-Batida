#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace batida
{

// Hands data edited on the message thread to the audio thread without locks.
// The message thread edits a working copy and publishes an immutable
// snapshot; the audio thread picks up the latest snapshot once per block. Old
// snapshots are freed on the message thread only after they have been out of
// use for a while (the same scheme as SampleSlot).
template <typename T>
class SnapshotStore
{
public:
    explicit SnapshotStore (T initial = {}) : working (std::move (initial)) { publish(); }
    virtual ~SnapshotStore() = default;

    // Message thread ------------------------------------------------------
    const T& get() const { return working; }
    void edit (const std::function<void (T&)>& change)
    {
        change (working);
        publish();
    }
    void replace (const T& value)
    {
        working = value;
        publish();
    }
    int getVersion() const { return version.load(); }

    void collectGarbage()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto* current = latest.load();
        const auto* used = inUse.load();
        pool.erase (std::remove_if (pool.begin(), pool.end(), [&] (const Entry& e)
                                    {
                                        return e.retiredAt > 0.0 && now - e.retiredAt > 1000.0
                                            && e.value.get() != current && e.value.get() != used;
                                    }),
                    pool.end());
    }

    // Audio thread --------------------------------------------------------
    const T* acquire()
    {
        auto* value = latest.load (std::memory_order_acquire);
        inUse.store (value, std::memory_order_release);
        return value;
    }

private:
    void publish()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (auto& e : pool)
            if (e.retiredAt == 0.0)
                e.retiredAt = now;
        pool.push_back ({ std::make_unique<T> (working), 0.0 });
        latest.store (pool.back().value.get(), std::memory_order_release);
        ++version;
        collectGarbage();
    }

    struct Entry
    {
        std::unique_ptr<T> value;
        double retiredAt = 0.0;
    };

    T working;
    std::vector<Entry> pool;
    std::atomic<T*> latest { nullptr }, inUse { nullptr };
    std::atomic<int> version { 0 };
};

} // namespace batida
