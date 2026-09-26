#include "PluginProcessor.h"

#include "Engine/BreakKit.h"
#include "Engine/ParamRange.h"
#include "Engine/Render.h"
#include "Engine/Sequencer/PatternMidi.h"
#include "Library/SampleLocator.h"

#include <FactoryData.h>

using namespace batida;

namespace
{
const juce::Identifier kSamplesTag ("SAMPLES");
const juce::Identifier kSampleTag ("SAMPLE");
const juce::Identifier kVoiceAttr ("voice");
const juce::Identifier kPathAttr ("path");
const juce::Identifier kPatternsTag ("PATTERNS");
const juce::Identifier kNamesTag ("NAMES");
const juce::Identifier kNameTag ("NAME");
const juce::Identifier kTextAttr ("text");
const juce::Identifier kMovementTag ("MOVEMENT");
const juce::Identifier kLibraryTag ("LIBRARY");
const juce::Identifier kBytesAttr ("bytes");

juce::String formatValue (const ParamSpec& spec, float v)
{
    const auto& u = spec.unit;

    // Host round trips leave values like -5e-7; show them as 0.
    if (std::abs (v) < 1.0e-4f)
        v = 0.0f;

    // juce::String (v, 0) would print every digit; whole numbers are rounded.
    auto fixed = [] (float x, int decimals) { return decimals == 0 ? juce::String (juce::roundToInt (x)) : juce::String (x, decimals); };
    if (u == "ms")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s"
                            : fixed (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms";
    if (u == "Hz")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                            : fixed (v, v < 100.0f ? 1 : 0) + " Hz";
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
    if (u == "n")
        return juce::String (juce::roundToInt (v));
    if (u == "bpm")
        return juce::String (v, 2) + " bpm";
    if (u == "dist")
    {
        static const char* names[] = { "Tape", "Tube", "Clip", "Fold", "Crush" };
        const auto m = v * 4.0f;
        const auto i = std::min ((int) m, 3);
        const auto t = m - (float) i;
        if (t < 0.02f) return names[i];
        if (t > 0.98f) return names[i + 1];
        return juce::String (names[i]) + ">" + names[i + 1] + " " + juce::String (juce::roundToInt (t * 100.0f)) + "%";
    }
    if (u == "wave")
    {
        static const char* names[] = { "Sine", "Tri", "Saw", "Square", "Noise" };
        const auto m = v * 4.0f;
        const auto i = std::min ((int) m, 3);
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
    const auto& u = spec.unit;

    // "-inf dB" is the bottom of the range; text with no number keeps the default.
    if (t.contains ("inf"))
        return t.startsWith ("-") ? spec.min : spec.max;
    const auto digits = t.retainCharacters ("0123456789");
    const auto named = u == "wave" || (u == "pan" && t.startsWith ("c"));
    if (digits.isEmpty() && ! named)
        return spec.def;

    auto v = t.retainCharacters ("0123456789.-+").getFloatValue();

    if (u == "ms" && t.endsWith ("s") && ! t.endsWith ("ms"))
        v *= 1000.0f;
    else if (u == "Hz" && t.contains ("k"))
        v *= 1000.0f;
    else if (u == "%" || u == "bi")
        v /= 100.0f;
    else if (u == "pan")
        v = t.startsWith ("l") ? -v / 100.0f : (t.startsWith ("r") ? v / 100.0f : 0.0f);
    else if (u == "wave")
        v = t.startsWith ("sine") ? 0.0f : t.startsWith ("tri") ? 0.25f : t.startsWith ("saw") ? 0.5f
          : t.startsWith ("sq") ? 0.75f : t.startsWith ("noise") ? 1.0f : v;
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

    const auto range = rangeFor (spec); // shared with the engine's modulation

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
            "voice" + juce::String (v + 1), "Sound " + juce::String (v + 1), " | "); // the ID stays "voice"

        for (int p = 0; p < kNumVoiceParams; ++p)
            group->addChild (makeParameter (voiceParamSpecs()[(size_t) p], voiceParamID (v, p),
                                            voiceParamName (v, p), defaultVoiceValue (v, p)));

        layout.add (std::move (group));
    }

    return layout;
}

BatidaProcessor::BatidaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "BATIDA", createLayout())
{
    batida::Library::setFactoryArchive (FactoryData::Factory_zip, (size_t) FactoryData::Factory_zipSize);
    formats.registerBasicFormats();

    for (int v = 0; v < kNumVoices; ++v)
        for (int p = 0; p < kNumVoiceParams; ++p)
            voiceRaw[(size_t) v][(size_t) p] = state.getRawParameterValue (voiceParamID (v, p));

    for (int g = 0; g < kNumGlobalParams; ++g)
        globalRaw[(size_t) g] = state.getRawParameterValue (globalParamID (g));

    for (int v = 0; v < kNumVoices; ++v)
        for (int p = 0; p < kNumVoiceParams; ++p)
            paramRefs[voiceParamID (v, p)] = { true, false, v, p };
    for (int g = 0; g < kNumGlobalParams; ++g)
        paramRefs[globalParamID (g)] = { true, true, 0, g };

    sampleBytes.fill (-1);
    startTimer (1000);
}

BatidaProcessor::~BatidaProcessor()
{
    stopTimer();
    varyPool.removeAllJobs (true, 5000);
}

void BatidaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    kit.prepare (sampleRate, samplesPerBlock);
    sidechainCopy.setSize (2, std::max (1, samplesPerBlock));
    setLatencySamples (kit.getLatencySamples());
}

bool BatidaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet(); // the sidechain
    const auto okOut = out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
    const auto okIn = in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
    return okOut && okIn;
}

void BatidaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int v = 0; v < kNumVoices; ++v)
        for (int p = 0; p < kNumVoiceParams; ++p)
            params.voices[(size_t) v][p] = voiceRaw[(size_t) v][(size_t) p]->load (std::memory_order_relaxed);

    for (int g = 0; g < kNumGlobalParams; ++g)
        params.global[(size_t) g] = globalRaw[(size_t) g]->load (std::memory_order_relaxed);

    // Auditioning a Vary suggestion: that voice plays the suggestion.
    if (const auto* p = preview.acquire(); p != nullptr && p->active)
        for (int v = 0; v < kNumVoices; ++v)
            if ((p->mask >> v) & 1u)
                params.voices[(size_t) v] = p->params[(size_t) v];

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

    // The sidechain arrives in the same buffer the output is written to, so
    // copy it out first.
    const juce::AudioBuffer<float>* sidechain = nullptr;
    if (auto* bus = getBus (true, 0); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0)
    {
        const auto in = getBusBuffer (buffer, true, 0);
        const auto n = buffer.getNumSamples();
        if (sidechainCopy.getNumSamples() < n)
            sidechainCopy.setSize (2, n, false, false, true); // only if the host exceeds its block size
        for (int ch = 0; ch < 2; ++ch)
        {
            sidechainCopy.copyFrom (ch, 0, in, std::min (ch, in.getNumChannels() - 1), 0, n);
            auto* x = sidechainCopy.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (x[i]))
                    x[i] = 0.0f;
        }
        sidechain = &sidechainCopy;
    }

    // Where the host is: the sequencer follows its bar/beat position and tempo.
    Transport transport;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            const auto ppq = pos->getPpqPosition();
            transport.hostPlaying = pos->getIsPlaying() && ppq.hasValue();
            transport.ppq = ppq.orFallback (0.0);
            transport.bpm = pos->getBpm().orFallback (120.0);
            if (const auto sig = pos->getTimeSignature())
                transport.beatsPerBar = sig->numerator * 4.0 / std::max (1, sig->denominator);
        }

    // Only the output bus: with a mono output and a stereo sidechain the
    // buffer has two channels, but the kit must mix down to one.
    buffer.clear();
    auto out = getBusBuffer (buffer, false, 0);
    kit.process (out, midi, sidechain, transport);

    // Peak hold for the editor's meter: the highest level until it's read.
    for (int ch = 0; ch < std::min (2, out.getNumChannels()); ++ch)
    {
        const auto level = out.getMagnitude (ch, 0, out.getNumSamples());
        auto& held = outputPeaks[(size_t) ch];
        auto was = held.load (std::memory_order_relaxed);
        while (level > was && ! held.compare_exchange_weak (was, level, std::memory_order_relaxed)) {}
    }
}

void BatidaProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Bypassed: silence. The input bus is the sidechain, which must not pass through.
    buffer.clear();
}

std::array<float, 2> BatidaProcessor::takeOutputPeaks()
{
    return { outputPeaks[0].exchange (0.0f), outputPeaks[1].exchange (0.0f) };
}

int BatidaProcessor::displayPattern() const
{
    const auto& seq = kit.getSequencer();
    if (seq.isRunning())
        return seq.getActivePattern();
    return juce::roundToInt (globalRaw[(size_t) gp::SeqPattern]->load());
}

juce::String BatidaProcessor::getVoiceName (int voice) const
{
    const auto& n = voiceNames[(size_t) voice];
    return n.isNotEmpty() ? n : juce::String (defaultVoiceName (voice));
}

void BatidaProcessor::setVoiceName (int voice, const juce::String& name)
{
    if (name.trim().substring (0, 24) != getVoiceName (voice))
        beginUndoStep();
    auto clean = name.trim().substring (0, 24);
    if (clean == defaultVoiceName (voice))
        clean = {};
    voiceNames[(size_t) voice] = clean;
    ++namesVersion;
}

void BatidaProcessor::swapVoices (int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= kNumVoices || b >= kNumVoices)
        return;
    beginUndoStep (true, true);
    ++actionCounter;
    kit.beginSwitch ((1u << a) | (1u << b)); // sounding slots fade before their sounds trade places
    clearPreviews();

    // Samples first, as they are in memory (no disk reads, so a sample whose
    // file has gone since keeps playing): each slot takes the other's.
    struct Held { SampleSlot::Status status; juce::String path; std::shared_ptr<const SampleData> data; };
    const Held ha { kit.sampleSlot (a).getStatus(), kit.sampleSlot (a).getPath(), kit.sampleSlot (a).share() };
    const Held hb { kit.sampleSlot (b).getStatus(), kit.sampleSlot (b).getPath(), kit.sampleSlot (b).share() };
    auto takeOver = [this] (int voice, const Held& h)
    {
        auto& slot = kit.sampleSlot (voice);
        if (h.status == SampleSlot::Status::Loaded && h.data != nullptr)
            slot.setShared (h.data);
        else if (h.path.isEmpty())
            slot.clear();
        else if (h.status == SampleSlot::Status::Error)
            slot.markUnreadable (h.path);
        else
            slot.markMissing (h.path);
    };
    takeOver (a, hb);
    takeOver (b, ha);

    // Settings (host parameters, so the host sees the change).
    for (int p = 0; p < kNumVoiceParams; ++p)
    {
        auto* pa = state.getParameter (voiceParamID (a, p));
        auto* pb = state.getParameter (voiceParamID (b, p));
        const auto va = pa->getValue(), vb = pb->getValue();
        for (auto [param, value] : { std::pair { pa, vb }, std::pair { pb, va } })
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (value);
            param->endChangeGesture();
        }
    }

    // What each sound was loaded from (Save writes there) and its sample's
    // size (for finding it again) go with it.
    std::swap (origins.soundFiles[(size_t) a], origins.soundFiles[(size_t) b]);
    std::swap (sampleBytes[(size_t) a], sampleBytes[(size_t) b]);
    ++originsVersion;

    // Names (an unnamed slot keeps showing its sound's default name).
    const auto na = getVoiceName (a), nb = getVoiceName (b);
    voiceNames[(size_t) a] = nb == defaultVoiceName (a) ? juce::String() : nb;
    voiceNames[(size_t) b] = na == defaultVoiceName (b) ? juce::String() : na;
    ++namesVersion;

    // The tracks in every pattern.
    kit.patternStore().edit ([&] (PatternBank& bank)
    {
        for (auto& pat : bank.patterns)
            std::swap (pat.tracks[(size_t) a], pat.tracks[(size_t) b]);
    });

    // Keys Voice follows its sound.
    auto* keys = state.getParameter (globalParamID (gp::KeysVoice));
    const auto k = juce::roundToInt (keys->convertFrom0to1 (keys->getValue()));
    if (k == a || k == b)
    {
        keys->beginChangeGesture();
        keys->setValueNotifyingHost (keys->convertTo0to1 ((float) (k == a ? b : a)));
        keys->endChangeGesture();
    }
}

// Undo -------------------------------------------------------------------------

juce::int64 BatidaProcessor::historyStamp() const
{
    return (juce::int64) kit.getPatternVersion() * 1000003 + (juce::int64) namesVersion.load() * 7919 + actionCounter;
}

HistorySnapshot BatidaProcessor::snapshot (bool withParameters, bool withSamples) const
{
    HistorySnapshot s;
    s.patterns = const_cast<Kit&> (kit).patternStore().get();
    s.names = voiceNames;
    if (const auto xml = originsToXml())
        s.origins = xml->toString();
    if (withParameters)
    {
        std::vector<float> values;
        for (auto* p : getParameters())
            values.push_back (p->getValue());
        s.parameters = values;
    }
    if (withSamples)
    {
        std::array<juce::String, kNumVoices> paths;
        for (int v = 0; v < kNumVoices; ++v)
            paths[(size_t) v] = const_cast<Kit&> (kit).sampleSlot (v).getPath();
        s.samples = paths;
    }
    return s;
}

void BatidaProcessor::restore (const HistorySnapshot& s)
{
    clearPreviews();
    kit.patternStore().replace (s.patterns);
    voiceNames = s.names;
    ++namesVersion;
    if (const auto xml = juce::parseXML (s.origins))
        originsFromXml (xml.get());

    // Samples decode before anything switches, then the sounds change together.
    std::array<std::shared_ptr<const SampleData>, kNumVoices> decoded;
    uint32_t changed = 0;
    if (s.samples)
    {
        SampleCache cache;
        for (int v = 0; v < kNumVoices; ++v)
        {
            const auto& path = (*s.samples)[(size_t) v];
            if (path == kit.sampleSlot (v).getPath())
                continue;
            changed |= 1u << v;
            if (path.isNotEmpty())
                decoded[(size_t) v] = sharedSample (path, cache);
        }
    }

    if (s.parameters || changed != 0)
        kit.beginSwitch (0xffu); // sounding voices fade before their sounds change
    for (int v = 0; v < kNumVoices; ++v)
        if ((changed >> v) & 1u)
        {
            const auto& path = (*s.samples)[(size_t) v];
            placeSample (v, path, decoded[(size_t) v]);
            sampleBytes[(size_t) v] = juce::File (path).existsAsFile() ? juce::File (path).getSize() : -1;
        }
    if (s.parameters)
    {
        const auto& all = getParameters();
        for (size_t i = 0; i < (size_t) all.size() && i < s.parameters->size(); ++i)
            if (! juce::exactlyEqual (all[(int) i]->getValue(), (*s.parameters)[i]))
            {
                all[(int) i]->beginChangeGesture();
                all[(int) i]->setValueNotifyingHost ((*s.parameters)[i]);
                all[(int) i]->endChangeGesture();
            }
    }
    ++actionCounter;
}

std::shared_ptr<const SampleData> BatidaProcessor::sharedSample (const juce::String& path, SampleCache& cache)
{
    if (const auto it = cache.find (path); it != cache.end())
        return it->second;
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& slot = kit.sampleSlot (v);
        if (slot.getStatus() == SampleSlot::Status::Loaded && slot.getPath() == path)
            if (auto held = slot.share())
                return cache[path] = held;
    }
    juce::String note;
    std::shared_ptr<const SampleData> data = SampleSlot::decode (juce::File (path), formats, note);
    return cache[path] = data;
}

