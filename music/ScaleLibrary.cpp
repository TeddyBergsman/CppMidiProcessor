#include "music/ScaleLibrary.h"

namespace music {
namespace {

static Scale make(ScaleType type, const char* name, std::initializer_list<int> iv) {
    Scale s;
    s.type = type;
    s.name = QString::fromLatin1(name);
    s.intervals = QVector<int>(iv);
    return s;
}

static const QVector<Scale>& allScales() {
    static const QVector<Scale> k = {
        make(ScaleType::Ionian,        "Ionian (major)",              {0,2,4,5,7,9,11}),
        make(ScaleType::Dorian,        "Dorian",                      {0,2,3,5,7,9,10}),
        make(ScaleType::Phrygian,      "Phrygian",                    {0,1,3,5,7,8,10}),
        make(ScaleType::Lydian,        "Lydian",                      {0,2,4,6,7,9,11}),
        make(ScaleType::Mixolydian,    "Mixolydian",                  {0,2,4,5,7,9,10}),
        make(ScaleType::Aeolian,       "Aeolian (natural minor)",     {0,2,3,5,7,8,10}),
        make(ScaleType::Locrian,       "Locrian",                     {0,1,3,5,6,8,10}),

        make(ScaleType::MelodicMinor,  "Melodic minor",               {0,2,3,5,7,9,11}),
        make(ScaleType::DorianB2,      "Dorian b2",                   {0,1,3,5,7,9,10}),
        make(ScaleType::LydianDominant,"Lydian dominant",             {0,2,4,6,7,9,10}),
        make(ScaleType::Altered,       "Altered (super-locrian)",     {0,1,3,4,6,8,10}),
        make(ScaleType::LocrianNat2,   "Locrian natural 2",           {0,2,3,5,6,8,10}),

        make(ScaleType::HarmonicMinor, "Harmonic minor",              {0,2,3,5,7,8,11}),

        make(ScaleType::DiminishedWH,  "Diminished (whole-half)",     {0,2,3,5,6,8,9,11}),
        make(ScaleType::DiminishedHW,  "Diminished (half-whole)",     {0,1,3,4,6,7,9,10}),
        make(ScaleType::WholeTone,     "Whole tone",                  {0,2,4,6,8,10}),

        make(ScaleType::MajorPentatonic,"Major pentatonic",           {0,2,4,7,9}),
        make(ScaleType::MinorPentatonic,"Minor pentatonic",           {0,3,5,7,10}),
        make(ScaleType::Blues,         "Blues",                       {0,3,5,6,7,10}),
    };
    return k;
}

} // namespace

const Scale& ScaleLibrary::get(ScaleType type) {
    const auto& scales = allScales();
    for (const auto& s : scales) {
        if (s.type == type) return s;
    }
    // Fallback: Ionian
    return scales[0];
}

QVector<ScaleType> ScaleLibrary::suggestForChord(const ChordSymbol& chord) {
    if (chord.placeholder || chord.noChord) return {};
    QVector<ScaleType> out;

    auto hasSharp11 = [&]() -> bool {
        // If the chord explicitly has a #11, Lydian is the idiomatic choice.
        for (const auto& a : chord.alterations) {
            if (a.degree == 11 && a.delta == +1) return true;
        }
        return false;
    };

    // A small, useful heuristic set for later “musician brains”.
    if (chord.quality == ChordQuality::Dominant) {
        if (chord.alt) out.push_back(ScaleType::Altered);
        else if (chord.alterations.size() > 0 && chord.extension >= 7) out.push_back(ScaleType::LydianDominant);
        else out.push_back(ScaleType::Mixolydian);
    } else if (chord.quality == ChordQuality::Major) {
        // Maj7: Ionian vs Lydian depends on whether #11 is implied.
        // Many standards treat Maj7 as Ionian by default; Lydian becomes appropriate when #11 is present
        // (or when a composition clearly lives in that sound).
        if (chord.seventh == SeventhQuality::Major7) {
            if (hasSharp11()) {
                out.push_back(ScaleType::Lydian);
                out.push_back(ScaleType::Ionian);
            } else {
                out.push_back(ScaleType::Ionian);
                out.push_back(ScaleType::Lydian);
            }
        } else {
            out.push_back(ScaleType::Ionian);
        }
    } else if (chord.quality == ChordQuality::Minor) {
        out.push_back(ScaleType::Dorian);
        out.push_back(ScaleType::Aeolian);
    } else if (chord.quality == ChordQuality::HalfDiminished) {
        out.push_back(ScaleType::Locrian);
        out.push_back(ScaleType::LocrianNat2);
    } else if (chord.quality == ChordQuality::Diminished) {
        out.push_back(ScaleType::DiminishedHW);
        out.push_back(ScaleType::DiminishedWH);
    } else if (chord.quality == ChordQuality::Augmented) {
        out.push_back(ScaleType::WholeTone);
    } else if (chord.quality == ChordQuality::Sus4 || chord.quality == ChordQuality::Sus2) {
        out.push_back(ScaleType::Mixolydian);
    } else {
        out.push_back(ScaleType::Ionian);
    }

    return out;
}

} // namespace music

