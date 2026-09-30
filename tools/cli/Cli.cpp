#include "Cli.h"

#include "Analysis/Measure.h"
#include "Engine/Render.h"
#include "Library/Library.h"
#include "Library/Reference.h"
#include "Library/Validate.h"

#include <cstdio>
#include <map>
#include <set>

#ifndef BATIDA_VERSION
 #define BATIDA_VERSION "dev"
#endif

namespace batida::cli
{

namespace
{
constexpr int kOk = 0, kFailed = 1, kUsage = 2;

// Arguments -----------------------------------------------------------------------

struct Args
{
    juce::StringArray positional;
    std::map<juce::String, juce::String> options; // --name value
    std::set<juce::String> flags;                 // --json, --loop...
    juce::String error;

    bool has (const juce::String& flag) const { return flags.count (flag) > 0; }
    bool hasOption (const juce::String& name) const { return options.count (name) > 0; }
    juce::String option (const juce::String& name, const juce::String& fallback = {}) const
    {
        const auto i = options.find (name);
        return i != options.end() ? i->second : fallback;
    }
};

Args parse (const juce::StringArray& raw, const juce::StringArray& valueOptions, const juce::StringArray& flagOptions)
{
    Args a;
    for (int i = 0; i < raw.size(); ++i)
    {
        auto word = raw[i];
        if (word == "-o")
            word = "--out";
        if (! word.startsWith ("--"))
        {
            a.positional.add (word);
            continue;
        }
        const auto name = word.substring (2);
        if (flagOptions.contains (name) || name == "json")
            a.flags.insert (name);
        else if (valueOptions.contains (name))
        {
            if (i + 1 >= raw.size())
                a.error = word + " needs a value";
            else
                a.options[name] = raw[++i];
        }
        else if (a.error.isEmpty())
            a.error = "unknown option " + word;
    }
    return a;
}

juce::File fileArg (const juce::String& path)
{
    if (path == "~" || path.startsWith ("~/"))
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (path.substring (2));
    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

void print (std::ostream& o, const juce::String& text) { o << text.toStdString(); }
void printLine (std::ostream& o, const juce::String& text = {}) { o << text.toStdString() << "\n"; }
void printJson (std::ostream& o, const juce::var& v) { o << juce::JSON::toString (v, juce::JSON::FormatOptions {}.withMaxDecimalPlaces (7)).toStdString() << "\n"; }

juce::String fixed (double v, int decimals)
{
    char buffer[48];
    std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, v);
    return buffer;
}

juce::String number (float v)
{
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%g", (double) v);
    return buffer;
}

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [k, v] : props)
        o->setProperty (k, v);
    return juce::var (o);
}

// Measures ----------------------------------------------------------------------------

double round2 (float v) { return std::round ((double) v * 100.0) / 100.0; } // double, so JSON shows -0.8, not -0.800000011920929

juce::var measuresJson (const AudioMeasures& m)
{
    juce::Array<juce::var> bands, onsets;
    for (int i = 0; i < AudioMeasures::kBands; ++i)
        bands.add (object ({ { "hz", (int) AudioMeasures::kBandHz[(size_t) i] }, { "db", round2 (m.bandsDb[(size_t) i]) } }));
    for (const auto& o : m.onsets)
        onsets.add (object ({ { "seconds", std::round (o.seconds * 1000.0) / 1000.0 }, { "strength", round2 (o.strength) } }));
    const auto& s = m.sound;
    const auto note = s.pitchHz > 0.0f && s.flatness < 0.5f ? noteNumberFor (s.pitchHz) : -1;
    return object ({
        { "seconds", std::round (m.seconds * 1000.0) / 1000.0 },
        { "sampleRate", m.sampleRate },
        { "channels", m.channels },
        { "finite", m.finite },
        { "silent", m.isSilent() },
        { "peakDb", round2 (m.peakDb) },
        { "truePeakDb", round2 (m.truePeakDb) },
        { "clippedSamples", m.clipped },
        { "loudnessLufs", round2 (m.loudnessLufs) },
        { "loudestLufs", round2 (m.loudestLufs) },
        { "hitLoudnessDb", round2 (s.loudnessDb) },
        { "crestDb", round2 (s.crestDb) },
        { "lengthMs", std::round ((double) s.lengthMs) },
        { "centroidHz", std::round ((double) s.centroidHz) },
        { "lowShare", round2 (s.low) },
        { "highShare", round2 (s.high) },
        { "flatness", round2 (s.flatness) },
        { "inharmonic", round2 (s.inharmonic) },
        { "pitchHz", note >= 0 ? juce::var (round2 (s.pitchHz)) : juce::var() },
        { "note", note >= 0 ? juce::var (note) : juce::var() },
        { "noteName", note >= 0 ? juce::var (noteName (note)) : juce::var() },
        { "widthPercent", round2 (m.widthPercent) },
        { "bands", bands },
        { "onsets", onsets },
    });
}

