#include "PluginProcessor.h"

using namespace batida;

namespace
{
const juce::Identifier kSamplesTag ("SAMPLES");
const juce::Identifier kSampleTag ("SAMPLE");
const juce::Identifier kVoiceAttr ("voice");
const juce::Identifier kPathAttr ("path");

juce::String formatValue (const ParamSpec& spec, float v)
{
    const auto& u = spec.unit;

    // Host round trips leave values like -5e-7; show them as 0.
    if (std::abs (v) < 1.0e-4f)
        v = 0.0f;

    if (u == "ms")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s"
                            : juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms";
    if (u == "Hz")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
    if (u == "st")
        return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st";
    if (u == "dB")
        return v <= -60.0f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
    if (u == "%")
        return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (u == "bi")
        return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%";
    if (u == "pan")
        return std::abs (v) < 0.005f ? juce::String ("C")
                                     : (v < 0.0f ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
    if (u == "x")
        return juce::String (v, 3);
    if (u == "wave")
    {
        static const char* names[] = { "Sine", "Tri", "Saw", "Square" };
        const auto m = v * 3.0f;
        const auto i = std::min ((int) m, 2);
        const auto t = m - (float) i;
        if (t < 0.02f) return names[i];
        if (t > 0.98f) return names[i + 1];
        return juce::String (names[i]) + ">" + names[i + 1] + " " + juce::String (juce::roundToInt (t * 100.0f)) + "%";
    }
    return juce::String (v, 2);
}

float parseValue (const ParamSpec& spec, const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    auto v = t.retainCharacters ("0123456789.-+").getFloatValue();
    const auto& u = spec.unit;

    if (u == "ms" && t.endsWith ("s") && ! t.endsWith ("ms"))
        v *= 1000.0f;
    else if (u == "Hz" && t.contains ("k"))
        v *= 1000.0f;
    else if (u == "%" || u == "bi")
        v /= 100.0f;
    else if (u == "pan")
        v = t.startsWith ("l") ? -v / 100.0f : (t.startsWith ("r") ? v / 100.0f : 0.0f);
    else if (u == "wave")
        v = t.startsWith ("sine") ? 0.0f : t.startsWith ("tri") ? 1.0f / 3.0f
          : t.startsWith ("saw") ? 2.0f / 3.0f : t.startsWith ("sq") ? 1.0f : v;
    return juce::jlimit (spec.min, spec.max, v);
}

std::unique_ptr<juce::RangedAudioParameter> makeParameter (const ParamSpec& spec, const juce::String& id,
                                                           const juce::String& name, float def)
{
    const juce::ParameterID pid { id, 1 };

    switch (spec.kind)
    {
        case Kind::Choice:
        {
            juce::StringArray choices;
            for (const auto& c : spec.choices)
                choices.add (c);
            return std::make_unique<juce::AudioParameterChoice> (pid, name, choices, (int) def);
        }

        case Kind::Bool:
            return std::make_unique<juce::AudioParameterBool> (pid, name, def > 0.5f);

        case Kind::Float:
            break;
    }

    juce::NormalisableRange<float> range (spec.min, spec.max, spec.step);
    if (spec.centre > spec.min && spec.centre < spec.max && spec.centre != 0.5f * (spec.min + spec.max))
        range.setSkewForCentre (spec.centre);

    const auto attributes = juce::AudioParameterFloatAttributes()
                                .withStringFromValueFunction ([spec] (float v, int) { return formatValue (spec, v); })
                                .withValueFromStringFunction ([spec] (const juce::String& s) { return parseValue (spec, s); });

    return std::make_unique<juce::AudioParameterFloat> (pid, name, range, def, attributes);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout BatidaProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int g = 0; g < kNumGlobalParams; ++g)
    {
        const auto& spec = globalParamSpecs()[(size_t) g];
        layout.add (makeParameter (spec, globalParamID (g), spec.label, spec.def));
    }

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            "voice" + juce::String (v + 1), "Voice " + juce::String (v + 1), " | ");

        for (int p = 0; p < kNumVoiceParams; ++p)
            group->addChild (makeParameter (voiceParamSpecs()[(size_t) p], voiceParamID (v, p),
                                            voiceParamName (v, p), defaultVoiceValue (v, p)));

        layout.add (std::move (group));
    }

    return layout;
}