void BatidaProcessor::placeSample (int voice, const juce::String& path, const std::shared_ptr<const SampleData>& data)
{
    auto& slot = kit.sampleSlot (voice);
    if (path.isEmpty())
        slot.clear();
    else if (data != nullptr)
        slot.setShared (data);
    else if (juce::File (path).existsAsFile())
        slot.markUnreadable (path);
    else
        slot.markMissing (path);
}

void BatidaProcessor::beginUndoStep (bool withParameters, bool withSamples)
{
    history.beginStep (snapshot (withParameters, withSamples), historyStamp());
}

bool BatidaProcessor::canUndo() const
{
    return history.canUndo (historyStamp());
}

void BatidaProcessor::undo()
{
    // The redo side keeps the same kind of snapshot (parameters and samples
    // included) so redo restores exactly what undo replaced.
    const auto now = snapshot (true, true);
    if (auto s = history.undo (now, historyStamp()))
    {
        restore (*s);
        history.finalise (historyStamp()); // nothing pending after an undo
    }
}

void BatidaProcessor::redo()
{
    if (auto s = history.redo (snapshot (true, true)))
        restore (*s);
}

// Movement -------------------------------------------------------------------------

BatidaProcessor::ParamRef BatidaProcessor::refFor (const juce::String& paramID) const
{
    const auto it = paramRefs.find (paramID);
    return it == paramRefs.end() ? ParamRef {} : it->second;
}

bool BatidaProcessor::isModulatable (const juce::String& paramID) const
{
    const auto r = refFor (paramID);
    return r.valid && (r.global ? isModulatableGlobal (r.param) : isModulatableVoice (r.param));
}

bool BatidaProcessor::isModTarget (int mod, const juce::String& paramID) const
{
    const auto r = refFor (paramID);
    const ModTarget probe { true, r.global, r.voice, r.param, 0.0f };
    for (const auto& t : kit.movementStore().get().mods[(size_t) mod].targets)
        if (t == probe)
            return true;
    return false;
}

void BatidaProcessor::toggleModTarget (int mod, const juce::String& paramID)
{
    if (! isModulatable (paramID))
        return;
    const auto r = refFor (paramID);
    const ModTarget probe { true, r.global, r.voice, r.param, 0.25f };
    kit.movementStore().edit ([&] (MovementData& d)
    {
        auto& targets = d.mods[(size_t) mod].targets;
        for (auto& t : targets)
            if (t == probe)
            {
                t = {};
                return;
            }
        for (auto& t : targets)
            if (! t.active)
            {
                t = probe;
                return;
            }
    });
}

std::optional<BatidaProcessor::ModDisplay> BatidaProcessor::modDisplayFor (const juce::String& paramID) const
{
    const auto r = refFor (paramID);
    if (! r.valid)
        return std::nullopt;

    const auto& data = kit.movementStore().get();
    float live = 0.0f, low = 0.0f, high = 0.0f;
    bool any = false;
    for (int m = 0; m < kNumMods; ++m)
    {
        const auto amount = globalRaw[(size_t) modParam (m, ModAmount)]->load();
        const auto bipolar = globalRaw[(size_t) modParam (m, ModPolarity)]->load() > 0.5f;
        for (const auto& t : data.mods[(size_t) m].targets)
            if (t.active && t.global == r.global && t.param == r.param && (r.global || t.voice == r.voice))
            {
                any = true;
                live += t.depth * kit.getModulators().getUiValue (m);
                const auto a = t.depth * amount, b = bipolar ? -t.depth * amount : 0.0f;
                low += std::min (a, b);
                high += std::max (a, b);
            }
    }
    if (! any)
        return std::nullopt;

    const auto& range = r.global ? globalRanges()[(size_t) r.param] : voiceRanges()[(size_t) r.param];
    const auto base = range.convertTo0to1 (r.global ? globalRaw[(size_t) r.param]->load()
                                                    : voiceRaw[(size_t) r.voice][(size_t) r.param]->load());
    auto at = [&] (float offset) { return (double) range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, base + offset)); };
    return ModDisplay { at (live), at (low), at (high) };
}

void BatidaProcessor::storeScene (int scene)
{
    kit.movementStore().edit ([&] (MovementData& d)
    {
        auto& sc = d.scenes[(size_t) scene];
        sc.stored = true;
        for (const auto g : sceneParams())
            sc.values[(size_t) g] = globalRaw[(size_t) g]->load();
    });
}

void BatidaProcessor::recallScene (int scene)
{
    const auto& sc = kit.movementStore().get().scenes[(size_t) scene];
    if (! sc.stored)
        return;
    beginUndoStep (true);
    ++actionCounter;
    for (const auto g : sceneParams())
    {
        auto* p = state.getParameter (globalParamID (g));
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (sc.values[(size_t) g]));
        p->endChangeGesture();
    }
}

// Vary -----------------------------------------------------------------------------

void BatidaProcessor::startVary (int voice, float amount, VaryDirection direction, bool lockSource, bool lockFx, bool lockEnvelopes)
{
    if (varyBusy.exchange (true))
        return;
    previewCandidate (-1);

    VaryRequest req;
    req.base = readVoiceParams (voice);
    req.keepAlive = kit.sampleSlot (voice).share(); // freed only after the job is done with it
    req.sample = req.keepAlive.get();
    req.amount = amount;
    req.direction = direction;
    req.lockSource = lockSource;
    req.lockFx = lockFx;
    req.lockEnvelopes = lockEnvelopes;
    req.seed = (uint32_t) juce::Random::getSystemRandom().nextInt();

    juce::WeakReference<BatidaProcessor> safe (this);
    varyPool.addJob ([safe, req, voice, generation = varyGeneration]
    {
        auto found = vary (req);
        juce::MessageManager::callAsync ([safe, result = std::move (found), voice, base = req.base, generation]() mutable
        {
            if (auto* self = safe.get())
            {
                self->varyBusy = false;
                ++self->varyResults;
                if (generation != self->varyGeneration)
                {
                    ++self->varyVersion; // the sound changed while it ran: these no longer apply
                    return;
                }
                self->candidates = std::move (result);
                self->candidatesBase = base;
                self->candidatesVoice = voice;
                self->previewIndex = -1;
                self->publishPreview(); // a tile pressed while it ran stops sounding
            }
        });
    });
}

// A suggestion with the voice's mix settings as they are now; Level gets the
// suggestion's loudness match on top of the current Level.
VoiceParams BatidaProcessor::withLiveMix (const VaryCandidate& c, const VoiceParams& live)
{
    auto p = c.params;
    for (const auto k : { vp::Pan, vp::ChainAmt, vp::Mute, vp::Solo })
        p[k] = live[k];
    const auto& level = voiceParamSpecs()[(size_t) vp::Level];
    p[vp::Level] = std::clamp (live[vp::Level] + c.levelDb, level.min, level.max);
    return p;
}

void BatidaProcessor::publishPreview()
{
    Preview p;
    if (previewIndex >= 0)
    {
        p.active = true;
        p.mask = 1u << candidatesVoice;
        p.params[(size_t) candidatesVoice] = withLiveMix (candidates[(size_t) previewIndex], readVoiceParams (candidatesVoice));
    }
    else if (kitPreviewIndex >= 0)
    {
        p.active = true;
        const auto& c = kitCands[(size_t) kitPreviewIndex];
        p.mask = c.changed;
        for (int v = 0; v < kNumVoices; ++v)
            if ((c.changed >> v) & 1u)
            {
                const auto live = readVoiceParams (v);
                auto& q = p.params[(size_t) v];
                q = c.params[(size_t) v];
                // The suggestion's level match on top of the slot's mix as it is now.
                const auto& level = voiceParamSpecs()[(size_t) vp::Level];
                q[vp::Level] = std::clamp (live[vp::Level] + (q[vp::Level] - kitBase[(size_t) v][vp::Level]), level.min, level.max);
                for (const auto k : { vp::Pan, vp::ChainAmt, vp::Mute, vp::Solo })
                    q[k] = live[k];
            }
    }
    preview.replace (p);
    ++varyVersion;
}