void printMeasures (std::ostream& o, const AudioMeasures& m)
{
    const auto& s = m.sound;
    printLine (o, "  length     " + fixed (m.seconds, 3) + " s (above -60 dB until " + fixed (s.lengthMs / 1000.0, 3) + " s)");
    printLine (o, "  peak       " + fixed (m.peakDb, 1) + " dBFS, true peak " + fixed (m.truePeakDb, 1) + " dBTP"
                  + (m.clipped > 0 ? ", " + juce::String (m.clipped) + " samples at full scale" : juce::String()));
    printLine (o, "  loudness   " + fixed (m.loudnessLufs, 1) + " LUFS integrated, " + fixed (m.loudestLufs, 1)
                  + " LUFS loudest 400 ms, hit " + fixed (s.loudnessDb, 1) + " dB (loudest 50 ms), crest " + fixed (s.crestDb, 1) + " dB");
    printLine (o, "  tone       centroid " + juce::String ((int) std::round (s.centroidHz)) + " Hz, " + juce::String ((int) std::round (s.low * 100.0f))
                  + "% below 150 Hz, " + juce::String ((int) std::round (s.high * 100.0f)) + "% above 6 kHz");
    juce::String bands;
    for (int i = 0; i < AudioMeasures::kBands; ++i)
    {
        const auto hz = AudioMeasures::kBandHz[(size_t) i];
        bands << (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz)) << " " << fixed (m.bandsDb[(size_t) i], 0) << "  ";
    }
    printLine (o, "  bands dB   " + bands.trimEnd());
    printLine (o, "  noise      flatness " + fixed (s.flatness, 2) + " (0 tonal .. 1 noise), inharmonic " + fixed (s.inharmonic, 2));
    if (s.pitchHz > 0.0f && s.flatness < 0.5f)
    {
        const auto note = noteNumberFor (s.pitchHz);
        printLine (o, "  pitch      " + fixed (s.pitchHz, 1) + " Hz, note " + juce::String (note) + " (" + noteName (note) + ")");
    }
    else
        printLine (o, "  pitch      none (noisy or unpitched)");
    if (m.channels == 2)
        printLine (o, "  width      " + fixed (m.widthPercent, 1) + "% side");
    juce::String times;
    int shown = 0;
    for (const auto& on : m.onsets)
        if (on.strength >= 0.2f && shown++ < 24)
            times << fixed (on.seconds, 3) << " ";
    printLine (o, "  onsets     " + juce::String (m.onsets.size()) + (times.isNotEmpty() ? " (strong: " + times.trimEnd() + (shown > 24 ? " ...)" : ")") : juce::String()));
}

juce::StringArray warningsFor (const AudioMeasures& m)
{
    juce::StringArray w;
    if (! m.finite)
        w.add ("the audio has NaN or infinite samples");
    else if (m.isSilent())
        w.add ("silent (peak below -80 dBFS)");
    if (m.clipped > 0)
        w.add (juce::String (m.clipped) + " samples at full scale (clipping)");
    else if (m.truePeakDb > 0.0f)
        w.add ("true peak over 0 dBTP");
    return w;
}

std::optional<juce::AudioBuffer<float>> readAudio (const juce::File& file, double& rate, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = file.existsAsFile() ? "can't read " + file.getFileName() + " as audio (WAV, AIFF, FLAC...)" : "not found: " + file.getFullPathName();
        return std::nullopt;
    }
    const auto frames = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    juce::AudioBuffer<float> audio ((int) std::min (2u, reader->numChannels), std::max (1, frames));
    audio.clear();
    reader->read (&audio, 0, frames, 0, true, audio.getNumChannels() > 1);
    rate = reader->sampleRate;
    return audio;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    const auto options = juce::AudioFormatWriterOptions {}.withSampleRate (rate).withNumChannels (audio.getNumChannels()).withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

// Files ----------------------------------------------------------------------------------

struct Loaded
{
    PresetType type = PresetType::Kit;
    KitPreset kit = defaultKitPreset();
    PatternBank bank;
    bool isSet = false;
    juce::String name;
};

std::optional<Loaded> loadAny (const juce::File& file, juce::String& error)
{
    const auto type = presetTypeOf (file);
    if (! type)
    {
        error = file.getFileName() + " isn't a Batida file (.batida-sound, -kit, -pattern or -set)";
        return std::nullopt;
    }
    Loaded l;
    l.type = *type;
    switch (*type)
    {
        case PresetType::Sound:
            if (const auto s = readSound (file, &error))
            {
                l.kit.voices[0] = s->params;
                l.kit.names[0] = s->info.name;
                l.kit.samples[0] = s->sample;
                l.name = s->info.name;
                return l;
            }
            break;
        case PresetType::Kit:
            if (const auto k = readKit (file, &error))
            {
                l.kit = *k;
                for (int g = 0; g < kNumGlobalParams; ++g)
                    if (! isKitGlobal (g))
                        l.kit.globals[(size_t) g] = globalParamSpecs()[(size_t) g].def;
                l.name = k->info.name;
                return l;
            }
            break;
        case PresetType::Pattern:
            if (const auto p = readPattern (file, &error))
            {
                l.bank.patterns[0] = p->pattern;
                l.name = p->info.name;
                return l;
            }
            break;
        case PresetType::Set:
            if (const auto s = readSet (file, &error))
            {
                l.kit = s->kit;
                l.bank = s->patterns;
                l.isSet = true;
                l.name = s->info.name;
                return l;
            }
            break;
    }
    return std::nullopt;
}

// help --------------------------------------------------------------------------------------

