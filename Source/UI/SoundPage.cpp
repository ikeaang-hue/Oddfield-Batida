#include "SoundPage.h"

#include "Engine/FmSource.h"
#include "Engine/SampleSource.h"

using namespace batida;
using namespace theme;

// SampleView -----------------------------------------------------------------------

juce::RangedAudioParameter* SampleView::paramFor (int handle) const
{
    return proc.getState().getParameter (voiceParamID (voice, handle == 0 ? vp::SmpStart : vp::SmpEnd));
}

int SampleView::handleAt (float x) const
{
    const auto p = proc.readVoiceParams (voice);
    const auto w = (float) getWidth();
    const auto ds = std::abs (x - p[vp::SmpStart] * w), de = std::abs (x - p[vp::SmpEnd] * w);
    if (std::min (ds, de) > 7.0f)
        return -1;
    return ds <= de ? 0 : 1;
}

void SampleView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.fillAll (bg);
    g.setColour (line);
    g.drawRect (r, 1.0f);

    auto& slot = proc.sampleSlot (voice);
    const auto* data = slot.getDisplayData();
    if (data == nullptr || data->peaks.empty())
    {
        g.setColour (slot.getStatus() == SampleSlot::Status::Missing ? ink : faint);
        g.setFont (mono (9.5f));
        g.drawText (slot.getStatus() == SampleSlot::Status::Missing ? "MISSING: " + juce::File (slot.getPath()).getFileName() : juce::String ("NO SAMPLE"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto p = proc.readVoiceParams (voice);
    const auto xAt = [&] (float norm) { return r.getX() + norm * r.getWidth(); };
    const auto start = std::min (p[vp::SmpStart], p[vp::SmpEnd]);
    const auto end = std::max (p[vp::SmpStart], p[vp::SmpEnd]);

    // Peaks
    const auto bins = (int) data->peaks.size();
    const auto mid = r.getCentreY();
    const auto half = r.getHeight() * 0.5f - 3.0f;
    for (int px = 1; px < (int) r.getWidth() - 1; px += 2)
    {
        const auto from = px * bins / (int) r.getWidth(), to = std::max (from + 1, (px + 2) * bins / (int) r.getWidth());
        float lo = 0.0f, hi = 0.0f;
        for (int b = from; b < std::min (to, bins); ++b)
        {
            lo = std::min (lo, data->peaks[(size_t) b].first);
            hi = std::max (hi, data->peaks[(size_t) b].second);
        }
        g.setColour (ink.withAlpha (0.85f));
        g.fillRect ((float) px, mid - hi * half, 1.0f, std::max (1.0f, (hi - lo) * half));
    }

    // Slices, numbered as the SEQ Slice lane counts them.
    SliceSpec spec;
    spec.mode = (SliceMode) juce::jlimit (0, 2, p.choice (vp::SmpSliceMode));
    spec.gridCount = juce::roundToInt (p[vp::SmpSlices]);
    spec.sensitivity = p[vp::SmpSliceSens];
    if (spec.mode != SliceMode::Off)
    {
        const auto frames = (double) data->numFrames();
        const auto count = sliceCount (*data, spec, start * frames, end * frames);
        const float dash[] = { 2.0f, 2.0f };
        g.setFont (mono (8.5f));
        for (int k = 0; k < count; ++k)
        {
            const auto x = xAt ((float) (sliceRegion (*data, spec, start * frames, end * frames, k).first / frames));
            if (k > 0)
            {
                g.setColour (juce::Colour (0xff5a5a5a));
                g.drawDashedLine ({ x, r.getY(), x, r.getBottom() }, dash, 2);
            }
            g.setColour (muted);
            g.drawText (juce::String (k + 1), juce::Rectangle<float> (x + 3.0f, r.getY() + 2.0f, 20.0f, 10.0f), juce::Justification::topLeft);
        }
    }

    // Loop points (Gate mode loops between them).
    if (p.flag (vp::SmpLoop))
    {
        const float dash[] = { 3.0f, 2.0f };
        g.setColour (ink.withAlpha (0.7f));
        for (const auto v : { p[vp::SmpLoopStart], p[vp::SmpLoopEnd] })
            g.drawDashedLine ({ xAt (v), r.getY(), xAt (v), r.getBottom() }, dash, 2, 1.0f);
    }

    // Fades
    const auto seconds = (float) (data->numFrames() / data->sampleRate);
    const auto regionMs = std::max (1.0f, (end - start) * seconds * 1000.0f);
    if (p[vp::SmpFadeIn] > 0.5f)
    {
        const auto fx = xAt (start + (end - start) * std::min (1.0f, p[vp::SmpFadeIn] / regionMs));
        const float dash[] = { 3.0f, 2.0f };
        g.setColour (ink);
        g.drawDashedLine ({ xAt (start), r.getBottom() - 2.0f, fx, r.getY() + 2.0f }, dash, 2);
    }
    if (p[vp::SmpFadeOut] > 0.5f)
    {
        const auto fx = xAt (end - (end - start) * std::min (1.0f, p[vp::SmpFadeOut] / regionMs));
        const float dash[] = { 3.0f, 2.0f };
        g.setColour (ink);
        g.drawDashedLine ({ fx, r.getY() + 2.0f, xAt (end), r.getBottom() - 2.0f }, dash, 2);
    }

    // Outside start/end: dimmed. The handles are squares at the top.
    g.setColour (bg.withAlpha (0.7f));
    g.fillRect (r.withRight (xAt (start)));
    g.fillRect (r.withLeft (xAt (end)));
    g.setColour (ink);
    g.fillRect (xAt (start), r.getY(), 1.5f, r.getHeight());
    g.fillRect (xAt (end) - 1.5f, r.getY(), 1.5f, r.getHeight());
    g.fillRect (xAt (start), r.getY(), 7.0f, 7.0f);
    g.fillRect (xAt (end) - 7.0f, r.getY(), 7.0f, 7.0f);
}

void SampleView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (handleAt (e.position.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void SampleView::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position.x);
    if (dragging < 0)
        return;
    auto* param = paramFor (dragging);
    dragStart = param->getValue();
    param->beginChangeGesture();
}

void SampleView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
    paramFor (dragging)->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStart + fine * (float) e.getDistanceFromDragStartX() / (float) getWidth()));
    repaint();
}