void BatidaProcessor::previewCandidate (int index)
{
    previewIndex = index >= 0 && index < (int) candidates.size() ? index : -1;
    if (previewIndex >= 0)
        kitPreviewIndex = -1;
    publishPreview();
}

void BatidaProcessor::startKitVary (float amount, VaryDirection direction, bool lockSource, bool lockFx, bool lockEnvelopes,
                                    std::array<bool, kNumVoices> lockedSlots)
{
    if (kitVaryBusy.exchange (true))
        return;
    previewCandidate (-1);
    previewKitCandidate (-1);

    std::vector<std::pair<int, VaryRequest>> requests;
    std::array<VoiceParams, kNumVoices> base;
    const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt();
    for (int v = 0; v < kNumVoices; ++v)
    {
        base[(size_t) v] = readVoiceParams (v);
        if (lockedSlots[(size_t) v])
            continue;
        VaryRequest req;
        req.base = base[(size_t) v];
        req.keepAlive = kit.sampleSlot (v).share();
        req.sample = req.keepAlive.get();
        req.amount = amount;
        req.direction = direction;
        req.lockSource = lockSource;
        req.lockFx = lockFx;
        req.lockEnvelopes = lockEnvelopes;
        req.seed = seed + (uint32_t) v * 7919u;
        requests.push_back ({ v, req });
    }

    juce::WeakReference<BatidaProcessor> safe (this);
    varyPool.addJob ([safe, requests, base, generation = varyGeneration]
    {
        std::array<std::vector<VaryCandidate>, kNumVoices> results;
        for (const auto& [v, req] : requests)
            results[(size_t) v] = vary (req);

        std::vector<KitCandidate> built;
        for (int i = 0; i < 4; ++i)
        {
            KitCandidate k;
            k.params = base;
            for (int v = 0; v < kNumVoices; ++v)
                if (i < (int) results[(size_t) v].size())
                {
                    k.params[(size_t) v] = results[(size_t) v][(size_t) i].params;
                    k.changed |= 1u << v;
                }
            if (k.changed == 0)
                continue;
            const auto n = juce::countNumberOfBits (k.changed);
            k.note = std::to_string (n) + (n == 1 ? " sound" : " sounds");
            built.push_back (std::move (k));
        }
        juce::MessageManager::callAsync ([safe, kits = std::move (built), base, generation]() mutable
        {
            if (auto* self = safe.get())
            {
                self->kitVaryBusy = false;
                ++self->varyResults;
                if (generation != self->varyGeneration)
                {
                    ++self->varyVersion; // the kit changed while it ran: these no longer apply
                    return;
                }
                self->kitCands = std::move (kits);
                self->kitBase = base;
                self->kitVaried = true;
                self->kitPreviewIndex = -1;
                self->publishPreview();
            }
        });
    });
}

void BatidaProcessor::previewKitCandidate (int index)
{
    kitPreviewIndex = index >= 0 && index < (int) kitCands.size() ? index : -1;
    if (kitPreviewIndex >= 0)
        previewIndex = -1;
    publishPreview();
}

void BatidaProcessor::keepKitCandidate()
{
    if (kitPreviewIndex < 0)
        return;
    const auto kept = preview.get(); // what's being heard
    if (! kept.active)
        return;

    beginUndoStep (true);
    ++actionCounter;
    for (int v = 0; v < kNumVoices; ++v)
        if ((kept.mask >> v) & 1u)
            writeVoiceParameters (v, kept.params[(size_t) v]);
    kitPreviewIndex = -1;
    kitCands.clear();
    kitVaried = false;
    ++varyResults;
    publishPreview();
}

void BatidaProcessor::clearPreviews()
{
    kit.stopAudition();
    ++varyGeneration; // results of a Vary still running are for sounds that are gone
    const auto hadResults = kitVaried || ! candidates.empty();
    kitCands.clear();
    kitVaried = false;
    candidates.clear();
    candidatesVoice = -1;
    if (hadResults)
        ++varyResults;
    if (previewIndex >= 0 || kitPreviewIndex >= 0 || hadResults)
    {
        previewIndex = kitPreviewIndex = -1;
        publishPreview();
    }
    if (patternPreviewIndex >= 0 || ! patternCands.empty())
    {
        patternCands.clear();
        patternCandsFor = -1;
        previewPatternCandidate (-1);
    }
}

// Pattern Vary --------------------------------------------------------------------

void BatidaProcessor::startPatternVary (int pattern, float amount, PatternDirection direction, std::array<bool, kNumTracks> lockedTracks)
{
    previewPatternCandidate (-1);
    PatternVaryRequest req;
    req.base = kit.patternStore().get().patterns[(size_t) pattern];
    req.amount = amount;
    req.direction = direction;
    req.locked = lockedTracks;
    req.seed = (uint32_t) juce::Random::getSystemRandom().nextInt();
    patternCands = varyPattern (req);
    patternCandsFor = pattern;
    ++patternVaryVersion;
}

void BatidaProcessor::previewPatternCandidate (int index)
{
    patternPreviewIndex = index >= 0 && index < (int) patternCands.size() ? index : -1;
    PatternPreview pv;
    if (patternPreviewIndex >= 0)
    {
        pv.active = true;
        pv.index = patternCandsFor;
        pv.pattern = patternCands[(size_t) patternPreviewIndex].pattern;
    }
    kit.patternPreview().replace (pv);
    ++patternVaryVersion;
}

void BatidaProcessor::keepPatternCandidate()
{
    if (patternPreviewIndex < 0)
        return;
    const auto chosen = patternCands[(size_t) patternPreviewIndex].pattern;
    const auto pattern = patternCandsFor;
    beginUndoStep();
    kit.patternStore().edit ([&] (PatternBank& b) { b.patterns[(size_t) pattern] = chosen; });
    patternCands.clear();
    patternCandsFor = -1;
    previewPatternCandidate (-1);
}