const std::map<juce::String, juce::String>& helpTexts()
{
    static const std::map<juce::String, juce::String> texts {
        { "render",
          "batida render <file> [-o out.wav] [options]\n"
          "  Renders a sound, kit, pattern or set offline, through the whole chain, to a 24-bit WAV,\n"
          "  and prints what analyze would. Exits 1 if the render is silent or not finite.\n"
          "  A sound plays one hit in slot 1 of the Neutral kit. A kit plays --sound N or --pattern-file.\n"
          "  A pattern plays with the Neutral kit, or --kit. A set plays its pattern (or --pattern N, --sound N).\n"
          "  -o, --out <file>        where to write (default: <name>.wav here)\n"
          "  --sound <1-8>           one hit of that slot\n"
          "  --note <0-127>          the hit's note; 60 plays the sound at its own pitch (default 60)\n"
          "  --velocity <1-127>      the hit's velocity (default 127)\n"
          "  --pattern <1-16>        a set's pattern (default: the set's current pattern)\n"
          "  --pattern-file <file>   a pattern file to play with a kit or set\n"
          "  --kit <file>            the kit a pattern file plays (default: Neutral)\n"
          "  --bars <n>              play n bars (16 steps each) from the start, then let the tails ring\n"
          "                          (default: one pass of the pattern)\n"
          "  --loop                  one pass that loops seamlessly (tails wrap to the start)\n"
          "  --bpm <tempo>           default: the set's seq_tempo, else 120\n"
          "  --xy <x,y>              the pad, 0..1 each (default: the file's)\n"
          "  --rate <hz>             sample rate (default 48000)\n" },
        { "analyze",
          "batida analyze <file.wav> [--against <other.wav>]\n"
          "  Measures any audio file: peak and true peak, loudness (LUFS), length, tone (centroid,\n"
          "  low and high shares, octave bands), noise (flatness, inharmonicity), pitch, stereo width\n"
          "  and onsets. --against also measures a second file and prints the differences.\n" },
        { "validate",
          "batida validate <file>...\n"
          "  Checks Batida files for everything loading lets slide: unknown settings (with the nearest\n"
          "  name), values out of range, unknown menu choices, bad pattern lanes, missing samples.\n"
          "  Exits 1 if any file has a problem.\n" },
        { "describe",
          "batida describe <file>\n"
          "  A plain summary: a sound's settings that differ from the defaults, a kit's slots and chain,\n"
          "  a pattern as a text grid (x hit, X accent, . rest).\n" },
        { "new",
          "batida new sound|kit|pattern|set <name> [-o <folder>] [--force]\n"
          "  Writes a file to start from (the same as Init in the plugin), into the library's User\n"
          "  folder unless -o says where. --force replaces an existing file.\n" },
        { "list",
          "batida list [--type sound|kit|pattern|set] [--category <c>] [--tag <t>] [--search <words>]\n"
          "            [--user | --factory]\n"
          "  The library (~/Music/Oddfield/Batida, or $BATIDA_LIBRARY): type, source, name, category,\n"
          "  tags and path of every file.\n" },
        { "params",
          "batida params [--json | --markdown]\n"
          "  Every setting: its key, range, unit, default, choices and what it does. --markdown prints\n"
          "  the whole file reference (docs/REFERENCE.md).\n" },
    };
    return texts;
}

int help (std::ostream& out, const juce::String& command)
{
    if (const auto i = helpTexts().find (command); i != helpTexts().end())
    {
        print (out, i->second);
        return kOk;
    }
    printLine (out, "batida " BATIDA_VERSION ": Batida's files and engine, without a DAW.");
    printLine (out);
    printLine (out, "  batida render <file> -o out.wav    render a sound, kit, pattern or set to WAV");
    printLine (out, "  batida analyze <file.wav>          measure audio (loudness, tone, pitch, onsets...)");
    printLine (out, "  batida validate <file>...          check files for typos and out-of-range values");
    printLine (out, "  batida describe <file>             summarise a file; patterns as a text grid");
    printLine (out, "  batida new <type> <name>           write a file to start from");
    printLine (out, "  batida list                        the library");
    printLine (out, "  batida params                      every setting, its range and unit");
    printLine (out);
    printLine (out, "Add --json to any command for output another program can read.");
    printLine (out, "batida help <command> shows its options. Files and settings: docs/REFERENCE.md,");
    printLine (out, juce::String ("or ") + kReferenceUrl);
    return command.isEmpty() ? kOk : kUsage;
}

int usage (std::ostream& err, const juce::String& command, const juce::String& problem)
{
    printLine (err, "batida " + command + ": " + problem);
    printLine (err, "(batida help " + command + " for its options)");
    return kUsage;
}

// render ----------------------------------------------------------------------------------------