void SampleView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        paramFor (dragging)->endChangeGesture();
    dragging = -1;
}

// OpCard ---------------------------------------------------------------------------

OpCard::OpCard (BatidaProcessor& p, int o) : op (o), ratio (p.getState(), "Ratio"), level (p.getState(), "Level")
{
    for (auto* c : { &ratio, &level })
    {
        c->numberOnly();
        c->withLabelWidth (40);
        addAndMakeVisible (c);
    }
    wave.setInterceptsMouseClicks (false, false);
    env.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (wave);
    addAndMakeVisible (env);
    bindVoice (0);
}

void OpCard::bindVoice (int voice)
{
    ratio.bind (voiceParamID (voice, opParam (op, Ratio)));
    level.bind (voiceParamID (voice, opParam (op, OpLevel)));
}

void OpCard::update (const VoiceParams& p, const FmOpEffective& e)
{
    auto marker = [] (float effective, float base) -> std::optional<double>
    {
        if (std::abs (effective - base) <= 1.0e-4f * std::max (1.0f, std::abs (base)))
            return std::nullopt;
        return (double) effective;
    };
    ratio.setMarker (marker (e.ratio, p.op (op, Ratio)));
    level.setMarker (marker (e.level, p.op (op, OpLevel)));
    wave.set (p.op (op, Wave));
    env.set (e.attack, e.decay, e.sustain, e.release, e.level <= 0.0f);
    const auto text = waveName (p.op (op, Wave));
    if (e.carrier != carrier || (e.level > 0.0f) != active || text != waveText)
    {
        carrier = e.carrier;
        active = e.level > 0.0f;
        waveText = text;
        setAlpha (active ? 1.0f : 0.55f);
        repaint();
    }
}