void BatidaProcessor::writeVoiceParameters (int voice, const VoiceParams& values)
{
    for (int k = 0; k < kNumVoiceParams; ++k)
    {
        auto* p = state.getParameter (voiceParamID (voice, k));
        const auto norm = p->convertTo0to1 (values[k]);
        if (std::abs (p->getValue() - norm) > 1.0e-6f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
    }
}

void BatidaProcessor::keepCandidate()
{
    if (previewIndex < 0)
        return;
    const auto voice = candidatesVoice;
    const auto values = withLiveMix (candidates[(size_t) previewIndex], readVoiceParams (voice));

    beginUndoStep (true);
    ++actionCounter;
    writeVoiceParameters (voice, values);
    previewCandidate (-1);
    candidates.clear();
    candidatesVoice = -1; // back to "press Vary", not "nothing found"
    ++varyResults;
    ++varyVersion;
}

// Sampling -------------------------------------------------------------------------

std::optional<BatidaProcessor::BreakResult> BatidaProcessor::loadBreak (const juce::File& file, int pattern, juce::String* error)
{
    juce::String note;
    std::shared_ptr<const SampleData> loop = SampleSlot::decode (file, formats, note);
    if (loop == nullptr)
    {
        if (error != nullptr)
            *error = "Can't read " + file.getFileName();
        return std::nullopt;
    }
    const auto plan = planBreak (*loop);
    if (! plan)
    {
        if (error != nullptr)
            *error = "No loop at a playable tempo in " + file.getFileName();
        return std::nullopt;
    }

    beginLoadStep ("break", LoadMode::Step);
    uint32_t mask = 0;
    for (int v = 0; v < kNumVoices; ++v)
        mask |= plan->used[(size_t) v] ? 1u << v : 0u;
    kit.beginSwitch (mask);
    for (int v = 0; v < kNumVoices; ++v)
    {
        if (! plan->used[(size_t) v])
            continue;
        kit.sampleSlot (v).setShared (loop); // one decoded loop behind every slot that plays it
        sampleBytes[(size_t) v] = file.getSize();
        auto values = plan->voices[(size_t) v];
        const auto live = readVoiceParams (v);
        for (const auto k : { vp::Pan, vp::Mute, vp::Solo })
            values[k] = live[k];
        writeVoiceParameters (v, values);
        setNameQuietly (v, plan->names[(size_t) v]);
        origins.soundFiles[(size_t) v] = juce::File();
    }
    kit.patternStore().edit ([&] (PatternBank& b) { b.patterns[(size_t) pattern] = plan->pattern; });
    origins.patternNames[(size_t) pattern] = file.getFileNameWithoutExtension().substring (0, 24);
    origins.patternFiles[(size_t) pattern] = juce::File();
    writeGlobalParameter (gp::SeqTempo, (float) (std::round (plan->bpm * 100.0) / 100.0)); // the loop's own tempo
    endLoadStep ("break", LoadMode::Step);
    return BreakResult { (int) plan->hits.size(), plan->steps, (double) readGlobalParams()[gp::SeqTempo] }; // the tempo as set
}

bool BatidaProcessor::loadRendered (const juce::AudioBuffer<float>& audio, int target, const juce::String& name, bool loop, juce::String* error)
{
    // Written to the library, so the sound can be saved and found again.
    const auto folder = library().folderFor (PresetType::Sound, false).getSiblingFile ("Samples").getChildFile ("Resampled");
    folder.createDirectory();
    const auto file = folder.getChildFile (safeFileName (name) + ".wav").getNonexistentSibling();
    {
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
        juce::WavAudioFormat wav;
        const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        auto writer = out != nullptr ? wav.createWriterFor (out, juce::AudioFormatWriterOptions {}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24))
                                     : nullptr;
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        {
            if (error != nullptr)
                *error = "Couldn't write " + file.getFullPathName();
            return false;
        }
    }

    // A plain sample sound that plays the render as it is: to its end, dry
    // (the chain is already in it). Pan, Mute and Solo stay as they were.
    SoundPreset sound = initSoundPreset();
    sound.info.name = name.substring (0, 24);
    auto& p = sound.params;
    p[vp::SrcMode] = (float) SourceMode::Sample;
    p[vp::PlayMode] = (float) PlayMode::OneShot;
    p[vp::AmpA] = 0.0f;
    p[vp::AmpD] = 8000.0f;
    p[vp::AmpS] = 1.0f;
    p[vp::AmpR] = 8000.0f;
    p[vp::SmpFadeOut] = loop ? 0.0f : 4.0f;
    p[vp::ChainAmt] = 0.0f;
    sound.sample = { file.getFullPathName(), file.getSize() };
    applySound (target, sound, {});
    return kit.sampleSlot (target).getStatus() == SampleSlot::Status::Loaded;
}

namespace
{
RenderRequest renderRequestFor (BatidaProcessor& proc, const Kit& kit)
{
    RenderRequest req;
    for (int v = 0; v < kNumVoices; ++v)
    {
        req.params.voices[(size_t) v] = proc.readVoiceParams (v);
        req.samples[(size_t) v] = proc.sampleSlot (v).share();
    }
    req.params.global = proc.readGlobalParams();
    req.movement = kit.movementStore().get();
    req.bank = proc.patterns().get();
    req.sampleRate = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
    req.bpm = kit.isFollowingHost() ? kit.getHostBpm() : (double) req.params.global[gp::SeqTempo];
    return req;
}
} // namespace

bool BatidaProcessor::resampleSound (int source, int target, juce::String* error)
{
    auto req = renderRequestFor (*this, kit);
    req.mode = RenderRequest::Mode::Sound;
    req.voice = source;
    for (int v = 0; v < kNumVoices; ++v) // the sound alone, even if others are soloed
    {
        req.params.voices[(size_t) v][vp::Mute] = v == source ? 0.0f : 1.0f;
        req.params.voices[(size_t) v][vp::Solo] = 0.0f;
    }
    const auto audio = renderKit (req);
    return loadRendered (audio, target, getVoiceName (source) + " RS", false, error);
}

bool BatidaProcessor::resamplePattern (int pattern, int target, juce::String* error)
{
    auto req = renderRequestFor (*this, kit);
    req.mode = RenderRequest::Mode::Pattern;
    req.pattern = pattern;
    if (req.bank.patterns[(size_t) pattern].isEmpty())
    {
        if (error != nullptr)
            *error = getPatternName (pattern) + " is empty";
        return false;
    }
    const auto audio = renderKit (req);
    return loadRendered (audio, target, getPatternName (pattern) + " Loop", true, error);
}

juce::File BatidaProcessor::exportPatternMidi (int pattern, juce::String* error)
{
    MidiExportOptions options;
    const auto g = readGlobalParams();
    options.swing = g[gp::SeqSwing];
    options.padX = g[gp::XyX];
    options.padY = g[gp::XyY];
    const auto midi = patternToMidi (kit.patternStore().get(), pattern, options);

    const auto folder = library().folderFor (PresetType::Sound, false).getSiblingFile ("MIDI");
    folder.createDirectory();
    const auto name = "P" + juce::String (pattern + 1).paddedLeft ('0', 2) + " " + getPatternName (pattern);
    const auto file = folder.getChildFile (safeFileName (name) + ".mid");
    file.deleteFile(); // the same pattern exported again replaces its file
    std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());
    if (out == nullptr || ! midi.writeTo (*out))
    {
        if (error != nullptr)
            *error = "Couldn't write " + file.getFullPathName();
        return {};
    }
    return file;
}

// Library --------------------------------------------------------------------------

void BatidaProcessor::beginLoadStep (const juce::String& key, LoadMode mode)
{
    clearPreviews(); // sounds or patterns are about to change under any suggestion
    // Browsing: the step taken before the first load of the run covers the rest.
    if (mode == LoadMode::Browse && key == browseKey && historyStamp() == browseStamp)
        return;
    beginUndoStep (true, true);
}

void BatidaProcessor::endLoadStep (const juce::String& key, LoadMode mode)
{
    ++actionCounter;
    ++originsVersion;
    browseKey = mode == LoadMode::Browse ? key : juce::String();
    browseStamp = historyStamp();
}

