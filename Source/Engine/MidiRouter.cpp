#include "MidiRouter.h"

namespace batida
{

bool routeMidi (const juce::MidiMessage& message, MidiMode mode, VoiceEvent& out)
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

    if (channel < 1 || channel > kNumVoices)
        return false;

    out.voice = channel - 1;
    out.key = note;
    return true;
}

} // namespace batida