void OpCard::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds();
    g.setColour (isMouseOver (true) ? faint : line);
    g.drawRect (r, 1);
    auto head = r.reduced (6, 5).withHeight (14);
    g.setColour (ink);
    g.setFont (mono (10.5f, Weight::Bold));
    g.drawText ("OP" + juce::String (op + 1), head, juce::Justification::centredLeft, false);
    drawLabel (g, carrier ? "carrier" : "mod", head, muted, juce::Justification::centredRight);
    drawLabel (g, "wave", juce::Rectangle<int> (6, wave.getY(), 40, wave.getHeight()));
    g.setColour (ink);
    g.setFont (mono (10.0f, Weight::Medium));
    g.drawText (waveText, juce::Rectangle<int> (wave.getRight() + 4, wave.getY(), getWidth() - wave.getRight() - 10, wave.getHeight()),
                juce::Justification::centredRight, false);
}

void OpCard::resized()
{
    auto r = getLocalBounds().reduced (6, 5);
    r.removeFromTop (18);
    ratio.setBounds (r.removeFromTop (18));
    level.setBounds (r.removeFromTop (18));
    r.removeFromTop (3);
    wave.setBounds (r.removeFromTop (14).withX (r.getX() + 34).withWidth (22));
    r.removeFromTop (8);
    env.setBounds (r.withHeight (std::min (r.getHeight(), 44)));
}

void OpCard::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < 22 && onOpen)
        onOpen();
}

// SoundPage ------------------------------------------------------------------------

ParamControl& SoundPage::add (int param, const juce::String& label, bool followsVoice)
{
    auto* c = owned.add (new ParamControl (proc.getState(), label));
    if (followsVoice)
        voiceControls.emplace_back (c, param);
    c->bind (voiceParamID (0, param));
    addAndMakeVisible (c);
    return *c;
}