void BatidaProcessor::writeGlobalParameter (int g, float value)
{
    auto* p = state.getParameter (globalParamID (g));
    const auto norm = p->convertTo0to1 (value);
    if (std::abs (p->getValue() - norm) > 1.0e-6f)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

void BatidaProcessor::setNameQuietly (int voice, const juce::String& name)
{
    auto clean = name.trim().substring (0, 24);
    if (clean == defaultVoiceName (voice))
        clean = {};
    voiceNames[(size_t) voice] = clean;
    ++namesVersion;
}

SampleRef BatidaProcessor::sampleRefFor (int voice) const
{
    const auto path = const_cast<Kit&> (kit).sampleSlot (voice).getPath();
    if (path.isEmpty())
        return {};
    const juce::File f (path);
    return { path, f.existsAsFile() ? f.getSize() : sampleBytes[(size_t) voice] };
}

BatidaProcessor::PendingSample BatidaProcessor::prepareSample (const SampleRef& ref)
{
    PendingSample p;
    p.path = ref.path;
    p.bytes = ref.bytes;
    if (ref.isEmpty())
        return p;
    auto file = juce::File (ref.path);
    if (! file.existsAsFile())
        file = findSample (file.getFileName(), ref.bytes, library().searchFolders(), 20000);
    if (! file.existsAsFile())
    {
        p.kind = PendingSample::Missing;
        return p;
    }
    p.data = SampleSlot::decode (file, formats, p.note);
    p.kind = p.data != nullptr ? PendingSample::Loaded : PendingSample::Unreadable;
    if (p.data != nullptr)
        p.bytes = file.getSize();
    return p;
}

void BatidaProcessor::commitSample (int voice, PendingSample&& p)
{
    auto& slot = kit.sampleSlot (voice);
    sampleBytes[(size_t) voice] = p.bytes;
    switch (p.kind)
    {
        case PendingSample::Empty:      slot.clear(); break;
        case PendingSample::Loaded:     slot.setDecoded (std::move (p.data), p.note); break;
        case PendingSample::Missing:    slot.markMissing (p.path); break;
        case PendingSample::Unreadable: slot.markUnreadable (p.path); break; // never the previous sample
    }
}

SoundPreset BatidaProcessor::captureSound (int voice) const
{
    SoundPreset s;
    s.info.name = getVoiceName (voice);
    s.info.author = library().getAuthor();
    s.info.category = guessCategory (s.info.name);
    s.params = readVoiceParams (voice);
    s.sample = sampleRefFor (voice);
    return s;
}

KitPreset BatidaProcessor::captureKit() const
{
    KitPreset k;
    k.info.name = origins.kitName;
    k.info.author = library().getAuthor();
    for (int v = 0; v < kNumVoices; ++v)
    {
        k.voices[(size_t) v] = readVoiceParams (v);
        k.names[(size_t) v] = getVoiceName (v);
        k.samples[(size_t) v] = sampleRefFor (v);
    }
    k.globals = readGlobalParams();
    k.movement = kit.movementStore().get();
    return k;
}

PatternPreset BatidaProcessor::capturePattern (int pattern) const
{
    PatternPreset p;
    p.info.name = getPatternName (pattern);
    p.info.author = library().getAuthor();
    p.pattern = const_cast<Kit&> (kit).patternStore().get().patterns[(size_t) pattern];
    return p;
}

SetPreset BatidaProcessor::captureSet() const
{
    SetPreset s;
    s.kit = captureKit();
    s.info = s.kit.info;
    s.info.name = origins.setName.isNotEmpty() ? origins.setName : origins.kitName;
    s.patterns = const_cast<Kit&> (kit).patternStore().get();
    return s;
}

juce::String BatidaProcessor::getPatternName (int pattern) const
{
    const auto& n = origins.patternNames[(size_t) pattern];
    return n.isNotEmpty() ? n : "Pattern " + juce::String (pattern + 1);
}

void BatidaProcessor::applySound (int voice, const SoundPreset& sound, const juce::File& from, LoadMode mode)
{
    const auto key = "sound" + juce::String (voice);
    beginLoadStep (key, mode);
    auto sample = prepareSample (sound.sample); // decoded before the old sound starts fading
    kit.beginSwitch (1u << voice);

    auto values = sound.params;
    const auto live = readVoiceParams (voice);
    for (const auto k : { vp::Pan, vp::Mute, vp::Solo })
        values[k] = live[k];
    commitSample (voice, std::move (sample));
    writeVoiceParameters (voice, values);
    setNameQuietly (voice, sound.info.name);
    origins.soundFiles[(size_t) voice] = from;
    endLoadStep (key, mode);
}

void BatidaProcessor::applyKit (const KitPreset& k, const juce::File& from, LoadMode mode)
{
    beginLoadStep ("kit", mode);
    std::array<PendingSample, kNumVoices> samples;
    for (int v = 0; v < kNumVoices; ++v)
        samples[(size_t) v] = prepareSample (k.samples[(size_t) v]);
    kit.beginSwitch (0xffu);
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto values = k.voices[(size_t) v];
        const auto live = readVoiceParams (v);
        values[vp::Mute] = live[vp::Mute];
        values[vp::Solo] = live[vp::Solo];
        commitSample (v, std::move (samples[(size_t) v]));
        writeVoiceParameters (v, values);
        setNameQuietly (v, k.names[(size_t) v]);
    }
    for (int g = 0; g < kNumGlobalParams; ++g)
        if (isKitGlobal (g))
            writeGlobalParameter (g, k.globals[(size_t) g]);
    kit.movementStore().replace (k.movement);

    origins.kitName = k.info.name.isNotEmpty() ? k.info.name : juce::String ("Kit");
    origins.kitFile = from;
    origins.soundFiles = {};
    endLoadStep ("kit", mode);
}

void BatidaProcessor::applyPattern (int pattern, const PatternPreset& preset, const juce::File& from, LoadMode mode)
{
    const auto key = "pattern" + juce::String (pattern);
    beginLoadStep (key, mode);
    kit.patternStore().edit ([&] (PatternBank& bank) { bank.patterns[(size_t) pattern] = preset.pattern; });
    origins.patternNames[(size_t) pattern] = preset.info.name;
    origins.patternFiles[(size_t) pattern] = from;
    endLoadStep (key, mode);
}

void BatidaProcessor::applySet (const SetPreset& set, const juce::File& from, LoadMode mode)
{
    beginLoadStep ("set", mode);
    std::array<PendingSample, kNumVoices> samples;
    for (int v = 0; v < kNumVoices; ++v)
        samples[(size_t) v] = prepareSample (set.kit.samples[(size_t) v]);
    kit.beginSwitch (0xffu);
    for (int v = 0; v < kNumVoices; ++v)
    {
        commitSample (v, std::move (samples[(size_t) v]));
        writeVoiceParameters (v, set.kit.voices[(size_t) v]);
        setNameQuietly (v, set.kit.names[(size_t) v]);
    }
    for (int g = 0; g < kNumGlobalParams; ++g)
        writeGlobalParameter (g, set.kit.globals[(size_t) g]);
    kit.movementStore().replace (set.kit.movement);
    kit.patternStore().replace (set.patterns);

    origins = {};
    origins.setName = set.info.name;
    origins.kitName = set.info.name.isNotEmpty() ? set.info.name : juce::String ("Neutral");
    origins.setFile = from;
    endLoadStep ("set", mode);
}

bool BatidaProcessor::loadPresetFile (const juce::File& file, int voice, int pattern, LoadMode mode, juce::String* error)
{
    const auto type = presetTypeOf (file);
    if (! type)
    {
        if (error != nullptr)
            *error = file.getFileName() + " isn't a Batida file";
        return false;
    }
    switch (*type)
    {
        case PresetType::Sound:
            if (const auto s = readSound (file, error)) { applySound (voice, *s, file, mode); return true; }
            break;
        case PresetType::Kit:
            if (const auto k = readKit (file, error)) { applyKit (*k, file, mode); return true; }
            break;
        case PresetType::Pattern:
            if (const auto p = readPattern (file, error)) { applyPattern (pattern, *p, file, mode); return true; }
            break;
        case PresetType::Set:
            if (const auto s = readSet (file, error)) { applySet (*s, file, mode); return true; }
            break;
    }
    return false;
}

bool BatidaProcessor::browseSample (int voice, const juce::File& file)
{
    const auto key = "sample" + juce::String (voice);
    beginLoadStep (key, LoadMode::Browse);
    const auto ok = decodeAndSwitch (voice, file);
    endLoadStep (key, LoadMode::Browse);
    return ok;
}

void BatidaProcessor::initSound (int voice)
{
    applySound (voice, initSoundPreset(), {});
}

void BatidaProcessor::initKit()
{
    applyKit (defaultKitPreset(), {});
}

void BatidaProcessor::initPattern (int pattern)
{
    applyPattern (pattern, {}, {});
}