BatidaProcessor::BatidaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "BATIDA", createLayout())
{
    formats.registerBasicFormats();

    for (int v = 0; v < kNumVoices; ++v)
        for (int p = 0; p < kNumVoiceParams; ++p)
            voiceRaw[(size_t) v][(size_t) p] = state.getRawParameterValue (voiceParamID (v, p));

    for (int g = 0; g < kNumGlobalParams; ++g)
        globalRaw[(size_t) g] = state.getRawParameterValue (globalParamID (g));

    startTimer (1000);
}

BatidaProcessor::~BatidaProcessor()
{
    stopTimer();
}

void BatidaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    kit.prepare (sampleRate, samplesPerBlock);
}

bool BatidaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void BatidaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int v = 0; v < kNumVoices; ++v)
        for (int p = 0; p < kNumVoiceParams; ++p)
            params.voices[(size_t) v][p] = voiceRaw[(size_t) v][(size_t) p]->load (std::memory_order_relaxed);

    for (int g = 0; g < kNumGlobalParams; ++g)
        params.global[(size_t) g] = globalRaw[(size_t) g]->load (std::memory_order_relaxed);

    kit.setParameters (params);

    // Audition clicks from the editor: note-on first, so a click shorter than a
    // block still plays.
    const auto on = auditionOn.exchange (0);
    const auto off = auditionOff.exchange (0);
    for (int v = 0; v < kNumVoices; ++v)
    {
        if ((on >> v) & 1u)
            kit.noteOn (v, Voice::kBaseKey, 0.85f);
        if ((off >> v) & 1u)
            kit.noteOff (v, Voice::kBaseKey);
    }

    kit.process (buffer, midi);
}

void BatidaProcessor::audition (int voice, bool on)
{
    (on ? auditionOn : auditionOff).fetch_or (1u << voice);
}

void BatidaProcessor::setKeysVoice (int voice)
{
    auto* keys = state.getParameter (globalParamID (gp::KeysVoice));
    const auto value = keys->convertTo0to1 ((float) voice);
    if (keys->getValue() != value)
        keys->setValueNotifyingHost (value);
}

VoiceParams BatidaProcessor::readVoiceParams (int voice) const
{
    VoiceParams p;
    for (int i = 0; i < kNumVoiceParams; ++i)
        p[i] = voiceRaw[(size_t) voice][(size_t) i]->load (std::memory_order_relaxed);
    return p;
}

bool BatidaProcessor::loadSample (int voice, const juce::File& file)
{
    if (! kit.sampleSlot (voice).load (file, formats))
        return false;

    auto* mode = state.getParameter (voiceParamID (voice, vp::SrcMode));
    if ((SourceMode) juce::roundToInt (mode->convertFrom0to1 (mode->getValue())) == SourceMode::FM)
        mode->setValueNotifyingHost (mode->convertTo0to1 ((float) SourceMode::Sample));

    return true;
}

void BatidaProcessor::clearSample (int voice)
{
    kit.sampleSlot (voice).clear();
}

void BatidaProcessor::timerCallback()
{
    for (int v = 0; v < kNumVoices; ++v)
        kit.sampleSlot (v).collectGarbage();
}

void BatidaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName (kSamplesTag), nullptr);

    juce::ValueTree samples (kSamplesTag);
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto path = kit.sampleSlot (v).getPath();
        if (path.isNotEmpty())
            samples.appendChild (juce::ValueTree (kSampleTag, { { kVoiceAttr, v }, { kPathAttr, path } }), nullptr);
    }
    tree.appendChild (samples, nullptr);

    if (const auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void BatidaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto samples = tree.getChildWithName (kSamplesTag);
    tree.removeChild (samples, nullptr);
    state.replaceState (tree);

    std::array<juce::String, kNumVoices> paths;
    for (const auto& s : samples)
    {
        const auto v = (int) s.getProperty (kVoiceAttr, -1);
        if (v >= 0 && v < kNumVoices)
            paths[(size_t) v] = s.getProperty (kPathAttr).toString();
    }

    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& slot = kit.sampleSlot (v);
        if (paths[(size_t) v].isEmpty())
            slot.clear();
        else if (paths[(size_t) v] != slot.getPath() || slot.getStatus() != SampleSlot::Status::Loaded)
            slot.load (juce::File (paths[(size_t) v]), formats); // marks it missing if the file is gone
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BatidaProcessor();
}
