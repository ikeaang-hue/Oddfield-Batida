#include "PluginProcessor.h"

#include "Engine/ParamRange.h"

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
            "voice" + juce::String (v + 1), "Voice " + juce::String (v + 1), " | ");

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
        params.voices[(size_t) p->voice] = p->params;

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
            sidechainCopy.copyFrom (ch, 0, in, std::min (ch, in.getNumChannels() - 1), 0, n);
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

    kit.process (buffer, midi, sidechain, transport);
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

    // Samples: each slot reloads the other's file (or its missing path).
    struct Held { SampleSlot::Status status; juce::String path; };
    const Held ha { kit.sampleSlot (a).getStatus(), kit.sampleSlot (a).getPath() };
    const Held hb { kit.sampleSlot (b).getStatus(), kit.sampleSlot (b).getPath() };
    auto takeOver = [this] (int voice, const Held& h)
    {
        auto& slot = kit.sampleSlot (voice);
        if (h.status == SampleSlot::Status::Loaded && slot.load (juce::File (h.path), formats))
            return;
        if (h.path.isNotEmpty())
            slot.markMissing (h.path);
        else
            slot.clear();
    };
    takeOver (a, hb);
    takeOver (b, ha);

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
    kit.patternStore().replace (s.patterns);
    voiceNames = s.names;
    ++namesVersion;

    if (s.parameters)
    {
        const auto& params = getParameters();
        for (size_t i = 0; i < params.size() && i < s.parameters->size(); ++i)
            if (params[(int) i]->getValue() != (*s.parameters)[i])
            {
                params[(int) i]->beginChangeGesture();
                params[(int) i]->setValueNotifyingHost ((*s.parameters)[i]);
                params[(int) i]->endChangeGesture();
            }
    }
    if (s.samples)
        for (int v = 0; v < kNumVoices; ++v)
        {
            auto& slot = kit.sampleSlot (v);
            const auto& path = (*s.samples)[(size_t) v];
            if (path == slot.getPath())
                continue;
            if (path.isEmpty())
                slot.clear();
            else if (! slot.load (juce::File (path), formats))
                slot.markMissing (path);
        }
    ++actionCounter;
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
    req.sample = kit.sampleSlot (voice).getDisplayData();
    req.amount = amount;
    req.direction = direction;
    req.lockSource = lockSource;
    req.lockFx = lockFx;
    req.lockEnvelopes = lockEnvelopes;
    req.seed = (uint32_t) juce::Random::getSystemRandom().nextInt();

    juce::WeakReference<BatidaProcessor> safe (this);
    varyPool.addJob ([safe, req, voice]
    {
        auto result = vary (req);
        juce::MessageManager::callAsync ([safe, result = std::move (result), voice, base = req.base]() mutable
        {
            if (auto* self = safe.get())
            {
                self->candidates = std::move (result);
                self->candidatesBase = base;
                self->candidatesVoice = voice;
                self->previewIndex = -1;
                self->varyBusy = false;
                ++self->varyVersion;
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

void BatidaProcessor::previewCandidate (int index)
{
    previewIndex = index >= 0 && index < (int) candidates.size() ? index : -1;
    Preview p;
    if (previewIndex >= 0)
    {
        p.active = true;
        p.voice = candidatesVoice;
        p.params = withLiveMix (candidates[(size_t) previewIndex], readVoiceParams (candidatesVoice));
    }
    preview.replace (p);
    ++varyVersion;
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
    ++varyVersion;
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
        keys->setValueNotifyingHost (value);
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
    kit.patternStore().collectGarbage();
    kit.movementStore().collectGarbage();
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
    const auto patternTree = tree.getChildWithName (kPatternsTag);
    tree.removeChild (patternTree, nullptr);
    const auto namesTree = tree.getChildWithName (kNamesTag);
    tree.removeChild (namesTree, nullptr);
    const auto movementTree = tree.getChildWithName (kMovementTag);
    tree.removeChild (movementTree, nullptr);
    if (movementTree.isValid())
        if (const auto xml = movementTree.createXml())
        {
            MovementData m;
            m.fromXml (*xml);
            kit.movementStore().replace (m);
        }
    state.replaceState (tree);

    for (auto& n : voiceNames)
        n = {};
    for (const auto& n : namesTree)
    {
        const auto v = (int) n.getProperty (kVoiceAttr, -1);
        if (v >= 0 && v < kNumVoices)
            voiceNames[(size_t) v] = n.getProperty (kTextAttr).toString();
    }
    ++namesVersion;

    // Projects from before phase 3 have no patterns: keep the defaults.
    if (patternTree.isValid())
        if (const auto patternXml = patternTree.createXml())
        {
            PatternBank bank;
            bank.fromXml (*patternXml);
            kit.patternStore().replace (bank);
        }

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
