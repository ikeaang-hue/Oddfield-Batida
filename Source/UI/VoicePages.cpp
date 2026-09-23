#include "VoicePages.h"

#include "Engine/SampleSource.h"

using namespace batida;

namespace
{
constexpr int kTitleHeight = 18;
constexpr int kGap = 10;

void styleNote (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (13.0f));
    label.setColour (juce::Label::textColourId, juce::Colours::grey);
    label.setJustificationType (juce::Justification::topLeft);
}
} // namespace

// VoicePage -------------------------------------------------------------------

int VoicePage::addSection (const juce::String& title)
{
    sections.push_back ({ title, {}, {} });
    return (int) sections.size() - 1;
}

ParamControl& VoicePage::addControl (int section, const juce::String& label)
{
    owned.push_back (std::make_unique<ParamControl> (proc.getState(), label));
    auto& ref = *owned.back();
    addAndMakeVisible (ref);
    sections[(size_t) section].controls.push_back (&ref);
    return ref;
}

ParamControl& VoicePage::add (int section, int param, const juce::String& label)
{
    auto& c = addControl (section, label);
    selectedVoiceControls.emplace_back (&c, param);
    return c;
}

ParamControl& VoicePage::addGlobal (int section, int param, const juce::String& label)
{
    auto& c = addControl (section, label);
    c.bind (globalParamID (param));
    return c;
}

ParamControl& VoicePage::addFixedVoice (int section, int fixedVoice, int param, const juce::String& label)
{
    auto& c = addControl (section, label);
    c.bind (voiceParamID (fixedVoice, param));
    return c;
}

void VoicePage::bindVoice (int newVoice)
{
    voice = newVoice;
    for (auto& [control, param] : selectedVoiceControls)
        control->bind (voiceParamID (voice, param));
}

int VoicePage::layoutRow (juce::Rectangle<int> area, std::initializer_list<int> sectionIndices, int perRow)
{
    auto x = area.getX();
    int height = 0;

    for (const auto index : sectionIndices)
    {
        auto& s = sections[(size_t) index];
        const auto n = (int) s.controls.size();
        const auto cols = std::min (n, perRow);
        const auto rows = (n + cols - 1) / cols;
        const auto top = area.getY() + kTitleHeight;
        int w = 12;

        if (rows == 1)
        {
            // One row: each control keeps its own width.
            auto cx = x + 6;
            for (auto* c : s.controls)
            {
                c->setBounds (cx, top, c->getPreferredWidth(), ParamControl::kHeight);
                cx += c->getPreferredWidth();
            }
            w = cx - x + 6;
        }
        else
        {
            for (int i = 0; i < n; ++i)
                s.controls[(size_t) i]->setBounds (x + 6 + (i % cols) * ParamControl::kWidth,
                                                   top + (i / cols) * ParamControl::kHeight,
                                                   ParamControl::kWidth, ParamControl::kHeight);
            w = sectionWidth (cols);
        }

        const auto h = kTitleHeight + rows * ParamControl::kHeight + 6;
        s.bounds = { x, area.getY(), w, h };
        x += w + kGap;
        height = std::max (height, h);
    }
    return height;
}

void VoicePage::paint (juce::Graphics& g)
{
    for (const auto& s : sections)
    {
        if (s.bounds.isEmpty())
            continue;
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (s.bounds.toFloat(), 4.0f);
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (s.title, s.bounds.withHeight (kTitleHeight).reduced (6, 0), juce::Justification::centredLeft);
    }
}

// WaveformView ----------------------------------------------------------------