SoundPage::SoundPage (BatidaProcessor& p) : proc (p), sampleView (p), vary (p), operators (p)
{
    soundStrip.setComponentID ("soundStrip");
    addAndMakeVisible (soundStrip);

    source = &add (vp::SrcMode);
    source->withLabelWidth (0).withOptionLabels ({ "FM", "Sample", "Layer" });
    level = &add (vp::Level, "Level");
    pan = &add (vp::Pan, "Pan");
    velocity = &add (vp::VelSens, "Velocity");
    chain = &add (vp::ChainAmt, "Chain");
    for (auto* c : { level, pan, velocity, chain })
        c->withLabelWidth (54).withValueWidth (54);
    varyButton.withDice = true;
    varyButton.setComponentID ("varyButton");
    varyButton.onClick = [this] { openVary(); };
    addAndMakeVisible (varyButton);

    // FM source
    addAndMakeVisible (algorithm);
    algorithm.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    algorithm.setInterceptsMouseClicks (false, false);
    fmPitch = &add (vp::FmPitch, "Pitch");
    feedback = &add (vp::FmFeedback, "Feedback");
    harmonic = &add (vp::FmHarm, "Harmonic");
    bright = &add (vp::FmBright, "Bright");
    brightDecay = &add (vp::FmBrightDecay, "Br. decay");
    grit = &add (vp::FmGrit, "Grit");
    openOperators.onClick = [this] { operators.bindVoice (voice); operators.open(); };
    addAndMakeVisible (openOperators);

    // Sample source
    addAndMakeVisible (sampleView);
    start = &add (vp::SmpStart, "Start");
    end = &add (vp::SmpEnd, "End");
    tune = &add (vp::SmpTune, "Tune");
    gain = &add (vp::SmpGain, "Gain");
    fadeIn = &add (vp::SmpFadeIn, "Fade in");
    fadeOut = &add (vp::SmpFadeOut, "Fade out");
    reverse = &add (vp::SmpReverse, "Reverse");
    for (auto* c : { start, end, tune, gain, fadeIn, fadeOut })
        c->withLabelWidth (58).withValueWidth (52);
    loadButton.onClick = [this] { loadSample(); };
    clearButton.onClick = [this] { proc.clearSample (voice); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);

    // Layer
    balance = &add (vp::Balance, "Balance");
    balance->withLabelWidth (58).withValueWidth (52);
    layerView.onChange = [this] (int) { layoutSource(); repaint(); };
    addAndMakeVisible (layerView);
    addAndMakeVisible (layerPeaks);
    addAndMakeVisible (layerAlgorithm);
    layerTune = &add (vp::SmpTune, "Tune");
    layerGain = &add (vp::SmpGain, "Gain");
    layerPitch = &add (vp::FmPitch, "Pitch");
    layerBright = &add (vp::FmBright, "Bright");
    for (auto* c : { layerTune, layerGain, layerPitch, layerBright })
        c->withLabelWidth (40).withValueWidth (64);

    // FX
    punch = &add (vp::Punch, "Amt");
    punch->withLabelWidth (26).withValueWidth (36);
    drive = &add (vp::Drive, "Amt");
    drive->withLabelWidth (26).withValueWidth (36);
    driveType = &add (vp::DriveType, "Type");
    driveType->asMenu().withLabelWidth (30);
    filterType = &add (vp::FltType);
    filterType->withLabelWidth (0).withOptionLabels ({ "LP", "HP", "BP" });
    cutoff = &add (vp::FltCutoff, "Cutoff");
    resonance = &add (vp::FltRes, "Reso");
    for (auto* c : { cutoff, resonance })
        c->withLabelWidth (44).withValueWidth (62);
    addAndMakeVisible (driveGraph);
    addAndMakeVisible (filterGraph);

    // Row 2
    for (int o = 0; o < kNumOps; ++o)
    {
        auto* card = ops.add (new OpCard (p, o));
        card->onOpen = [this] { operators.bindVoice (voice); operators.open(); };
        addAndMakeVisible (card);
    }
    sliceMode = &add (vp::SmpSliceMode, "Slices");
    slices = &add (vp::SmpSlices, "Count");
    sliceSens = &add (vp::SmpSliceSens, "Sensitivity");
    loop = &add (vp::SmpLoop, "Loop");
    loopStart = &add (vp::SmpLoopStart, "Loop start");
    loopEnd = &add (vp::SmpLoopEnd, "Loop end");
    for (auto* c : { slices, sliceSens, loopStart, loopEnd })
        c->withLabelWidth (70).withValueWidth (52);
    sliceMode->withLabelWidth (70);

    addAndMakeVisible (ampGraph);
    attack = &add (vp::AmpA, "A");
    decay = &add (vp::AmpD, "D");
    sustain = &add (vp::AmpS, "S");
    release = &add (vp::AmpR, "R");
    for (auto* c : { attack, decay, sustain, release })
        c->withLabelWidth (12).withValueWidth (54);

    addAndMakeVisible (pitchGraph);
    pitchAmount = &add (vp::PitchAmt, "Amt");
    pitchDecay = &add (vp::PitchDec, "Decay");
    playMode = &add (vp::PlayMode);
    playMode->withLabelWidth (0).withOptionLabels ({ "One-shot", "Gate" });
    glide = &add (vp::Glide, "Glide");
    for (auto* c : { pitchAmount, pitchDecay, glide })
        c->withLabelWidth (40).withValueWidth (64);

    vary.onSave = [this] { if (onSaveSound) onSaveSound(); };
    addChildComponent (vary);
    addChildComponent (operators);

    bindVoice (0);
    startTimerHz (30);
}

SoundPage::~SoundPage()
{
    stopTimer();
}

void SoundPage::bindVoice (int v)
{
    voice = v;
    for (auto& [c, param] : voiceControls)
        c->bind (voiceParamID (v, param));
    for (auto* card : ops)
        card->bindVoice (v);
    sampleView.setVoice (v);
    vary.setVoice (v);
    operators.bindVoice (v);
    shownSource = -1;
    shownSampleVersion = -1;
    timerCallback();
}

void SoundPage::openVary()
{
    vary.setVoice (voice);
    vary.open();
    if (proc.varyVoice() != voice || proc.varyCandidates().empty())
        vary.startVary();
}

void SoundPage::closeOverlays()
{
    vary.close();
    operators.close();
}

void SoundPage::loadSample()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into sound " + juce::String (voice + 1), juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SoundPage> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult() != juce::File())
                                  safe->proc.loadSample (safe->voice, fc.getResult());
                          });
}