int render (const juce::StringArray& raw, std::ostream& out, std::ostream& err)
{
    const auto a = parse (raw, { "out", "sound", "note", "velocity", "pattern", "pattern-file", "kit", "bars", "bpm", "xy", "rate" }, { "loop" });
    if (a.error.isNotEmpty())
        return usage (err, "render", a.error);
    if (a.positional.size() != 1)
        return usage (err, "render", "give one file to render");

    const auto file = fileArg (a.positional[0]);
    juce::String error;
    auto loaded = loadAny (file, error);
    if (! loaded)
    {
        printLine (err, "batida render: " + error);
        return kUsage;
    }
    auto& l = *loaded;

    if (a.hasOption ("kit"))
    {
        if (l.type != PresetType::Pattern)
            return usage (err, "render", "--kit is for pattern files");
        const auto k = readKit (fileArg (a.option ("kit")), &error);
        if (! k)
        {
            printLine (err, "batida render: " + error);
            return kUsage;
        }
        const auto bank = l.bank;
        l.kit = *k;
        l.bank = bank;
    }
    if (a.hasOption ("pattern-file"))
    {
        if (l.type != PresetType::Kit && l.type != PresetType::Set)
            return usage (err, "render", "--pattern-file is for kit and set files");
        const auto p = readPattern (fileArg (a.option ("pattern-file")), &error);
        if (! p)
        {
            printLine (err, "batida render: " + error);
            return kUsage;
        }
        l.bank = {};
        l.bank.patterns[0] = p->pattern;
    }

    RenderRequest req;
    req.sampleRate = a.option ("rate", "48000").getDoubleValue();
    if (req.sampleRate < 22050.0 || req.sampleRate > 192000.0)
        return usage (err, "render", "--rate must be 22050..192000");
    req.params.voices = l.kit.voices;
    req.params.global = l.kit.globals;
    req.movement = l.kit.movement;
    req.bank = l.bank;

    if (a.hasOption ("xy"))
    {
        juce::StringArray xy;
        xy.addTokens (a.option ("xy"), ",", "");
        if (xy.size() != 2)
            return usage (err, "render", "--xy takes x,y (0..1 each)");
        req.params.global[gp::XyX] = juce::jlimit (0.0f, 1.0f, xy[0].getFloatValue());
        req.params.global[gp::XyY] = juce::jlimit (0.0f, 1.0f, xy[1].getFloatValue());
    }

    // What to play.
    const auto hasPattern = l.type == PresetType::Pattern || l.type == PresetType::Set || a.hasOption ("pattern-file");
    if (l.type == PresetType::Sound || a.hasOption ("sound") || ! hasPattern)
    {
        if (l.type == PresetType::Kit && ! a.hasOption ("sound"))
            return usage (err, "render", "a kit needs --sound <1-8> or --pattern-file <file>");
        req.mode = RenderRequest::Mode::Sound;
        req.voice = l.type == PresetType::Sound ? 0 : a.option ("sound").getIntValue() - 1;
        if (req.voice < 0 || req.voice >= kNumVoices)
            return usage (err, "render", "--sound must be 1..8");
        req.key = juce::jlimit (0, 127, a.option ("note", "60").getIntValue());
        req.velocity = (float) juce::jlimit (1, 127, a.option ("velocity", "127").getIntValue()) / 127.0f;
    }
    else
    {
        req.mode = RenderRequest::Mode::Pattern;
        req.pattern = a.hasOption ("pattern") ? a.option ("pattern").getIntValue() - 1
                      : l.isSet && ! a.hasOption ("pattern-file") ? juce::jlimit (0, kNumPatterns - 1, (int) (l.kit.globals[gp::SeqPattern] + 0.5f))
                                                                  : 0;
        if (req.pattern < 0 || req.pattern >= kNumPatterns)
            return usage (err, "render", "--pattern must be 1..16");
        const auto& pattern = req.bank.patterns[(size_t) req.pattern];
        if (pattern.isEmpty())
        {
            printLine (err, "batida render: pattern " + juce::String (req.pattern + 1) + " is empty");
            return kFailed;
        }
        req.bpm = a.hasOption ("bpm") ? a.option ("bpm").getDoubleValue() : l.isSet ? (double) l.kit.globals[gp::SeqTempo] : 120.0;
        if (req.bpm < 20.0 || req.bpm > 400.0)
            return usage (err, "render", "--bpm must be 20..400");
        if (a.has ("loop"))
            req.steps = 0;
        else if (a.hasOption ("bars"))
            req.steps = std::max (1, a.option ("bars").getIntValue()) * 16;
        else
            req.steps = pattern.length;
    }

    // Samples, decoded as the plugin decodes them.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::StringArray warnings;
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto& ref = l.kit.samples[(size_t) v];
        if (ref.isEmpty())
            continue;
        juce::String note;
        if (auto data = SampleSlot::decode (juce::File (ref.path), formats, note))
            req.samples[(size_t) v] = std::shared_ptr<const SampleData> (std::move (data));
        else
            warnings.add ("slot " + juce::String (v + 1) + ": sample not found or unreadable: " + ref.path);
    }

    const auto audio = renderKit (req);
    const auto outFile = a.hasOption ("out") ? fileArg (a.option ("out"))
                                             : juce::File::getCurrentWorkingDirectory().getChildFile (safeFileName (l.name.isNotEmpty() ? l.name : file.getFileNameWithoutExtension()) + ".wav");
    if (! writeWav (outFile, audio, req.sampleRate))
    {
        printLine (err, "batida render: can't write " + outFile.getFullPathName());
        return kUsage;
    }

    const auto m = measureAudio (audio, req.sampleRate);
    warnings.addArray (warningsFor (m));
    const auto failed = ! m.finite || m.isSilent();
    if (a.has ("json"))
    {
        juce::Array<juce::var> w;
        for (const auto& s : warnings)
            w.add (s);
        printJson (out, object ({ { "file", file.getFullPathName() }, { "out", outFile.getFullPathName() },
                                  { "what", req.mode == RenderRequest::Mode::Sound ? "sound " + juce::String (req.voice + 1)
                                                                                  : "pattern " + juce::String (req.pattern + 1) },
                                  { "bpm", req.mode == RenderRequest::Mode::Pattern ? juce::var (req.bpm) : juce::var() },
                                  { "measures", measuresJson (m) }, { "warnings", w } }));
    }
    else
    {
        printLine (out, outFile.getFullPathName());
        printLine (out, "  rendered   " + (req.mode == RenderRequest::Mode::Sound ? "slot " + juce::String (req.voice + 1) + ", note " + juce::String (req.key)
                                                                               : "pattern " + juce::String (req.pattern + 1) + " at " + number ((float) req.bpm) + " bpm"
                                                                                     + (req.steps > 0 ? ", " + juce::String (req.steps) + " steps" : juce::String (", looped"))));
        printMeasures (out, m);
        for (const auto& w : warnings)
            printLine (out, "  warning    " + w);
    }
    return failed ? kFailed : kOk;
}

// analyze -----------------------------------------------------------------------------------------

