#pragma once

#include "Pattern.h"

#include <array>
#include <atomic>
#include <vector>

namespace batida
{

enum class RunMode { Keys, Transport };
enum class Quantise { Step, Beat, Bar };

constexpr int kPatternKeyFirst = 60; // C3..D#4 play patterns 1..16
constexpr int kPatternKeyLast = kPatternKeyFirst + kNumPatterns - 1;

// Where the host is, once per block.
struct Transport
{
    bool hostPlaying = false;
    double ppq = 0.0;          // quarter notes at the block start
    double bpm = 120.0;
    double beatsPerBar = 4.0;  // in quarter notes
};

struct SeqSettings
{
    RunMode run = RunMode::Keys;
    int pattern = 0;           // the Pattern parameter
    bool play = false;         // internal play (Transport mode, host stopped)
    double tempo = 120.0;      // internal tempo
    float swing = 0.5f;        // 0.5 straight .. 0.75
    Quantise quantise = Quantise::Beat;
    bool latch = false;
};

struct SeqEvent
{
    enum class Type { NoteOn, NoteOff, XyLock, XyRelease };
    Type type = Type::NoteOn;
    int offset = 0;
    int voice = 0, key = 60, slice = -1;
    float velocity = 1.0f;
    float x = 0.5f, y = 0.0f;
};

// Plays patterns. Steps are 16th notes (0.25 quarter notes). The step grid is
// computed from the song position every block, so host loops and jumps stay
// in sync; hits land on their exact sample.
class Sequencer
{
public:
    void prepare (double sampleRate);
    void reset();

    // Pattern keys for the coming block (call before generate).
    void keyDown (int pattern, float velocity, int offset);
    void keyUp (int pattern, int offset);

    // Appends this block's events to `out`, sorted by offset.
    void generate (int numSamples, const Transport& transport, const SeqSettings& settings,
                   const PatternBank* bank, std::vector<SeqEvent>& out);

    // For the UI (any thread).
    bool isRunning() const { return uiRunning.load(); }
    int getActivePattern() const { return uiPattern.load(); }
    int getPatternStep() const { return uiPatternStep.load(); }
    int getTrackStep (int track) const { return uiTrackStep[(size_t) track].load(); }

    static constexpr double kStep = 0.25;

private:
    struct Command
    {
        bool down;
        int pattern, offset;
        float velocity;
    };

    struct PendingOff
    {
        double time;
        int voice, key;
    };

    struct Run
    {
        bool active = false;
        int pattern = 0;
        double origin = 0.0;   // ppq of step 0
        long long parityBase = 0;
        float velocityScale = 1.0f;
    };

    void stopAt (double t);
    void startOrSwitch (int pattern, float velocity, double t);
    double quantised (double t) const;
    void emitInterval (double from, double to);
    void emitRun (const Run& run, double from, double to);
    void flushOffsets (double to);
    void push (SeqEvent e);
    int offsetOf (double t) const;
    long long sampleOf (double t) const; // nearest sample, relative to the block start

    double sampleRate = 48000.0;

    // Per-block context
    std::vector<SeqEvent>* out = nullptr;
    const PatternBank* bank = nullptr;
    SeqSettings settings;
    double blockStart = 0.0, ppqPerSample = 0.0, beatsPerBar = 4.0;
    bool synced = false;
    int blockSamples = 0;

    std::array<Command, 64> commands {};
    int numCommands = 0;

    std::array<std::pair<int, float>, kNumPatterns> held {};
    int numHeld = 0;

    Run run, pending;           // pending: a quantised start/switch
    double pendingSwitchAt = 0.0;
    std::vector<PendingOff> offs;

    bool lastHostPlaying = false, lastLatch = false;
    RunMode lastRun = RunMode::Keys;
    int lastParamPattern = -1;
    double lastEnd = -1.0;      // ppq where the previous block ended
    double internalPpq = 0.0;

    bool xyActive = false;

    std::atomic<bool> uiRunning { false };
    std::atomic<int> uiPattern { 0 }, uiPatternStep { -1 };
    std::array<std::atomic<int>, kNumTracks> uiTrackStep {};
};

} // namespace batida