void SoundPage::timerCallback()
{
    if (! isVisibleInTree (*this) && shownSource >= 0)
        return; // hidden pages do nothing
    const auto p = proc.readVoiceParams (voice);
    const auto src = juce::jlimit (0, 2, p.choice (vp::SrcMode));
    if (src != shownSource)
    {
        shownSource = src;
        layoutSource();
        repaint();
    }

    // The strip: name, slot, note and where the sound came from.
    const auto& file = proc.getOrigins().soundFiles[(size_t) voice];
    auto origin = juce::String();
    if (file.existsAsFile())
        origin = file.getFileNameWithoutExtension() + (proc.library().isFactory (file) ? " (Factory)" : " (User)");
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    soundStrip.setText (proc.getVoiceName (voice), "Sound " + juce::String (voice + 1));
    soundStrip.setSubtitle ("slot " + juce::String (voice + 1) + dot
                            + juce::MidiMessage::getMidiNoteName (kDrumMapFirstNote + voice, true, true, 3)
                            + (origin.isNotEmpty() ? dot + origin : juce::String()));

    // Graphs
    ampGraph.set (p[vp::AmpA], p[vp::AmpD], p[vp::AmpS], p[vp::AmpR], p.choice (vp::PlayMode) == (int) PlayMode::Gate);
    pitchGraph.set (p[vp::PitchAmt], p[vp::PitchDec]);
    driveGraph.set (p.choice (vp::DriveType), p[vp::Drive]);
    filterGraph.set (p.choice (vp::FltType), p[vp::FltCutoff], p[vp::FltRes]);

    const auto fm = computeEffectiveFm (p);
    algorithm.set (fm.algorithm);
    layerAlgorithm.set (fm.algorithm);
    for (int o = 0; o < kNumOps; ++o)
        ops[o]->update (p, fm.ops[(size_t) o]);
    feedback->setMarker (std::abs (fm.feedback - p[vp::FmFeedback]) > 1.0e-4f ? std::optional<double> (fm.feedback) : std::nullopt);

    // What the slice and loop settings don't use right now is dimmed.
    const auto sliceModeNow = p.choice (vp::SmpSliceMode);
    slices->setAlpha (sliceModeNow == (int) SliceMode::Grid ? 1.0f : 0.4f);
    sliceSens->setAlpha (sliceModeNow == (int) SliceMode::Transients ? 1.0f : 0.4f);
    loopStart->setAlpha (p.flag (vp::SmpLoop) ? 1.0f : 0.4f);
    loopEnd->setAlpha (p.flag (vp::SmpLoop) ? 1.0f : 0.4f);

    auto& slot = proc.sampleSlot (voice);
    if (slot.getVersion() != shownSampleVersion)
    {
        shownSampleVersion = slot.getVersion();
        fileLine = {};
        fileInfo = {};
        if (const auto* data = slot.getDisplayData(); data != nullptr && slot.getStatus() == SampleSlot::Status::Loaded)
        {
            fileLine = juce::File (slot.getPath()).getFileName();
            fileInfo = juce::String (data->numChannels() > 1 ? "stereo" : "mono") + dot + juce::String (data->sampleRate / 1000.0, 1) + " kHz"
                     + dot + juce::String (data->numFrames() / data->sampleRate, 2) + " s";
            std::vector<float> peaks;
            for (const auto& [lo, hi] : data->peaks)
                peaks.push_back (std::max (-lo, hi));
            layerPeaks.set (std::move (peaks), ink);
        }
        else
        {
            fileLine = slot.getStatus() == SampleSlot::Status::Missing ? "MISSING: " + juce::File (slot.getPath()).getFileName() : juce::String ("no sample");
            layerPeaks.set ({}, ink);
        }
        repaint (sourceBounds);
        sampleView.repaint();
    }

    // The waveform redraws when its markers or slices change (automation too).
    const std::array<float, 12> key { p[vp::SmpStart], p[vp::SmpEnd], p[vp::SmpLoopStart], p[vp::SmpLoopEnd], p[vp::SmpLoop],
                                      p[vp::SmpSliceMode], p[vp::SmpSlices], p[vp::SmpSliceSens], p[vp::SmpFadeIn], p[vp::SmpFadeOut],
                                      p[vp::PlayMode], (float) voice };
    if (key != sampleKey)
    {
        sampleKey = key;
        sampleView.repaint();
    }
}

