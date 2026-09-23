#include "PatternStore.h"

namespace batida
{

PatternStore::PatternStore()
{
    working = defaultPatternBank();
    publish();
}

void PatternStore::edit (const std::function<void (PatternBank&)>& change)
{
    change (working);
    publish();
}

void PatternStore::replace (const PatternBank& bank)
{
    working = bank;
    publish();
}

void PatternStore::publish()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (auto& e : pool)
        if (e.retiredAt == 0.0)
            e.retiredAt = now;

    pool.push_back ({ std::make_unique<PatternBank> (working), 0.0 });
    latest.store (pool.back().bank.get(), std::memory_order_release);
    ++version;
    collectGarbage();
}

void PatternStore::collectGarbage()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto* current = latest.load();
    const auto* used = inUse.load();

    pool.erase (std::remove_if (pool.begin(), pool.end(), [&] (const Entry& e)
                                {
                                    return e.retiredAt > 0.0 && now - e.retiredAt > 1000.0
                                        && e.bank.get() != current && e.bank.get() != used;
                                }),
                pool.end());
}

} // namespace batida
