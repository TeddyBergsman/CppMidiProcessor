#include "virtuoso/theory/PatternLibrary.h"

#include <algorithm>

namespace virtuoso::theory {
namespace {

static int normalizePc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}

} // namespace

PatternLibrary PatternLibrary::builtins() {
    PatternLibrary lib;

    auto add = [&](QString key,
                   QString name,
                   QStringList tags,
                   int order,
                   PatternContour contour,
                   QVector<PatternStep> steps) {
        PatternDef p;
        p.key = std::move(key);
        p.name = std::move(name);
        p.tags = std::move(tags);
        p.order = order;
        p.contour = contour;
        p.steps = std::move(steps);
        lib.m_patterns.push_back(std::move(p));
    };

    // Core arpeggio patterns (degree-based)
    add("arp_135", "Arpeggio 1-3-5", {"arpeggio","triad"}, 0, PatternContour::Up,
        {{PatternStepKind::ChordDegree, 1},
         {PatternStepKind::ChordDegree, 3},
         {PatternStepKind::ChordDegree, 5}});

    add("arp_1357", "Arpeggio 1-3-5-7", {"arpeggio","seventh"}, 1, PatternContour::Up,
        {{PatternStepKind::ChordDegree, 1},
         {PatternStepKind::ChordDegree, 3},
         {PatternStepKind::ChordDegree, 5},
         {PatternStepKind::ChordDegree, 7}});

    add("arp_13579", "Arpeggio 1-3-5-7-9", {"arpeggio","extended"}, 2, PatternContour::Up,
        {{PatternStepKind::ChordDegree, 1},
         {PatternStepKind::ChordDegree, 3},
         {PatternStepKind::ChordDegree, 5},
         {PatternStepKind::ChordDegree, 7},
         {PatternStepKind::ChordDegree, 9}});

    add("arp_1357_updown", "Arpeggio 1-3-5-7 (up/down)", {"arpeggio","seventh"}, 3, PatternContour::UpDown,
        {{PatternStepKind::ChordDegree, 1},
         {PatternStepKind::ChordDegree, 3},
         {PatternStepKind::ChordDegree, 5},
         {PatternStepKind::ChordDegree, 7}});

    // Scale patterns (scale-degree based)
    add("scale_updown", "Scale (up/down)", {"scale_pattern"}, 50, PatternContour::UpDown,
        {{PatternStepKind::ScaleDegree, 1},
         {PatternStepKind::ScaleDegree, 2},
         {PatternStepKind::ScaleDegree, 3},
         {PatternStepKind::ScaleDegree, 4},
         {PatternStepKind::ScaleDegree, 5},
         {PatternStepKind::ScaleDegree, 6},
         {PatternStepKind::ScaleDegree, 7},
         {PatternStepKind::ScaleDegree, 8}});

    // Bebop-ish: 1-2-3-5-6-5-3-2 (common scalar turn)
    add("scale_turn_12356532", "Scale turn 1-2-3-5-6-5-3-2", {"scale_pattern","bebop"}, 51, PatternContour::AsWritten,
        {{PatternStepKind::ScaleDegree, 1},
         {PatternStepKind::ScaleDegree, 2},
         {PatternStepKind::ScaleDegree, 3},
         {PatternStepKind::ScaleDegree, 5},
         {PatternStepKind::ScaleDegree, 6},
         {PatternStepKind::ScaleDegree, 5},
         {PatternStepKind::ScaleDegree, 3},
         {PatternStepKind::ScaleDegree, 2}});

    return lib;
}

QVector<const PatternDef*> PatternLibrary::all() const {
    QVector<const PatternDef*> out;
    out.reserve(m_patterns.size());
    for (const auto& p : m_patterns) out.push_back(&p);
    std::sort(out.begin(), out.end(), [](const PatternDef* a, const PatternDef* b) {
        if (!a || !b) return a != nullptr;
        if (a->order != b->order) return a->order < b->order;
        return a->name < b->name;
    });
    return out;
}

const PatternDef* PatternLibrary::pattern(const QString& key) const {
    for (const auto& p : m_patterns) {
        if (p.key == key) return &p;
    }
    return nullptr;
}

int PatternLibrary::chordDegreeToSemitone(const virtuoso::ontology::ChordDef* chordCtx, int degree) {
    if (degree == 1) return 0;
    if (!chordCtx) return 0;

    auto thirdFromChord = [&]() -> int {
        for (int iv : chordCtx->intervals) if (iv == 3 || iv == 4) return iv;
        return 4;
    };
    auto fifthFromChord = [&]() -> int {
        for (int iv : chordCtx->intervals) if (iv == 6 || iv == 7 || iv == 8) return iv;
        return 7;
    };
    auto seventhFromChord = [&]() -> int {
        for (int iv : chordCtx->intervals) if (iv == 9 || iv == 10 || iv == 11) return iv;
        return 10;
    };

    switch (degree) {
    case 3: return thirdFromChord();
    case 5: return fifthFromChord();
    case 7: return seventhFromChord();
    case 9: return 14;
    case 11: return 17;
    case 13: return 21;
    default:
        // For higher degrees, treat as stacked 3rds on top of 7th.
        if (degree > 13) return 24;
        return 0;
    }
}

int PatternLibrary::scaleDegreeToSemitone(const virtuoso::ontology::ScaleDef* scale, int degree) {
    if (!scale) return 0;
    if (degree <= 0) return 0;
    // Degree 1..N maps to intervals[0..N-1], and allows octave extension.
    const int n = scale->intervals.size();
    if (n <= 0) return 0;
    const int idx0 = degree - 1;
    const int oct = idx0 / n;
    const int idx = idx0 % n;
    return scale->intervals[idx] + 12 * oct;
}

QVector<int> PatternLibrary::renderSemitoneSequence(const PatternDef& pattern,
                                                    const virtuoso::ontology::ChordDef* chordCtx,
                                                    const virtuoso::ontology::ScaleDef* scaleCtx) {
    QVector<int> seq;
    seq.reserve(pattern.steps.size() * 2);

    auto stepToSemitone = [&](const PatternStep& s) -> int {
        switch (s.kind) {
        case PatternStepKind::ChordDegree: return chordDegreeToSemitone(chordCtx, s.value);
        case PatternStepKind::ScaleDegree: return scaleDegreeToSemitone(scaleCtx, s.value);
        case PatternStepKind::SemitoneOffset: return s.value;
        case PatternStepKind::Rest: default: return 0;
        }
    };

    for (const auto& st : pattern.steps) {
        if (st.kind == PatternStepKind::Rest) continue;
        seq.push_back(stepToSemitone(st));
    }

    // Apply contour
    if (pattern.contour == PatternContour::Up) {
        std::sort(seq.begin(), seq.end());
    } else if (pattern.contour == PatternContour::Down) {
        std::sort(seq.begin(), seq.end(), std::greater<int>());
    } else if (pattern.contour == PatternContour::UpDown) {
        std::sort(seq.begin(), seq.end());
        for (int i = seq.size() - 2; i >= 0; --i) seq.push_back(seq[i]);
    } else if (pattern.contour == PatternContour::DownUp) {
        std::sort(seq.begin(), seq.end(), std::greater<int>());
        for (int i = seq.size() - 2; i >= 0; --i) seq.push_back(seq[i]);
    } else {
        // AsWritten: keep as provided, but normalize to monotonic octave bumps when needed
        // (optional later; for now we leave exact semitone offsets).
    }

    // Normalize to keep within a small range (avoid negative values from mis-specified steps)
    for (int& v : seq) v = std::max(0, v);
    return seq;
}

} // namespace virtuoso::theory