int analyze (const juce::StringArray& raw, std::ostream& out, std::ostream& err)
{
    const auto a = parse (raw, { "against" }, {});
    if (a.error.isNotEmpty())
        return usage (err, "analyze", a.error);
    if (a.positional.size() != 1)
        return usage (err, "analyze", "give one audio file");

    auto measure = [&] (const juce::String& path, std::optional<AudioMeasures>& result) -> bool
    {
        double rate = 48000.0;
        juce::String error;
        const auto audio = readAudio (fileArg (path), rate, error);
        if (! audio)
        {
            printLine (err, "batida analyze: " + error);
            return false;
        }
        result = measureAudio (*audio, rate);
        return true;
    };
    std::optional<AudioMeasures> m, other;
    if (! measure (a.positional[0], m) || (a.hasOption ("against") && ! measure (a.option ("against"), other)))
        return kUsage;

    // Differences: this file minus the other.
    struct Diff { const char* key; const char* label; float value; const char* unit; };
    std::vector<Diff> diffs;
    if (other)
    {
        const auto &x = *m, &y = *other;
        diffs = { { "loudnessLufs", "loudness", x.loudnessLufs - y.loudnessLufs, "LU" },
                  { "peakDb", "peak", x.peakDb - y.peakDb, "dB" },
                  { "lengthMs", "length", x.sound.lengthMs - y.sound.lengthMs, "ms" },
                  { "centroidOctaves", "brightness", (float) std::log2 ((x.sound.centroidHz + 1.0f) / (y.sound.centroidHz + 1.0f)), "octaves" },
                  { "lowShare", "below 150 Hz", 100.0f * (x.sound.low - y.sound.low), "points" },
                  { "highShare", "above 6 kHz", 100.0f * (x.sound.high - y.sound.high), "points" },
                  { "flatness", "flatness", x.sound.flatness - y.sound.flatness, "" },
                  { "widthPercent", "width", x.widthPercent - y.widthPercent, "points" } };
        for (int i = 0; i < AudioMeasures::kBands; ++i)
        {
            static const char* keys[] { "band63", "band125", "band250", "band500", "band1k", "band2k", "band4k", "band8k" };
            diffs.push_back ({ keys[i], keys[i], x.bandsDb[(size_t) i] - y.bandsDb[(size_t) i], "dB" });
        }
    }

    if (a.has ("json"))
    {
        auto result = object ({ { "file", fileArg (a.positional[0]).getFullPathName() }, { "measures", measuresJson (*m) } });
        if (other)
        {
            auto* d = new juce::DynamicObject();
            for (const auto& diff : diffs)
                d->setProperty (diff.key, round2 (diff.value));
            result.getDynamicObject()->setProperty ("against", fileArg (a.option ("against")).getFullPathName());
            result.getDynamicObject()->setProperty ("againstMeasures", measuresJson (*other));
            result.getDynamicObject()->setProperty ("difference", juce::var (d));
        }
        printJson (out, result);
    }
    else
    {
        printLine (out, fileArg (a.positional[0]).getFullPathName());
        printMeasures (out, *m);
        for (const auto& w : warningsFor (*m))
            printLine (out, "  warning    " + w);
        if (other)
        {
            printLine (out);
            printLine (out, "against " + fileArg (a.option ("against")).getFullPathName() + " (this minus that)");
            for (const auto& d : diffs)
                printLine (out, "  " + juce::String (d.label).paddedRight (' ', 13) + (d.value >= 0.0f ? "+" : "") + fixed (d.value, 2) + " " + d.unit);
        }
    }
    return kOk;
}

// validate ------------------------------------------------------------------------------------------

int validate (const juce::StringArray& raw, std::ostream& out, std::ostream& err)
{
    const auto a = parse (raw, {}, {});
    if (a.error.isNotEmpty())
        return usage (err, "validate", a.error);
    if (a.positional.isEmpty())
        return usage (err, "validate", "give one or more files");

    bool allClean = true;
    juce::Array<juce::var> results;
    for (const auto& path : a.positional)
    {
        const auto file = fileArg (path);
        const auto v = validateFile (file);
        allClean = allClean && v.clean();
        if (a.has ("json"))
        {
            juce::Array<juce::var> problems;
            for (const auto& p : v.problems)
                problems.add (object ({ { "where", p.where }, { "problem", p.what }, { "fatal", p.fatal } }));
            results.add (object ({ { "file", file.getFullPathName() }, { "type", v.type ? juce::var (typeName (*v.type)) : juce::var() },
                                   { "loads", v.loads() }, { "problems", problems } }));
        }
        else if (v.clean())
            printLine (out, file.getFullPathName() + ": ok");
        else
            for (const auto& p : v.problems)
                printLine (out, file.getFullPathName() + ": " + p.where + ": " + p.what);
    }
    if (a.has ("json"))
        printJson (out, results);
    return allClean ? kOk : kFailed;
}

// describe ------------------------------------------------------------------------------------------

juce::String valueText (const ParamSpec& spec, float v)
{
    if (spec.kind == Kind::Choice)
        return spec.choices[(size_t) std::clamp ((int) (v + 0.5f), 0, (int) spec.choices.size() - 1)];
    if (spec.kind == Kind::Bool)
        return v > 0.5f ? "on" : "off";
    const auto unit = unitLabel (spec);
    return number (std::round (v * 1000.0f) / 1000.0f) + (unit.isNotEmpty() ? " " + unit : juce::String());
}

// A sound's settings that differ from the defaults, as "key value" pairs.
std::vector<std::pair<juce::String, juce::String>> changedVoice (const VoiceParams& p)
{
    std::vector<std::pair<juce::String, juce::String>> out;
    const auto mode = (SourceMode) p.choice (vp::SrcMode);
    for (int k = 0; k < kNumVoiceParams; ++k)
    {
        const auto& spec = voiceParamSpecs()[(size_t) k];
        if (std::abs (p[k] - spec.def) < 1.0e-6f)
            continue;
        const auto fm = k >= vp::FmPitch && k <= vp::FmGrit;
        const auto sample = k >= vp::SmpTune && k <= vp::SmpSliceSens;
        if ((fm || k >= vp::OpBase) && mode == SourceMode::Sample)
            continue; // not heard
        if (sample && mode == SourceMode::FM)
            continue;
        out.push_back ({ spec.key, valueText (spec, p[k]) });
    }
    return out;
}

