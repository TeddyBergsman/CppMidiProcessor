#include "virtuoso/theory/ScaleSuggester.h"

#include <algorithm>

namespace virtuoso::theory {
namespace {

static int normPc(int pc) {
    int v = pc % 12;
    if (v < 0) v += 12;
    return v;
}

static QSet<int> toPcSet(const QVector<int>& intervals) {
    QSet<int> out;
    for (int iv : intervals) out.insert(normPc(iv));
    return out;
}

static QSet<int> transposeSet(const QSet<int>& pcs, int shift) {
    QSet<int> out;
    out.reserve(pcs.size());
    for (int pc : pcs) out.insert(normPc(pc + shift));
    return out;
}

static double tagBonus(const QStringList& tags) {
    // Small deterministic nudges to prefer common jazz labels when coverage ties.
    double b = 0.0;
    if (tags.contains("diatonic")) b += 0.02;
    if (tags.contains("melodic_minor")) b += 0.03;
    if (tags.contains("harmonic_minor")) b += 0.02;
    if (tags.contains("harmonic_major")) b += 0.015;
    if (tags.contains("bebop")) b += 0.02;
    if (tags.contains("symmetric")) b += 0.01;
    if (tags.contains("messiaen")) b += 0.005;
    if (tags.contains("exotic")) b -= 0.01; // push exotic slightly down when ties
    return b;
}

} // namespace

QVector<ScaleSuggestion> suggestScalesForPitchClasses(const virtuoso::ontology::OntologyRegistry& registry,
                                                     const QSet<int>& pitchClasses,
                                                     int limit) {
    QVector<ScaleSuggestion> out;
    if (pitchClasses.isEmpty()) return out;

    const QSet<int> target = [&]() {
        QSet<int> t;
        for (int pc : pitchClasses) t.insert(normPc(pc));
        return t;
    }();

    for (const auto* s : registry.allScales()) {
        if (!s) continue;
        const QSet<int> scalePcs = toPcSet(s->intervals);
        // Consider all transpositions (0..11) and keep the best match.
        int bestMatched = -1;
        int bestShift = 0;
        for (int shift = 0; shift < 12; ++shift) {
            const QSet<int> shifted = transposeSet(scalePcs, shift);
            int matched = 0;
            for (int pc : target) if (shifted.contains(pc)) ++matched;
            if (matched > bestMatched) {
                bestMatched = matched;
                bestShift = shift;
            }
        }

        const int matched = std::max(0, bestMatched);
        const int total = target.size();
        const double coverage = total > 0 ? double(matched) / double(total) : 0.0;

        // Scoring:
        // - prioritize full coverage heavily
        // - then prefer smaller scales (more specific)
        // - then minor tag bonus
        const bool full = (matched == total);
        const int scaleSize = int(scalePcs.size());
        const double specificity = 1.0 / double(std::max(1, scaleSize));
        double score = coverage;
        if (full) score += 2.0;
        score += 0.15 * specificity;
        score += tagBonus(s->tags);

        ScaleSuggestion sug;
        sug.key = s->key;
        sug.name = s->name;
        sug.score = score;
        sug.coverage = coverage;
        sug.matched = matched;
        sug.total = total;
        sug.bestTranspose = bestShift;
        out.push_back(std::move(sug));
    }

    std::sort(out.begin(), out.end(), [](const ScaleSuggestion& a, const ScaleSuggestion& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.coverage != b.coverage) return a.coverage > b.coverage;
        return a.name < b.name;
    });

    if (limit > 0 && out.size() > limit) out.resize(limit);
    return out;
}

QVector<QString> explicitHintScalesForContext(const QString& voicingKey, const QString& chordKey) {
    QVector<QString> out;

    // UST mappings (dominant-centric defaults)
    if (voicingKey == "piano_ust_bVI") out = {"altered"};
    else if (voicingKey == "piano_ust_II") out = {"lydian_dominant"};
    else if (voicingKey.startsWith("piano_ust_")) out = {"altered"};

    // Chord-key hints
    if (out.isEmpty()) {
        if (chordKey.contains("7alt")) out = {"altered"};
        else if (chordKey.contains("7b9") || chordKey.contains("7#9") || chordKey.contains("7b13")) out = {"altered", "diminished_hw"};
        else if (chordKey == "7" || chordKey.startsWith("9") || chordKey.startsWith("13")) out = {"mixolydian", "lydian_dominant"};
        else if (chordKey.startsWith("maj")) out = {"ionian", "lydian"};
        else if (chordKey.startsWith("min")) out = {"dorian", "melodic_minor", "harmonic_minor"};
    }

    return out;
}

} // namespace virtuoso::theory

