#pragma once

#include "BrowseStrip.h"
#include "Graphs.h"
#include "ParamControl.h"
#include "SoundOverlays.h"
#include "Widgets.h"

// The loaded sample: peaks, start/end handles (drag them), fades, loop
// points and numbered slices.
class SampleView final : public juce::Component
{
public:
    explicit SampleView (BatidaProcessor& p) : proc (p) {}
    void setVoice (int v) { voice = v; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    int handleAt (float x) const; // 0 start, 1 end, -1 none
    juce::RangedAudioParameter* paramFor (int handle) const;
    BatidaProcessor& proc;
    int voice = 0, dragging = -1;
    float dragStart = 0.0f;
};

// One FM operator on the SOUND page: ratio and level (macro offsets in lime),
// its wave and envelope. The header opens the full operator editor.
class OpCard final : public juce::Component
{
public:
    OpCard (BatidaProcessor& p, int op);
    void bindVoice (int voice);
    void update (const batida::VoiceParams& p, const batida::FmOpEffective& e);
    std::function<void()> onOpen;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    const int op;
    ParamControl ratio, level;
    WaveGlyph wave;
    MiniEnvelope env;
    juce::String waveText;
    bool carrier = false, active = true;
};

// SOUND: the selected sound as its own signal path.
//   Header: ‹ name ›, source (FM / Sample / Layer), Level, Pan, Velocity,
//           Chain, VARY.
//   Row 1:  01 Source → 02 Punch → 03 Drive → 04 Filter.
//   Row 2:  05 Operators (or Slices · loop for a sample), 06 Amp env,
//           07 Pitch · play.
// Vary and the operator editor open over the page.
class SoundPage final : public juce::Component, private juce::Timer
{
public:
    explicit SoundPage (BatidaProcessor& p);
    ~SoundPage() override;

    void bindVoice (int voice);
    BrowseStrip soundStrip { BrowseStrip::Style::Large };
    std::function<void()> onSaveSound, onSaveKit; // Vary's Save to library
    void openVary();
    void closeOverlays();
    bool isOverlayOpen() const { return vary.isVisible() || operators.isVisible(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    ParamControl& add (int param, const juce::String& label = {}, bool followsVoice = true);
    void layoutSource();
    void loadSample();

    BatidaProcessor& proc;
    int voice = 0;
    std::vector<std::pair<ParamControl*, int>> voiceControls;
    juce::OwnedArray<ParamControl> owned;

    // Header
    ParamControl *source, *level, *pan, *velocity, *chain;
    SolidButton varyButton { "VARY", theme::lime };

    // FM source
    AlgorithmView algorithm;
    ParamControl *fmPitch, *feedback, *harmonic, *bright, *brightDecay, *grit;
    TextLink openOperators { juce::String::fromUTF8 ("OPERATORS \xe2\x86\x97") };

    // Sample source
    SampleView sampleView;
    ParamControl *start, *end, *tune, *gain, *fadeIn, *fadeOut, *reverse;
    TextLink loadButton { juce::String::fromUTF8 ("LOAD\xe2\x80\xa6"), true }, clearButton { "CLEAR", true };
    std::unique_ptr<juce::FileChooser> chooser;

    // Layer source
    ParamControl* balance;
    Segmented layerView { { "Both", "Sample", "FM" } };
    PeaksView layerPeaks;
    AlgorithmView layerAlgorithm;
    ParamControl *layerTune, *layerGain, *layerPitch, *layerBright;

    // FX
    ParamControl *punch, *drive, *driveType, *filterType, *cutoff, *resonance;
    DriveGraph driveGraph;
    FilterGraph filterGraph;

    // Row 2
    juce::OwnedArray<OpCard> ops;
    ParamControl *sliceMode, *slices, *sliceSens, *loop, *loopStart, *loopEnd;
    AmpEnvelopeGraph ampGraph;
    ParamControl *attack, *decay, *sustain, *release;
    PitchEnvelopeGraph pitchGraph;
    ParamControl *pitchAmount, *pitchDecay, *playMode, *glide, *choke;

    VaryOverlay vary;
    OperatorEditor operators;

    int shownSource = -1, shownSampleVersion = -1;
    std::array<float, 12> sampleKey {};
    juce::Rectangle<int> sourceBounds, punchBounds, driveBounds, filterBounds, opsBounds, ampBounds, pitchBounds;
    juce::String fileLine, fileInfo;
};