juce::String sourceText (const VoiceParams& p, const SampleRef& sample)
{
    const auto mode = (SourceMode) p.choice (vp::SrcMode);
    juce::String s = voiceParamSpecs()[vp::SrcMode].choices[(size_t) p.choice (vp::SrcMode)];
    if (mode != SourceMode::Sample)
        s << " (algorithm " << (p.choice (vp::FmAlgo) + 1) << ")";
    if (mode != SourceMode::FM && ! sample.isEmpty())
        s << ", sample " << juce::File (sample.path).getFileName() << (juce::File (sample.path).existsAsFile() ? "" : " (missing)");
    return s;
}

juce::String gridRow (const Pattern& pat, int track)
{
    juce::String row;
    const auto& tr = pat.tracks[(size_t) track];
    for (int s = 0; s < pat.length; ++s)
    {
        if (s > 0 && s % 4 == 0)
            row << " ";
        const auto& st = tr.steps[(size_t) s];
        row << (s >= std::min (tr.length, pat.length) ? juce::String ("~") : ! st.gate ? juce::String (".") : st.velocity >= 110 ? juce::String ("X") : juce::String ("x"));
    }
    return row;
}

juce::var patternJson (const Pattern& pat, int index, const std::array<juce::String, kNumVoices>& names)
{
    juce::Array<juce::var> tracks;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tr = pat.tracks[(size_t) t];
        juce::Array<juce::var> steps;
        for (int s = 0; s < pat.length; ++s)
            if (const auto& st = tr.steps[(size_t) s]; st.gate)
                steps.add (object ({ { "step", s + 1 }, { "velocity", (int) st.velocity }, { "pitch", (int) st.pitch }, { "slice", (int) st.slice },
                                     { "ratchet", (int) st.ratchet }, { "probability", (int) st.probability } }));
        if (! steps.isEmpty())
            tracks.add (object ({ { "slot", t + 1 }, { "name", names[(size_t) t] }, { "length", std::min (tr.length, pat.length) },
                                  { "grid", gridRow (pat, t).removeCharacters (" ") }, { "hits", steps } }));
    }
    juce::Array<juce::var> xy;
    for (int s = 0; s < pat.length; ++s)
        if (pat.xy[(size_t) s].active)
            xy.add (object ({ { "step", s + 1 }, { "x", round2 (pat.xy[(size_t) s].x) }, { "y", round2 (pat.xy[(size_t) s].y) } }));
    return object ({ { "pattern", index + 1 }, { "length", pat.length }, { "tracks", tracks }, { "xy", xy } });
}

void printPattern (std::ostream& out, const Pattern& pat, int index, const std::array<juce::String, kNumVoices>& names)
{
    printLine (out, "pattern " + juce::String (index + 1) + ", " + juce::String (pat.length) + " steps (x hit, X accent, . rest, ~ past the track's length)");
    juce::String ruler;
    for (int s = 0; s < pat.length; s += 4)
        ruler << juce::String (s + 1).paddedRight (' ', 5);
    printLine (out, "  " + juce::String().paddedRight (' ', 14) + ruler.trimEnd());
    for (int t = 0; t < kNumTracks; ++t)
    {
        bool any = false;
        for (int s = 0; s < pat.length; ++s)
            any = any || pat.tracks[(size_t) t].steps[(size_t) s].gate;
        if (any)
            printLine (out, "  " + (juce::String (t + 1) + " " + names[(size_t) t]).substring (0, 13).paddedRight (' ', 14) + gridRow (pat, t));
    }
    juce::String xy;
    for (int s = 0; s < pat.length; ++s)
        if (pat.xy[(size_t) s].active)
            xy << (s + 1) << ":" << fixed (pat.xy[(size_t) s].x, 2) << "," << fixed (pat.xy[(size_t) s].y, 2) << " ";
    if (xy.isNotEmpty())
        printLine (out, "  xy lane       " + xy.trimEnd());
}

