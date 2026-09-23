#pragma once

#include "Parameters.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace batida
{

// Where an incoming MIDI note goes.
//   Drum map (default): notes 36-43 play voices 1-8 on any channel, at the
//                       voice's own pitch.
//   Chromatic:          channel 10 is still the drum map; every other channel
//                       plays the Keys Voice like a mono synth (note 60 = the
//                       voice's own pitch).
struct VoiceEvent
{
    int voice = 0;
    int key = 60;
    float velocity = 0.0f;
    bool isNoteOn = false;
};

constexpr int kDrumMapFirstNote = 36;
constexpr int kDrumChannel = 10;

bool routeMidi (const juce::MidiMessage& message, MidiMode mode, int keysVoice, VoiceEvent& out);

} // namespace batida
