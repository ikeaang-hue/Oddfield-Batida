#include "MidiRouter.h"

#include <algorithm>

namespace batida
{

bool routeMidi (const juce::MidiMessage& message, MidiMode mode, int keysVoice, VoiceEvent& out)
{
    const bool on = message.isNoteOn();
    if (! on && ! message.isNoteOff())
        return false;

    const auto note = message.getNoteNumber();
    const auto channel = message.getChannel();

    out.isNoteOn = on;
    out.velocity = on ? message.getFloatVelocity() : 0.0f;

    const bool drumMap = mode == MidiMode::DrumMap || channel == kDrumChannel;

    if (drumMap)
    {
        if (note < kDrumMapFirstNote || note >= kDrumMapFirstNote + kNumVoices)
            return false;

        out.voice = note - kDrumMapFirstNote;
        out.key = 60;
        return true;
    }

    out.voice = std::clamp (keysVoice, 0, kNumVoices - 1);
    out.key = note;
    return true;
}

} // namespace batida