void SoundPage::layoutSource()
{
    const auto fmShown = shownSource == 0 || (shownSource == 2 && layerView.getSelected() == 2);
    const auto sampleShown = shownSource == 1 || (shownSource == 2 && layerView.getSelected() == 1);
    const auto both = shownSource == 2 && layerView.getSelected() == 0;

    for (juce::Component* c : { (juce::Component*) &algorithm, (juce::Component*) fmPitch, (juce::Component*) feedback,
                                (juce::Component*) harmonic, (juce::Component*) bright, (juce::Component*) brightDecay,
                                (juce::Component*) grit, (juce::Component*) &openOperators })
        c->setVisible (fmShown);
    for (juce::Component* c : { (juce::Component*) &sampleView, (juce::Component*) start, (juce::Component*) end, (juce::Component*) tune,
                                (juce::Component*) gain, (juce::Component*) fadeIn, (juce::Component*) fadeOut, (juce::Component*) reverse })
        c->setVisible (sampleShown);
    for (juce::Component* c : { (juce::Component*) &loadButton, (juce::Component*) &clearButton })
        c->setVisible (sampleShown || both);
    for (juce::Component* c : { (juce::Component*) &layerPeaks, (juce::Component*) &layerAlgorithm, (juce::Component*) layerTune,
                                (juce::Component*) layerGain, (juce::Component*) layerPitch, (juce::Component*) layerBright })
        c->setVisible (both);
    balance->setVisible (shownSource == 2);
    layerView.setVisible (shownSource == 2);

    // Row 2: operators for FM, slices and loop for a sample.
    const auto opsShown = shownSource != 1;
    for (auto* card : ops)
        card->setVisible (opsShown);
    for (auto* c : { sliceMode, slices, sliceSens, loop, loopStart, loopEnd })
        c->setVisible (! opsShown);
    resized();
}

void SoundPage::paint (juce::Graphics& g)
{
    const auto sourceTag = shownSource == 1 ? "01 Sample source" : shownSource == 2 ? "01 Layer source" : "01 FM source";
    drawPanel (g, sourceBounds, sourceTag);
    drawPanel (g, punchBounds, "02 Punch");
    drawPanel (g, driveBounds, "03 Drive");
    drawPanel (g, filterBounds, "04 Filter");
    drawPanel (g, opsBounds, shownSource == 1 ? "05 Slices " + juce::String::fromUTF8 ("\xc2\xb7") + " loop" : "05 Operators");
    drawPanel (g, ampBounds, "06 Amp env");
    drawPanel (g, pitchBounds, "07 Pitch " + juce::String::fromUTF8 ("\xc2\xb7") + " play");

    // Arrows between the stages of the sound's own path.
    g.setColour (faint);
    g.setFont (mono (11.0f));
    for (const auto& [a, b] : { std::pair { sourceBounds, punchBounds }, std::pair { punchBounds, driveBounds }, std::pair { driveBounds, filterBounds } })
        g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), juce::Rectangle<int> (a.getRight(), a.getCentreY() - 8, b.getX() - a.getRight(), 16),
                    juce::Justification::centred);

    const auto sampleShown = shownSource == 1 || (shownSource == 2 && layerView.getSelected() == 1);
    if (sampleShown)
    {
        const auto area = panelContent (sourceBounds).withTrimmedTop (shownSource == 2 ? 26 : 0).withHeight (14);
        g.setColour (ink);
        g.setFont (mono (10.5f, Weight::Medium));
        g.drawText (fileLine, area, juce::Justification::centredLeft, true);
        g.setColour (muted);
        g.setFont (mono (9.5f));
        g.drawText (fileInfo, area, juce::Justification::centredRight, true);
    }
    if (shownSource == 2 && layerView.getSelected() == 0)
    {
        g.setColour (ink);
        g.setFont (mono (10.0f, Weight::Bold));
        g.drawText ("SAMPLE", layerPeaks.getBounds().withY (layerPeaks.getY() - 16).withHeight (12), juce::Justification::centredLeft);
        g.drawText ("FM", layerAlgorithm.getBounds().withY (layerAlgorithm.getY() - 16).withHeight (12), juce::Justification::centredLeft);
        g.setColour (muted);
        g.setFont (mono (9.5f));
        g.drawText (fileLine, layerPeaks.getBounds().translated (0, layerPeaks.getHeight() + 4).withHeight (12), juce::Justification::centredLeft, true);
    }
}

