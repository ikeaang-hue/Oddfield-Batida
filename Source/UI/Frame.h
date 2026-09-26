#pragma once

#include "BrowseStrip.h"
#include "ParamControl.h"
#include "Widgets.h"
#include "Plugin/PluginProcessor.h"

// The frame around every page: the top bar and the sound row, plus the
// settings popover.

// One sound slot: number and name, hit light (lime, decaying), note or track
// length, Chain amount, Mute and Solo. Click selects and plays it; double-click
// renames; drag onto another slot swaps them; right-click opens the menu; a
// sample or sound file can be dropped on it (handled by the editor).
class SoundCell final : public juce::Component, public juce::DragAndDropTarget
{
public:
    struct Data
    {
        juce::String name, detail; // detail: the note ("C1") or the track length ("LEN 16")
        bool selected = false, missing = false, audible = true, mute = false, solo = false;
        float chain = 1.0f;
    };

    explicit SoundCell (int index);
    void setData (const Data& d);
    void hit();          // flash the light
    void setDropHighlight (bool on);

    std::function<void()> onPress, onRelease, onRename, onMenu, onMute, onSolo;
    std::function<void (int from)> onSwapFrom;
    const int index;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { setDropHighlight (true); }
    void itemDragExit (const SourceDetails&) override { setDropHighlight (false); }
    void itemDropped (const SourceDetails&) override;

private:
    juce::Rectangle<int> muteArea() const;
    juce::Rectangle<int> soloArea() const;
    juce::Rectangle<int> lightArea() const;

    Data data;
    bool pressed = false, dragStarted = false, dropHighlight = false;
    double lastHit = -1.0e9;
    std::unique_ptr<juce::VBlankAttachment> vblank;
};

// Fills the cells from the processor: names, missing samples, audibility,
// Chain amount, mute/solo; `detail` gives each cell's second line.
// Slot menus (sound row and SEQ tracks): Choke group and "Resample here",
// which renders the pattern on screen or any slot's sound into this slot.
constexpr int kChokeItem = 500, kResamplePatternItem = 300, kResampleSoundItem = 400;
juce::PopupMenu resampleMenu (BatidaProcessor& proc, int target);
bool runResample (BatidaProcessor& proc, int choice, int target); // true if `choice` was a resample item

void refreshSoundCells (BatidaProcessor& proc, std::array<SoundCell*, batida::kNumVoices> cells, int selected,
                        const std::function<juce::String (int)>& detail);

// The 8 sounds across the top of every page but SEQ.
class SoundRow final : public juce::Component
{
public:
    SoundRow();
    std::array<SoundCell*, batida::kNumVoices> getCells();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::OwnedArray<SoundCell> cells;
};

// The master meter: 10 blocks lit by the output level (lime), with a peak hold.
class Meter final : public juce::Component
{
public:
    void setLevel (float peak);
    void paint (juce::Graphics&) override;

private:
    float shown = 0.0f;
    double lastUpdate = 0.0;
};

// BATIDA/ · page nav · kit strip (and the missing-samples notice) · undo, redo,
// settings · master meter and level.
class TopBar final : public juce::Component
{
public:
    explicit TopBar (BatidaProcessor& p);

    enum Page { Kit, Seq, Mod, Sound, Lib };
    void setPage (int page);
    std::function<void (int)> onPage;
    std::function<void()> onRelink, onSettings, onAbout;

    BrowseStrip kitStrip { BrowseStrip::Style::Compact };
    TextLink undo { "UNDO" }, redo { "REDO" }, settings { "CFG" };
    void setMissing (int count);
    void setOutputPeak (float peak) { meter.setLevel (peak); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { repaint (navBounds()); }

private:
    juce::Rectangle<int> navBounds() const;
    juce::Rectangle<int> navItem (int i) const;
    juce::Rectangle<int> logoBounds() const;

    ParamControl master;
    Meter meter;
    struct Notice final : juce::Button
    {
        Notice() : juce::Button ("missing") {}
        void paintButton (juce::Graphics&, bool, bool) override;
        int count = 0;
    } notice;
    int page = 0;
};

// The settings popover under CFG: MIDI mode, Keys Sound, zoom, library folder,
// author, and the version line. Clicking outside closes it.
class SettingsPanel final : public juce::Component
{
public:
    explicit SettingsPanel (BatidaProcessor& p);
    std::function<void (int percent)> onZoom;
    std::function<void()> onClose;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 330, kHeight = 324;

private:
    BatidaProcessor& proc;
    ParamControl midiMode, keysSound;
    Segmented zoom { { "100%", "125%", "150%" } };
    TextLink reveal { "SHOW IN FINDER", true };
    juce::TextEditor author;
};

// The card under the BATIDA/ logo: version and credits. Clicking outside closes it.
class AboutCard final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    static constexpr int kWidth = 320, kHeight = 262;
};