void BatidaProcessor::initAll()
{
    SetPreset set;
    set.kit = defaultKitPreset();
    set.info.name = set.kit.info.name;
    set.patterns = defaultPatternBank();
    applySet (set, {});
    origins = {};
    ++originsVersion;
}

bool BatidaProcessor::collectInto (SampleRef& ref, const juce::File& presetFile, const juce::String& name, juce::String* error)
{
    if (ref.isEmpty())
        return true;
    const juce::File sample (ref.path);
    if (! sample.existsAsFile())
        return true; // a missing sample stays a reference
    const auto copy = collectSample (sample, presetFile.getParentDirectory().getChildFile (safeFileName (name) + " Samples"));
    if (copy == juce::File())
    {
        if (error != nullptr)
            *error = "Couldn't copy " + sample.getFileName();
        return false;
    }
    ref.path = copy.getFullPathName();
    return true;
}

void BatidaProcessor::presetSaved (const juce::File& file)
{
    library().addRelinkFolder (file.getParentDirectory());
    library().refresh();
    ++originsVersion;
}

bool BatidaProcessor::saveSound (int voice, const juce::File& file, const PresetInfo& info, bool collect, juce::String* error)
{
    auto s = captureSound (voice);
    s.info = info;
    if (collect && ! collectInto (s.sample, file, info.name, error))
        return false;
    if (! writeSound (file, s, error))
        return false;
    if (s.info.name != getVoiceName (voice))
        beginUndoStep();
    setNameQuietly (voice, s.info.name);
    origins.soundFiles[(size_t) voice] = file;
    presetSaved (file);
    return true;
}

bool BatidaProcessor::saveKit (const juce::File& file, const PresetInfo& info, bool collect, juce::String* error)
{
    auto k = captureKit();
    k.info = info;
    for (int v = 0; v < kNumVoices; ++v)
        if (collect && ! collectInto (k.samples[(size_t) v], file, info.name, error))
            return false;
    if (! writeKit (file, k, error))
        return false;
    origins.kitName = info.name;
    origins.kitFile = file;
    presetSaved (file);
    return true;
}

bool BatidaProcessor::savePattern (int pattern, const juce::File& file, const PresetInfo& info, juce::String* error)
{
    auto p = capturePattern (pattern);
    p.info = info;
    if (! writePattern (file, p, error))
        return false;
    origins.patternNames[(size_t) pattern] = info.name;
    origins.patternFiles[(size_t) pattern] = file;
    presetSaved (file);
    return true;
}

bool BatidaProcessor::saveSet (const juce::File& file, const PresetInfo& info, bool collect, juce::String* error)
{
    auto s = captureSet();
    s.info = info;
    for (int v = 0; v < kNumVoices; ++v)
        if (collect && ! collectInto (s.kit.samples[(size_t) v], file, info.name, error))
            return false;
    if (! writeSet (file, s, error))
        return false;
    origins.setName = info.name;
    origins.setFile = file;
    presetSaved (file);
    return true;
}

std::unique_ptr<juce::XmlElement> BatidaProcessor::originsToXml() const
{
    auto e = std::make_unique<juce::XmlElement> (kLibraryTag);
    e->setAttribute ("kit", origins.kitName);
    e->setAttribute ("kitFile", origins.kitFile.getFullPathName());
    e->setAttribute ("set", origins.setName);
    e->setAttribute ("setFile", origins.setFile.getFullPathName());
    for (int v = 0; v < kNumVoices; ++v)
        if (origins.soundFiles[(size_t) v] != juce::File())
        {
            auto* se = e->createNewChildElement ("SOUND");
            se->setAttribute ("voice", v);
            se->setAttribute ("file", origins.soundFiles[(size_t) v].getFullPathName());
        }
    for (int p = 0; p < kNumPatterns; ++p)
        if (origins.patternNames[(size_t) p].isNotEmpty() || origins.patternFiles[(size_t) p] != juce::File())
        {
            auto* pe = e->createNewChildElement ("PATTERN");
            pe->setAttribute ("index", p);
            pe->setAttribute ("name", origins.patternNames[(size_t) p]);
            pe->setAttribute ("file", origins.patternFiles[(size_t) p].getFullPathName());
        }
    return e;
}

void BatidaProcessor::originsFromXml (const juce::XmlElement* e)
{
    origins = {};
    ++originsVersion;
    if (e == nullptr)
        return;
    auto file = [] (const juce::String& path) { return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File(); };
    origins.kitName = e->getStringAttribute ("kit", "Neutral");
    origins.kitFile = file (e->getStringAttribute ("kitFile"));
    origins.setName = e->getStringAttribute ("set");
    origins.setFile = file (e->getStringAttribute ("setFile"));
    for (auto* se : e->getChildWithTagNameIterator ("SOUND"))
        if (const auto v = se->getIntAttribute ("voice", -1); v >= 0 && v < kNumVoices)
            origins.soundFiles[(size_t) v] = file (se->getStringAttribute ("file"));
    for (auto* pe : e->getChildWithTagNameIterator ("PATTERN"))
        if (const auto p = pe->getIntAttribute ("index", -1); p >= 0 && p < kNumPatterns)
        {
            origins.patternNames[(size_t) p] = pe->getStringAttribute ("name");
            origins.patternFiles[(size_t) p] = file (pe->getStringAttribute ("file"));
        }
}

// Missing samples ---------------------------------------------------------------

int BatidaProcessor::numMissingSamples() const
{
    int n = 0;
    for (int v = 0; v < kNumVoices; ++v)
        n += const_cast<Kit&> (kit).sampleSlot (v).getStatus() == SampleSlot::Status::Missing ? 1 : 0;
    return n;
}

bool BatidaProcessor::relinkSample (int voice, const juce::File& file)
{
    return relinkFound ({ { voice, file } }) == 1;
}

int BatidaProcessor::relinkFound (const std::vector<std::pair<int, juce::File>>& found)
{
    if (found.empty())
        return 0;
    beginLoadStep ("relink", LoadMode::Step);
    int n = 0;
    uint32_t mask = 0;
    SampleCache cache;
    std::vector<std::shared_ptr<const SampleData>> decoded;
    for (const auto& [v, f] : found) // decoded first, one copy per file
    {
        mask |= 1u << v;
        decoded.push_back (sharedSample (f.getFullPathName(), cache));
    }
    kit.beginSwitch (mask);
    for (size_t i = 0; i < found.size(); ++i)
        if (const auto& [v, f] = found[i]; decoded[i] != nullptr)
        {
            kit.sampleSlot (v).setShared (decoded[i]);
            sampleBytes[(size_t) v] = f.getSize();
            library().addRelinkFolder (f.getParentDirectory());
            ++n;
        }
    endLoadStep ("relink", LoadMode::Step);
    return n;
}

int BatidaProcessor::autoRelink()
{
    std::vector<std::pair<int, juce::File>> found;
    const auto folders = library().searchFolders();
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& slot = kit.sampleSlot (v);
        if (slot.getStatus() != SampleSlot::Status::Missing)
            continue;
        const auto f = findSample (juce::File (slot.getPath()).getFileName(), sampleBytes[(size_t) v], folders, 20000);
        if (f.existsAsFile())
            found.push_back ({ v, f });
    }
    return relinkFound (found);
}

bool BatidaProcessor::collectSamples (const juce::File& folder, juce::String* error)
{
    std::vector<std::pair<int, juce::File>> copies;
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& slot = kit.sampleSlot (v);
        if (slot.getStatus() != SampleSlot::Status::Loaded)
            continue;
        const auto copy = collectSample (juce::File (slot.getPath()), folder);
        if (copy == juce::File())
        {
            if (error != nullptr)
                *error = "Couldn't copy " + juce::File (slot.getPath()).getFileName() + " into " + folder.getFullPathName();
            return false;
        }
        if (copy.getFullPathName() != slot.getPath())
            copies.push_back ({ v, copy });
    }
    relinkFound (copies);
    return true;
}