int describe (const juce::StringArray& raw, std::ostream& out, std::ostream& err)
{
    const auto a = parse (raw, {}, {});
    if (a.error.isNotEmpty())
        return usage (err, "describe", a.error);
    if (a.positional.size() != 1)
        return usage (err, "describe", "give one file");
    const auto file = fileArg (a.positional[0]);
    juce::String error;
    const auto loaded = loadAny (file, error);
    if (! loaded)
    {
        printLine (err, "batida describe: " + error);
        return kUsage;
    }
    const auto& l = *loaded;
    const auto json = a.has ("json");
    const auto info = readInfo (file).value_or (PresetInfo {});

    auto voiceJson = [&] (int v)
    {
        auto* changed = new juce::DynamicObject();
        for (const auto& [k, val] : changedVoice (l.kit.voices[(size_t) v]))
            changed->setProperty (k, val);
        return object ({ { "slot", v + 1 }, { "name", l.kit.names[(size_t) v] },
                         { "source", sourceText (l.kit.voices[(size_t) v], l.kit.samples[(size_t) v]) },
                         { "changed", juce::var (changed) } });
    };
    auto changedGlobals = [&] (bool kitOnly)
    {
        std::vector<std::pair<juce::String, juce::String>> c;
        for (int g = 0; g < kNumGlobalParams; ++g)
        {
            const auto& spec = globalParamSpecs()[(size_t) g];
            if ((kitOnly && ! isKitGlobal (g)) || std::abs (l.kit.globals[(size_t) g] - spec.def) < 1.0e-6f)
                continue;
            c.push_back ({ spec.key, valueText (spec, l.kit.globals[(size_t) g]) });
        }
        return c;
    };
    auto pairsText = [] (const std::vector<std::pair<juce::String, juce::String>>& pairs)
    {
        juce::StringArray s;
        for (const auto& [k, v] : pairs)
            s.add (k + " " + v);
        return s.isEmpty() ? juce::String ("defaults") : s.joinIntoString (", ");
    };

    juce::var result = object ({ { "file", file.getFullPathName() }, { "type", typeName (l.type) }, { "name", info.name },
                                 { "author", info.author }, { "category", info.category }, { "tags", info.tags.joinIntoString (",") } });
    auto* r = result.getDynamicObject();
    if (! json)
        printLine (out, juce::String (typeName (l.type)) + " \"" + info.name + "\"" + (info.author.isNotEmpty() ? " by " + info.author : juce::String())
                            + (info.category.isNotEmpty() ? ", " + info.category : juce::String())
                            + (info.tags.isEmpty() ? juce::String() : " [" + info.tags.joinIntoString (", ") + "]"));

    if (l.type == PresetType::Sound)
    {
        if (json)
        {
            const auto v = voiceJson (0);
            r->setProperty ("source", v["source"]);
            r->setProperty ("changed", v["changed"]);
        }
        else
        {
            printLine (out, "  source   " + sourceText (l.kit.voices[0], l.kit.samples[0]));
            printLine (out, "  changed  " + pairsText (changedVoice (l.kit.voices[0])));
        }
    }
    if (l.type == PresetType::Kit || l.type == PresetType::Set)
    {
        juce::Array<juce::var> slots;
        for (int v = 0; v < kNumVoices; ++v)
        {
            if (json)
                slots.add (voiceJson (v));
            else
                printLine (out, "  slot " + juce::String (v + 1) + "  " + l.kit.names[(size_t) v].paddedRight (' ', 14) + sourceText (l.kit.voices[(size_t) v], l.kit.samples[(size_t) v])
                                    + ": " + pairsText (changedVoice (l.kit.voices[(size_t) v])));
        }
        const auto chain = changedGlobals (l.type == PresetType::Kit);
        if (json)
        {
            auto* c = new juce::DynamicObject();
            for (const auto& [k, v] : chain)
                c->setProperty (k, v);
            r->setProperty ("slots", slots);
            r->setProperty ("kit", juce::var (c));
        }
        else
            printLine (out, "  kit      " + pairsText (chain));

        juce::Array<juce::var> mods;
        for (int m = 0; m < kNumMods; ++m)
            for (const auto& t : l.kit.movement.mods[(size_t) m].targets)
                if (t.active)
                {
                    const auto target = t.global ? juce::String (globalParamID (t.param))
                                                 : "slot " + juce::String (t.voice + 1) + " " + juce::String (voiceParamSpecs()[(size_t) t.param].key);
                    if (json)
                        mods.add (object ({ { "mod", m + 1 }, { "target", target }, { "depth", round2 (t.depth) } }));
                    else
                        printLine (out, "  mod " + juce::String (m + 1) + "    -> " + target + " depth " + fixed (t.depth, 2));
                }
        if (json)
            r->setProperty ("modulation", mods);
    }
    if (l.type == PresetType::Pattern || l.type == PresetType::Set)
    {
        juce::Array<juce::var> patterns;
        for (int p = 0; p < kNumPatterns; ++p)
        {
            const auto& pat = l.bank.patterns[(size_t) p];
            if (pat.isEmpty() && (l.type == PresetType::Set || p > 0))
                continue;
            if (json)
                patterns.add (patternJson (pat, p, l.kit.names));
            else
                printPattern (out, pat, p, l.kit.names);
        }
        if (json)
            r->setProperty ("patterns", patterns);
    }
    if (json)
        printJson (out, result);
    return kOk;
}

// new ---------------------------------------------------------------------------------------------------

int newFile (const juce::StringArray& raw, std::ostream& out, std::ostream& err)
{
    const auto a = parse (raw, { "out" }, { "force" });
    if (a.error.isNotEmpty())
        return usage (err, "new", a.error);
    if (a.positional.size() != 2)
        return usage (err, "new", "give a type (sound, kit, pattern or set) and a name");
    std::optional<PresetType> type;
    for (auto t : { PresetType::Sound, PresetType::Kit, PresetType::Pattern, PresetType::Set })
        if (a.positional[0].equalsIgnoreCase (typeName (t)))
            type = t;
    if (! type)
        return usage (err, "new", "the type is sound, kit, pattern or set");

    PresetInfo info;
    info.name = a.positional[1].trim();
    juce::File target;
    if (a.hasOption ("out"))
        target = fileArg (a.option ("out")).getChildFile (safeFileName (info.name) + extensionFor (*type));
    else
    {
        Library lib;
        target = lib.userFileFor (*type, info);
    }
    if (target.exists() && ! a.has ("force"))
    {
        printLine (err, "batida new: " + target.getFullPathName() + " already exists (--force replaces it)");
        return kFailed;
    }

    juce::String error;
    bool ok = false;
    switch (*type)
    {
        case PresetType::Sound: { auto s = initSoundPreset(); s.info = info; ok = writeSound (target, s, &error); break; }
        case PresetType::Kit:   { auto k = defaultKitPreset(); k.info = info; ok = writeKit (target, k, &error); break; }
        case PresetType::Pattern: { PatternPreset p; p.info = info; ok = writePattern (target, p, &error); break; }
        case PresetType::Set:
        {
            SetPreset s;
            s.kit = defaultKitPreset();
            s.info = info;
            s.kit.info = info;
            s.patterns = defaultPatternBank();
            ok = writeSet (target, s, &error);
            break;
        }
    }
    if (! ok)
    {
        printLine (err, "batida new: " + error);
        return kUsage;
    }
    if (a.has ("json"))
        printJson (out, object ({ { "file", target.getFullPathName() }, { "type", typeName (*type) } }));
    else
        printLine (out, target.getFullPathName());
    return kOk;
}

