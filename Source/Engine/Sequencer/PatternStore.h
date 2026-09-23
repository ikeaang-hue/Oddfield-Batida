#pragma once

#include "Engine/SnapshotStore.h"
#include "Pattern.h"

namespace batida
{

// The 16 patterns, edited on the message thread and published to the audio
// thread without locks. Starts with the default bank.
class PatternStore final : public SnapshotStore<PatternBank>
{
public:
    PatternStore() : SnapshotStore<PatternBank> (defaultPatternBank()) {}
};

} // namespace batida