void BatidaProcessor::setPatternParameter (int pattern)
{
    auto* p = state.getParameter (globalParamID (gp::SeqPattern));
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) pattern));
    p->endChangeGesture();
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
    {
        keys->beginChangeGesture();
        keys->setValueNotifyingHost (value);
        keys->endChangeGesture();
    }
}

std::array<float, kNumGlobalParams> BatidaProcessor::readGlobalParams() const
{
    std::array<float, kNumGlobalParams> g {};
    for (int i = 0; i < kNumGlobalParams; ++i)
        g[(size_t) i] = globalRaw[(size_t) i]->load (std::memory_order_relaxed);
    return g;
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
    beginLoadStep ("sample" + juce::String (voice), LoadMode::Step);
    const auto ok = decodeAndSwitch (voice, file);
    endLoadStep ("sample" + juce::String (voice), LoadMode::Step);
    return ok;
}

bool BatidaProcessor::decodeAndSwitch (int voice, const juce::File& file)
{
    // A file that can't be read leaves the slot as it was, with the reason in
    // its error (the drop or click simply didn't take).
    juce::String note;
    auto data = SampleSlot::decode (file, formats, note);
    auto& slot = kit.sampleSlot (voice);
    if (data == nullptr)
        return slot.load (file, formats); // records why, or that the file is gone

    kit.beginSwitch (1u << voice);
    slot.setDecoded (std::move (data), note);
    sampleBytes[(size_t) voice] = file.getSize();

    // A sound on FM switches to Sample, so the new sample is heard straight away.
    auto* mode = state.getParameter (voiceParamID (voice, vp::SrcMode));
    if ((SourceMode) juce::roundToInt (mode->convertFrom0to1 (mode->getValue())) == SourceMode::FM)
    {
        mode->beginChangeGesture();
        mode->setValueNotifyingHost (mode->convertTo0to1 ((float) SourceMode::Sample));
        mode->endChangeGesture();
    }
    return true;
}

void BatidaProcessor::clearSample (int voice)
{
    beginLoadStep ("clear" + juce::String (voice), LoadMode::Step);
    kit.beginSwitch (1u << voice);
    kit.sampleSlot (voice).clear();
    sampleBytes[(size_t) voice] = -1;
    endLoadStep ("clear" + juce::String (voice), LoadMode::Step);
}

void BatidaProcessor::timerCallback()
{
    for (int v = 0; v < kNumVoices; ++v)
        kit.sampleSlot (v).collectGarbage();
    kit.patternStore().collectGarbage();
    kit.movementStore().collectGarbage();
    kit.patternPreview().collectGarbage();
    preview.collectGarbage();
}

void BatidaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.removeChild (tree.getChildWithName (kSamplesTag), nullptr);
    tree.removeChild (tree.getChildWithName (kPatternsTag), nullptr);
    if (const auto patternXml = kit.patternStore().get().toXml())
        tree.appendChild (juce::ValueTree::fromXml (*patternXml), nullptr);

    tree.removeChild (tree.getChildWithName (kMovementTag), nullptr);
    if (const auto movementXml = kit.movementStore().get().toXml())
        tree.appendChild (juce::ValueTree::fromXml (*movementXml), nullptr);

    tree.removeChild (tree.getChildWithName (kNamesTag), nullptr);
    juce::ValueTree names (kNamesTag);
    for (int v = 0; v < kNumVoices; ++v)
        if (voiceNames[(size_t) v].isNotEmpty())
            names.appendChild (juce::ValueTree (kNameTag, { { kVoiceAttr, v }, { kTextAttr, voiceNames[(size_t) v] } }), nullptr);
    tree.appendChild (names, nullptr);

    juce::ValueTree samples (kSamplesTag);
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto path = kit.sampleSlot (v).getPath();
        if (path.isNotEmpty())
            samples.appendChild (juce::ValueTree (kSampleTag, { { kVoiceAttr, v }, { kPathAttr, path },
                                                                { kBytesAttr, juce::String (sampleBytes[(size_t) v]) } }),
                                 nullptr);
    }
    tree.appendChild (samples, nullptr);

    tree.removeChild (tree.getChildWithName (kLibraryTag), nullptr);
    if (const auto lib = originsToXml())
        tree.appendChild (juce::ValueTree::fromXml (*lib), nullptr);

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

    // The project's samples decode before anything switches (one copy per file).
    std::array<juce::String, kNumVoices> paths;
    std::array<juce::int64, kNumVoices> bytes;
    bytes.fill (-1);
    for (const auto& sv : samples)
    {
        const auto v = (int) sv.getProperty (kVoiceAttr, -1);
        if (v >= 0 && v < kNumVoices)
        {
            paths[(size_t) v] = sv.getProperty (kPathAttr).toString();
            bytes[(size_t) v] = sv.getProperty (kBytesAttr, "-1").toString().getLargeIntValue();
        }
    }
    std::array<std::shared_ptr<const SampleData>, kNumVoices> decoded;
    {
        SampleCache cache;
        for (int v = 0; v < kNumVoices; ++v)
            if (paths[(size_t) v].isNotEmpty())
                decoded[(size_t) v] = sharedSample (paths[(size_t) v], cache);
    }

    kit.beginSwitch (0xffu); // sounding voices fade before the project's sounds replace them
    clearPreviews();
    const auto patternTree = tree.getChildWithName (kPatternsTag);
    tree.removeChild (patternTree, nullptr);
    const auto namesTree = tree.getChildWithName (kNamesTag);
    tree.removeChild (namesTree, nullptr);
    const auto movementTree = tree.getChildWithName (kMovementTag);
    tree.removeChild (movementTree, nullptr);
    const auto libraryTree = tree.getChildWithName (kLibraryTag);
    tree.removeChild (libraryTree, nullptr);
    if (const auto xml = libraryTree.isValid() ? libraryTree.createXml() : nullptr)
        originsFromXml (xml.get());
    else
        originsFromXml (nullptr);
    // Projects from before phase 4 have no movement, and ones from before
    // phase 3 no patterns: they get the defaults, not this instance's last ones.
    {
        MovementData m;
        if (const auto mx = movementTree.isValid() ? movementTree.createXml() : nullptr)
            m.fromXml (*mx);
        else
            m = defaultMovement();
        kit.movementStore().replace (m);
    }
    // Projects from before 0.8.0 have no choke groups: they load with none, so
    // they sound as they did (the default kit's hats would otherwise choke).
    const auto hasChoke = tree.getChildWithProperty ("id", juce::String (voiceParamID (0, vp::Choke))).isValid();
    state.replaceState (tree);
    if (! hasChoke)
        for (int v = 0; v < kNumVoices; ++v)
            state.getParameter (voiceParamID (v, vp::Choke))->setValueNotifyingHost (0.0f);

    for (auto& n : voiceNames)
        n = {};
    for (const auto& n : namesTree)
    {
        const auto v = (int) n.getProperty (kVoiceAttr, -1);
        if (v >= 0 && v < kNumVoices)
            voiceNames[(size_t) v] = n.getProperty (kTextAttr).toString();
    }
    ++namesVersion;

    {
        PatternBank bank;
        if (const auto px = patternTree.isValid() ? patternTree.createXml() : nullptr)
            bank.fromXml (*px);
        else
            bank = defaultPatternBank();
        kit.patternStore().replace (bank);
    }

    sampleBytes = bytes;
    for (int v = 0; v < kNumVoices; ++v)
        placeSample (v, paths[(size_t) v], decoded[(size_t) v]);

    // Samples that moved: look in the library and the folders relinked from before.
    if (numMissingSamples() > 0)
        autoRelink();
    history = History(); // a project that opens starts a fresh history
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BatidaProcessor();
}