// list ----------------------------------------------------------------------------------------------------

int list (const juce::StringArray& raw, std::ostream& out, std::ostream& err)
{
    const auto a = parse (raw, { "type", "category", "tag", "search" }, { "user", "factory" });
    if (a.error.isNotEmpty())
        return usage (err, "list", a.error);

    juce::MessageManager::getInstance(); // the library's change messages need one
    Library lib;
    lib.scanNow();

    std::optional<PresetType> type;
    if (a.hasOption ("type"))
    {
        for (auto t : { PresetType::Sound, PresetType::Kit, PresetType::Pattern, PresetType::Set })
            if (a.option ("type").equalsIgnoreCase (typeName (t)))
                type = t;
        if (! type)
            return usage (err, "list", "--type is sound, kit, pattern or set");
    }
    juce::StringArray words;
    words.addTokens (a.option ("search").toLowerCase(), " ", "\"");
    words.removeEmptyStrings();

    juce::Array<juce::var> results;
    int count = 0;
    for (const auto& e : lib.getEntries())
    {
        if ((type && e.type != *type) || (a.has ("user") && e.factory) || (a.has ("factory") && ! e.factory) || e.review)
            continue;
        if (a.hasOption ("category") && ! e.info.category.equalsIgnoreCase (a.option ("category")))
            continue;
        if (a.hasOption ("tag") && ! e.info.tags.contains (a.option ("tag"), true))
            continue;
        const auto haystack = (e.info.name + " " + e.info.tags.joinIntoString (" ") + " " + e.info.category + " " + e.info.author).toLowerCase();
        bool found = true;
        for (const auto& w : words)
            found = found && haystack.contains (w);
        if (! found)
            continue;
        ++count;
        if (a.has ("json"))
            results.add (object ({ { "type", typeName (e.type) }, { "source", e.factory ? "factory" : "user" }, { "name", e.info.name },
                                   { "category", e.info.category }, { "tags", e.info.tags.joinIntoString (",") }, { "file", e.file.getFullPathName() } }));
        else
            printLine (out, juce::String (typeName (e.type)).paddedRight (' ', 8) + (e.factory ? "factory " : "user    ")
                                + e.info.name.paddedRight (' ', 24) + " " + e.info.category.paddedRight (' ', 8)
                                + juce::String ("[" + e.info.tags.joinIntoString (",") + "]").paddedRight (' ', 22) + " " + e.file.getFullPathName());
    }
    if (a.has ("json"))
        printJson (out, results);
    else if (count == 0)
        printLine (out, "nothing found in " + lib.getRoot().getFullPathName());
    return kOk;
}

// params ----------------------------------------------------------------------------------------------------

int params (const juce::StringArray& raw, std::ostream& out, std::ostream& err)
{
    const auto a = parse (raw, {}, { "markdown" });
    if (a.error.isNotEmpty())
        return usage (err, "params", a.error);
    if (a.has ("markdown"))
    {
        print (out, referenceMarkdown());
        return kOk;
    }
    const auto data = parameterData();
    if (a.has ("json"))
    {
        printJson (out, data);
        return kOk;
    }
    for (const auto& p : *data.getArray())
    {
        juce::String range;
        if (p["kind"] == juce::var ("menu"))
        {
            juce::StringArray c;
            for (const auto& choice : *p["choices"].getArray())
                c.add (choice.toString());
            range = c.joinIntoString (" | ");
        }
        else if (p["kind"] == juce::var ("switch"))
            range = "0 or 1";
        else
            range = number ((float) p["min"]) + ".." + number ((float) p["max"]) + (p["unit"].toString().isNotEmpty() ? " " + p["unit"].toString() : juce::String());
        printLine (out, p["element"].toString().paddedRight (' ', 6) + p["key"].toString().paddedRight (' ', 18) + range + " (default "
                            + p["default"].toString() + "): " + p["description"].toString());
    }
    return kOk;
}
} // namespace

int run (const juce::StringArray& args, std::ostream& out, std::ostream& err)
{
    const auto command = args.isEmpty() ? juce::String() : args[0];
    const juce::StringArray rest (args.begin() + std::min (1, args.size()), std::max (0, args.size() - 1));

    if (command.isEmpty() || command == "help" || command == "--help" || command == "-h")
        return help (out, rest.isEmpty() ? juce::String() : rest[0]);
    if (command == "--version" || command == "version")
    {
        printLine (out, "batida " BATIDA_VERSION);
        return kOk;
    }
    if (rest.contains ("--help") || rest.contains ("-h"))
        return help (out, command);

    if (command == "render")   return render (rest, out, err);
    if (command == "analyze" || command == "analyse") return analyze (rest, out, err);
    if (command == "validate") return validate (rest, out, err);
    if (command == "describe") return describe (rest, out, err);
    if (command == "new")      return newFile (rest, out, err);
    if (command == "list")     return list (rest, out, err);
    if (command == "params")   return params (rest, out, err);

    printLine (err, "batida: unknown command \"" + command + "\"");
    help (err, {});
    return kUsage;
}

} // namespace batida::cli
