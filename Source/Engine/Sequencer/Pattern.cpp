#include "Pattern.h"

namespace batida
{

bool Pattern::isEmpty() const
{
    for (const auto& t : tracks)
        for (const auto& s : t.steps)
            if (s.gate)
                return false;
    for (const auto& l : xy)
        if (l.active)
            return false;
    return true;
}

void Pattern::clear()
{
    *this = Pattern {};
}

// XML: one <PATTERN> per non-empty pattern; each track stores its lanes as
// comma-separated lists, so saved projects stay small and readable.
std::unique_ptr<juce::XmlElement> PatternBank::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("PATTERNS");

    for (int p = 0; p < kNumPatterns; ++p)
    {
        const auto& pat = patterns[(size_t) p];
        if (pat.isEmpty() && pat.length == 16)
            continue;

        auto* pe = root->createNewChildElement ("PATTERN");
        pe->setAttribute ("index", p);
        pe->setAttribute ("length", pat.length);

        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto& tr = pat.tracks[(size_t) t];
            juce::StringArray gate, vel, pitch, slice, ratchet, prob;
            for (const auto& s : tr.steps)
            {
                gate.add (s.gate ? "1" : "0");
                vel.add (juce::String (s.velocity));
                pitch.add (juce::String (s.pitch));
                slice.add (juce::String (s.slice));
                ratchet.add (juce::String (s.ratchet));
                prob.add (juce::String (s.probability));
            }
            auto* te = pe->createNewChildElement ("TRACK");
            te->setAttribute ("voice", t);
            te->setAttribute ("length", tr.length);
            te->setAttribute ("noteLength", tr.noteLength);
            te->setAttribute ("gate", gate.joinIntoString (""));
            te->setAttribute ("velocity", vel.joinIntoString (","));
            te->setAttribute ("pitch", pitch.joinIntoString (","));
            te->setAttribute ("slice", slice.joinIntoString (","));
            te->setAttribute ("ratchet", ratchet.joinIntoString (","));
            te->setAttribute ("probability", prob.joinIntoString (","));
        }

        juce::StringArray xy;
        for (const auto& l : pat.xy)
            xy.add (l.active ? juce::String (l.x, 3) + ":" + juce::String (l.y, 3) : juce::String ("-"));
        pe->setAttribute ("xy", xy.joinIntoString (","));
    }
    return root;
}

void PatternBank::fromXml (const juce::XmlElement& xml)
{
    *this = PatternBank {};

    for (auto* pe : xml.getChildWithTagNameIterator ("PATTERN"))
    {
        const auto p = pe->getIntAttribute ("index", -1);
        if (p < 0 || p >= kNumPatterns)
            continue;

        auto& pat = patterns[(size_t) p];
        pat.length = juce::jlimit (kMinPatternSteps, kMaxSteps, pe->getIntAttribute ("length", 16));

        for (auto* te : pe->getChildWithTagNameIterator ("TRACK"))
        {
            const auto t = te->getIntAttribute ("voice", -1);
            if (t < 0 || t >= kNumTracks)
                continue;

            auto& tr = pat.tracks[(size_t) t];
            tr.length = juce::jlimit (1, kMaxSteps, te->getIntAttribute ("length", pat.length));
            tr.noteLength = (float) juce::jlimit (0.05, 1.0, te->getDoubleAttribute ("noteLength", 0.5));

            const auto gate = te->getStringAttribute ("gate");
            auto list = [&] (const char* name)
            {
                juce::StringArray a;
                a.addTokens (te->getStringAttribute (name), ",", "");
                return a;
            };
            const auto vel = list ("velocity"), pitch = list ("pitch"), slice = list ("slice"),
                       ratchet = list ("ratchet"), prob = list ("probability");

            for (int s = 0; s < kMaxSteps; ++s)
            {
                auto& st = tr.steps[(size_t) s];
                st.gate = s < gate.length() && gate[s] == '1';
                if (s < vel.size())     st.velocity = (uint8_t) juce::jlimit (1, 127, vel[s].getIntValue());
                if (s < pitch.size())   st.pitch = (int8_t) juce::jlimit (-24, 24, pitch[s].getIntValue());
                if (s < slice.size())   st.slice = (uint8_t) juce::jlimit (0, kMaxSlices - 1, slice[s].getIntValue());
                if (s < ratchet.size()) st.ratchet = (uint8_t) juce::jlimit (1, kMaxRatchet, ratchet[s].getIntValue());
                if (s < prob.size())    st.probability = (uint8_t) juce::jlimit (0, 100, prob[s].getIntValue());
            }
        }

        juce::StringArray xy;
        xy.addTokens (pe->getStringAttribute ("xy"), ",", "");
        for (int s = 0; s < kMaxSteps && s < xy.size(); ++s)
        {
            auto& l = pat.xy[(size_t) s];
            l.active = xy[s].containsChar (':');
            if (l.active)
            {
                l.x = juce::jlimit (0.0f, 1.0f, xy[s].upToFirstOccurrenceOf (":", false, false).getFloatValue());
                l.y = juce::jlimit (0.0f, 1.0f, xy[s].fromFirstOccurrenceOf (":", false, false).getFloatValue());
            }
        }
    }
}

PatternBank defaultPatternBank()
{
    PatternBank bank;
    if (! kShipDemoPattern)
        return bank;

    // Pattern 1: a breakbeat. Voices: 0 kick, 1 rim, 2 snare, 3 clap, 4 tom,
    // 5 bass, 6 closed hat, 7 open hat.
    auto& p = bank.patterns[0];
    auto hit = [&] (int track, int step, int velocity, int probability = 100, int ratchet = 1)
    {
        auto& s = p.tracks[(size_t) track].steps[(size_t) step];
        s.gate = true;
        s.velocity = (uint8_t) velocity;
        s.probability = (uint8_t) probability;
        s.ratchet = (uint8_t) ratchet;
    };

    for (const auto [step, vel] : { std::pair { 0, 118 }, { 2, 100 }, { 10, 112 }, { 11, 88 } })
        hit (0, step, vel);                                   // kick

    hit (2, 4, 118);                                          // snare backbeats
    hit (2, 12, 118);
    hit (2, 7, 42);                                           // ghosts
    hit (2, 9, 38);
    hit (2, 15, 46, 70, 2);                                   // ghost roll, sometimes

    for (int s = 0; s < 16; s += 2)
        if (s != 14)
            hit (6, s, s % 4 == 0 ? 92 : 78);                 // closed hat, 8ths
    for (int s = 1; s < 16; s += 2)
        hit (6, s, 46, 55);                                   // 16th hats, sometimes

    hit (7, 14, 84);                                          // open hat into the bar
    hit (1, 13, 64, 50);                                      // rim, sometimes

    // One XY lock: the second snare hits hotter and dirtier.
    p.xy[12] = { true, 0.85f, 0.75f };
    return bank;
}

} // namespace batida
