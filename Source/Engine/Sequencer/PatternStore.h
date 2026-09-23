#pragma once

#include "Pattern.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace batida
{

// Owns the pattern bank. The message thread edits a working copy and
// publishes an immutable snapshot; the audio thread picks up the latest
// snapshot once per block. Old snapshots are freed on the message thread only
// after they have been out of use for a while (same scheme as SampleSlot).
class PatternStore
{
public:
    PatternStore();

    // Message thread ------------------------------------------------------
    const PatternBank& get() const { return working; }
    void edit (const std::function<void (PatternBank&)>& change); // change, then publish
    void replace (const PatternBank& bank);
    void collectGarbage();
    int getVersion() const { return version.load(); }

    // Audio thread --------------------------------------------------------
    const PatternBank* acquire()
    {
        auto* bank = latest.load (std::memory_order_acquire);
        inUse.store (bank, std::memory_order_release);
        return bank;
    }

private:
    void publish();

    struct Entry
    {
        std::unique_ptr<PatternBank> bank;
        double retiredAt = 0.0;
    };

    PatternBank working;
    std::vector<Entry> pool;
    std::atomic<PatternBank*> latest { nullptr }, inUse { nullptr };
    std::atomic<int> version { 0 };
};

} // namespace batida