void WaveformView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 4.0f);

    auto& slot = proc.sampleSlot (voice);
    const auto* data = slot.getDisplayData();

    if (data == nullptr || data->peaks.empty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (slot.getStatus() == SampleSlot::Status::Missing ? "Sample file not found"
                                                                     : "Drop a WAV, AIFF or FLAC file here, or click Load",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto p = proc.readVoiceParams (voice);
    const auto xAt = [&] (float norm) { return r.getX() + norm * r.getWidth(); };

    const auto start = std::min (p[vp::SmpStart], p[vp::SmpEnd]);
    const auto end = std::max (p[vp::SmpStart], p[vp::SmpEnd]);

    // Played region
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xAt (start), r.getY(), xAt (end), r.getBottom()));

    // Peaks
    const auto bins = (int) data->peaks.size();
    const auto mid = r.getCentreY();
    const auto half = r.getHeight() * 0.45f;
    const auto binW = r.getWidth() / (float) bins;
    for (int i = 0; i < bins; ++i)
    {
        const auto [lo, hi] = data->peaks[(size_t) i];
        const auto norm = ((float) i + 0.5f) / (float) bins;
        g.setColour (norm >= start && norm <= end ? juce::Colour (0xff4fc3f7) : juce::Colours::grey);
        g.fillRect (juce::Rectangle<float> (r.getX() + (float) i * binW, mid - hi * half,
                                            binW + 0.5f, std::max (1.0f, (hi - lo) * half)));
    }

    // Slice boundaries, numbered as the sequencer's Slice lane counts them.
    SliceSpec spec;
    spec.mode = (SliceMode) juce::jlimit (0, 2, p.choice (vp::SmpSliceMode));
    spec.gridCount = juce::roundToInt (p[vp::SmpSlices]);
    spec.sensitivity = p[vp::SmpSliceSens];
    if (spec.mode != SliceMode::Off)
    {
        const auto frames = (double) data->numFrames();
        const auto count = sliceCount (*data, spec, start * frames, end * frames);
        g.setFont (juce::FontOptions (11.0f));
        for (int k = 0; k < count; ++k)
        {
            const auto from = sliceRegion (*data, spec, start * frames, end * frames, k).first / frames;
            g.setColour (juce::Colours::yellow.withAlpha (0.7f));
            g.drawVerticalLine ((int) xAt ((float) from), r.getY(), r.getBottom());
            g.drawText (juce::String (k + 1), juce::Rectangle<float> (xAt ((float) from) + 2.0f, r.getY() + 2.0f, 24.0f, 12.0f),
                        juce::Justification::topLeft);
        }
    }

    // Markers
    g.setColour (juce::Colours::white);
    g.drawVerticalLine ((int) xAt (start), r.getY(), r.getBottom());
    g.drawVerticalLine ((int) xAt (end) - 1, r.getY(), r.getBottom());

    if (p.flag (vp::SmpLoop))
    {
        g.setColour (juce::Colours::orange);
        g.drawVerticalLine ((int) xAt (p[vp::SmpLoopStart]), r.getY(), r.getBottom());
        g.drawVerticalLine ((int) xAt (p[vp::SmpLoopEnd]) - 1, r.getY(), r.getBottom());
        if (p.choice (vp::PlayMode) != (int) PlayMode::Gate)
        {
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Loop plays in Gate mode", getLocalBounds().reduced (6), juce::Justification::topRight);
        }
    }

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (12.0f));
    const auto seconds = data->numFrames() / data->sampleRate;
    g.drawText (juce::String (seconds, 2) + " s, " + (data->numChannels() > 1 ? "stereo" : "mono") + ", "
                    + juce::String (data->sampleRate / 1000.0, 1) + " kHz",
                getLocalBounds().reduced (6), juce::Justification::bottomRight);
}

// SourcePage ------------------------------------------------------------------

SourcePage::SourcePage (BatidaProcessor& p) : VoicePage (p), waveform (p)
{
    voiceSection = addSection ("Voice");
    add (voiceSection, vp::SrcMode).withWidth (96);
    add (voiceSection, vp::Balance, "Layer Bal.");
    add (voiceSection, vp::Level);
    add (voiceSection, vp::Pan);
    add (voiceSection, vp::VelSens);
    add (voiceSection, vp::ChainAmt, "Chain Amount");

    sampleSection = addSection ("Sample");
    add (sampleSection, vp::SmpTune, "Tune");
    add (sampleSection, vp::SmpGain, "Gain");
    add (sampleSection, vp::SmpStart);
    add (sampleSection, vp::SmpEnd);
    add (sampleSection, vp::SmpFadeIn);
    add (sampleSection, vp::SmpFadeOut);
    add (sampleSection, vp::SmpReverse);

    loopSection = addSection ("Loop (Gate mode)");
    add (loopSection, vp::SmpLoop);
    add (loopSection, vp::SmpLoopStart);
    add (loopSection, vp::SmpLoopEnd);

    sliceSection = addSection ("Slices (sequencer Slice lane)");
    add (sliceSection, vp::SmpSliceMode, "Mode").withWidth (100);
    add (sliceSection, vp::SmpSlices, "Grid Slices");
    add (sliceSection, vp::SmpSliceSens, "Sensitivity");

    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (fileLabel);
    addAndMakeVisible (waveform);
    fileLabel.setFont (juce::FontOptions (13.0f));
    fileLabel.setMinimumHorizontalScale (0.6f);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample into voice " + juce::String (voice + 1),
                                                       juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                      proc.loadSample (voice, fc.getResult());
                                  refreshStatus();
                              });
    };

    clearButton.onClick = [this]
    {
        proc.clearSample (voice);
        refreshStatus();
    };

    startTimerHz (15);
}

void SourcePage::bindVoice (int newVoice)
{
    VoicePage::bindVoice (newVoice);
    waveform.setVoice (newVoice);
    lastVersion = -1;
    refreshStatus();
}