void SoundPage::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (46);
    soundStrip.setBounds (header.removeFromLeft (250).withTrimmedTop (4).withHeight (38));
    header.removeFromLeft (16);
    source->setBounds (header.removeFromLeft (source->getIdealWidth()).withSizeKeepingCentre (source->getIdealWidth(), 22));
    varyButton.setBounds (header.removeFromRight (96).withSizeKeepingCentre (96, 30));
    header.removeFromRight (24);
    auto mix = header.removeFromRight (380).withSizeKeepingCentre (380, 44);
    const auto half = (mix.getWidth() - 16) / 2;
    auto top = mix.removeFromTop (20), bottom = mix.removeFromBottom (20);
    level->setBounds (top.removeFromLeft (half));
    pan->setBounds (top.removeFromRight (half));
    velocity->setBounds (bottom.removeFromLeft (half));
    chain->setBounds (bottom.removeFromRight (half));

    r.removeFromTop (10);
    auto row1 = r.removeFromTop (212);
    sourceBounds = row1.removeFromLeft (436);
    row1.removeFromLeft (16);
    punchBounds = row1.removeFromLeft (96);
    row1.removeFromLeft (16);
    driveBounds = row1.removeFromLeft (150);
    row1.removeFromLeft (16);
    filterBounds = row1;

    r.removeFromTop (10);
    auto row2 = r;
    opsBounds = row2.removeFromLeft (436);
    row2.removeFromLeft (12);
    ampBounds = row2.removeFromLeft (268);
    row2.removeFromLeft (12);
    pitchBounds = row2;

    // 01 Source
    {
        auto c = panelContent (sourceBounds);
        const auto headerRight = juce::Rectangle<int> (sourceBounds.getRight() - 8, sourceBounds.getY() + 5, 0, 20);
        layerView.setBounds (headerRight.withLeft (headerRight.getX() - layerView.getIdealWidth()));
        // With a layer, the view switch takes the right; the rest sits left of it.
        const auto headerEnd = shownSource == 2 ? layerView.getX() - 10 : headerRight.getX();
        openOperators.setBounds (headerRight.withLeft (headerEnd - 100).withRight (headerEnd));
        clearButton.setBounds (headerEnd - 52, headerRight.getY(), 52, 20);
        loadButton.setBounds (clearButton.getX() - 58, headerRight.getY(), 54, 20);

        auto fm = c;
        if (shownSource == 2)
            fm.removeFromTop (26); // under the Balance bar
        algorithm.setBounds (fm.removeFromLeft (140).withHeight (std::min (140, fm.getHeight())));
        fm.removeFromLeft (14);
        RowLayout rows { fm };
        fmPitch->setBounds (rows.next());
        feedback->setBounds (rows.next());
        rows.next (6);
        for (auto* ctl : { harmonic, bright, brightDecay, grit })
            ctl->setBounds (rows.next());

        auto smp = c;
        if (shownSource == 2)
        {
            balance->setBounds (smp.removeFromTop (20));
            smp.removeFromTop (6);
        }
        smp.removeFromTop (18);
        sampleView.setBounds (smp.removeFromTop (shownSource == 2 ? 46 : 74));
        smp.removeFromTop (shownSource == 2 ? 6 : 8);
        const auto colW = (smp.getWidth() - 14) / 2;
        RowLayout left { smp.withWidth (colW) }, right { smp.withTrimmedLeft (colW + 14) };
        start->setBounds (left.next());
        end->setBounds (right.next());
        tune->setBounds (left.next());
        gain->setBounds (right.next());
        fadeIn->setBounds (left.next());
        fadeOut->setBounds (right.next());
        if (shownSource == 2) // no room below: next to LOAD in the header
            reverse->setBounds (loadButton.getX() - 8 - reverse->getIdealWidth(), headerRight.getY(), reverse->getIdealWidth(), 20);
        else
            reverse->setBounds (left.next().withWidth (reverse->getIdealWidth()));

        // Layer: both summaries side by side.
        auto both = c.withTrimmedTop (26 + 18);
        const auto halfW = (both.getWidth() - 14) / 2;
        auto sCol = both.removeFromLeft (halfW);
        both.removeFromLeft (14);
        auto fCol = both;
        layerPeaks.setBounds (sCol.removeFromTop (56));
        sCol.removeFromTop (22);
        layerTune->setBounds (sCol.removeFromTop (20));
        sCol.removeFromTop (2);
        layerGain->setBounds (sCol.removeFromTop (20));
        layerAlgorithm.setBounds (fCol.removeFromTop (90).withWidth (100));
        fCol.removeFromTop (6);
        layerPitch->setBounds (fCol.removeFromTop (20));
        fCol.removeFromTop (2);
        layerBright->setBounds (fCol.removeFromTop (20));
    }

    // 02–04
    punch->setBounds (panelContent (punchBounds).removeFromTop (20));
    {
        auto c = panelContent (driveBounds);
        driveGraph.setBounds (c.removeFromTop (96));
        c.removeFromTop (8);
        drive->setBounds (c.removeFromTop (20));
        c.removeFromTop (4);
        driveType->setBounds (c.removeFromTop (20));
    }
    {
        auto c = panelContent (filterBounds);
        filterType->setBounds (juce::Rectangle<int> (filterBounds.getRight() - 8 - filterType->getIdealWidth(), filterBounds.getY() + 5,
                                                     filterType->getIdealWidth(), 20));
        filterGraph.setBounds (c.removeFromTop (110));
        c.removeFromTop (8);
        cutoff->setBounds (c.removeFromTop (20));
        c.removeFromTop (2);
        resonance->setBounds (c.removeFromTop (20));
    }

    // 05 Operators, or slices and loop
    {
        auto c = panelContent (opsBounds);
        const auto w = (c.getWidth() - 3 * 6) / 4;
        for (auto* card : ops)
        {
            card->setBounds (c.removeFromLeft (w));
            c.removeFromLeft (6);
        }
        RowLayout rows { panelContent (opsBounds) };
        sliceMode->setBounds (rows.next (22));
        slices->setBounds (rows.next());
        sliceSens->setBounds (rows.next());
        rows.next (10);
        loop->setBounds (rows.next().withWidth (loop->getIdealWidth()));
        loopStart->setBounds (rows.next());
        loopEnd->setBounds (rows.next());
    }

    // 06 Amp env
    {
        auto c = panelContent (ampBounds);
        auto rows = c.removeFromBottom (42);
        ampGraph.setBounds (c.withTrimmedBottom (8));
        const auto colW = (rows.getWidth() - 12) / 2;
        auto l = rows.withWidth (colW), rr = rows.withTrimmedLeft (colW + 12);
        attack->setBounds (l.removeFromTop (20));
        decay->setBounds (rr.removeFromTop (20));
        l.removeFromTop (2);
        rr.removeFromTop (2);
        sustain->setBounds (l.removeFromTop (20));
        release->setBounds (rr.removeFromTop (20));
    }

    // 07 Pitch · play
    {
        auto c = panelContent (pitchBounds);
        auto rows = c.removeFromBottom (4 * 22);
        pitchGraph.setBounds (c.withTrimmedBottom (8));
        RowLayout layout { rows };
        pitchAmount->setBounds (layout.next());
        pitchDecay->setBounds (layout.next());
        playMode->setBounds (layout.next().withWidth (playMode->getIdealWidth()));
        glide->setBounds (layout.next());
    }

    vary.setBounds (getLocalBounds());
    operators.setBounds (getLocalBounds());
}