void SourcePage::refreshStatus()
{
    auto& slot = proc.sampleSlot (voice);
    const auto path = juce::File (slot.getPath());

    switch (slot.getStatus())
    {
        case SampleSlot::Status::Loaded:
            fileLabel.setText (path.getFileName() + (slot.getError().isNotEmpty() ? " (" + slot.getError() + ")" : ""),
                               juce::dontSendNotification);
            fileLabel.setColour (juce::Label::textColourId, juce::Colours::white);
            break;
        case SampleSlot::Status::Missing:
            fileLabel.setText ("Missing: " + slot.getPath(), juce::dontSendNotification);
            fileLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);
            break;
        case SampleSlot::Status::Empty:
        case SampleSlot::Status::Error:
            fileLabel.setText (slot.getError().isNotEmpty() ? slot.getError() : "No sample",
                               juce::dontSendNotification);
            fileLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
            break;
    }
    waveform.repaint();
}

void SourcePage::timerCallback()
{
    // Repaint when the sample or any marker changes (including automation).
    const auto p = proc.readVoiceParams (voice);
    const std::array<float, 8> markers { p[vp::SmpStart], p[vp::SmpEnd], p[vp::SmpLoopStart],
                                         p[vp::SmpLoopEnd], p[vp::SmpLoop] + 2.0f * p[vp::PlayMode],
                                         p[vp::SmpSliceMode], p[vp::SmpSlices], p[vp::SmpSliceSens] };
    const auto version = proc.sampleSlot (voice).getVersion();

    if (version != lastVersion)
    {
        lastVersion = version;
        refreshStatus();
    }
    else if (markers != lastMarkers)
    {
        waveform.repaint();
    }
    lastMarkers = markers;
}

void SourcePage::resized()
{
    auto area = getLocalBounds().reduced (kGap);
    area.removeFromTop (layoutRow (area, { voiceSection }) + kGap);

    auto sampleRow = area.removeFromTop (130);
    auto buttons = sampleRow.removeFromLeft (170);
    loadButton.setBounds (buttons.removeFromTop (28).withWidth (80));
    clearButton.setBounds (loadButton.getBounds().translated (88, 0));
    buttons.removeFromTop (6);
    fileLabel.setBounds (buttons.removeFromTop (60));
    waveform.setBounds (sampleRow.withTrimmedLeft (kGap));
    area.removeFromTop (kGap);

    layoutRow (area, { sampleSection, loopSection, sliceSection });
}

// ChainPage -------------------------------------------------------------------

ChainPage::ChainPage (BatidaProcessor& p) : VoicePage (p)
{
    punchSection = addSection ("Punch");
    add (punchSection, vp::Punch, "Amount");

    driveSection = addSection ("Drive");
    add (driveSection, vp::Drive, "Amount");
    add (driveSection, vp::DriveType, "Type").withWidth (90);

    filterSection = addSection ("Filter");
    add (filterSection, vp::FltType, "Type").withWidth (110);
    add (filterSection, vp::FltCutoff);
    add (filterSection, vp::FltRes);

    styleNote (note, "This voice's own processing: source > amp envelope > punch > drive > filter > level/pan.\n"
                     "Punch adds attack and tightens the tail. After level/pan, the voice's Chain Amount "
                     "(Source tab, or the Kit page) sends it into the kit chain.");
    addAndMakeVisible (note);
}

void ChainPage::resized()
{
    auto area = getLocalBounds().reduced (kGap);
    area.removeFromTop (layoutRow (area, { punchSection, driveSection, filterSection }) + kGap * 2);
    note.setBounds (area.removeFromTop (60));
}

// EnvelopePage ----------------------------------------------------------------

EnvelopePage::EnvelopePage (BatidaProcessor& p) : VoicePage (p)
{
    playSection = addSection ("Play");
    add (playSection, vp::PlayMode).withWidth (104);
    add (playSection, vp::Glide);

    ampSection = addSection ("Amp envelope");
    add (ampSection, vp::AmpA, "Attack");
    add (ampSection, vp::AmpD, "Decay");
    add (ampSection, vp::AmpS, "Sustain");
    add (ampSection, vp::AmpR, "Release");

    pitchSection = addSection ("Pitch envelope");
    add (pitchSection, vp::PitchAmt, "Amount");
    add (pitchSection, vp::PitchDec, "Decay");

    styleNote (note, "One-shot: note length is ignored. Attack > decay (to sustain) > release, "
                     "so sustain above 0 gives a two-stage decay.\n"
                     "Gate: a normal ADSR that holds sustain while the note is held. Samples loop in Gate mode.\n"
                     "Times are how long the level takes to fall by 60 dB. Voices are monophonic; "
                     "with Glide above 0, overlapping notes slide.");
    addAndMakeVisible (note);
}

void EnvelopePage::resized()
{
    auto area = getLocalBounds().reduced (kGap);
    area.removeFromTop (layoutRow (area, { playSection, ampSection, pitchSection }) + kGap * 2);
    note.setBounds (area.removeFromTop (90));
}
