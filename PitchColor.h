#ifndef PITCHCOLOR_H
#define PITCHCOLOR_H

#include <QString>
#include <cmath>
#include <algorithm>

// Shared cents->color mapping used across the UI.
// Returns a "#rrggbb" string suitable for QColor(QString).
inline QString pitchColorForCents(double cents) {
    // Continuous gradient:
    // - Flat (cents < 0): green (#00ff00) -> light blue (#00ccff) -> red (#ff0000) as |cents| grows to 50
    // - Sharp (cents > 0): green (#00ff00) -> vibrant orange (#ff9900) -> red (#ff0000) as cents grows to 50
    double t = std::min(1.0, std::fabs(cents) / 50.0);
    int r = 0, g = 0, b = 0;
    if (cents < 0) {
        const int midR = 0, midG = 204, midB = 255;
        if (t <= 0.5) {
            double u = t / 0.5;
            r = 0;
            g = static_cast<int>(std::round(255.0 * (1.0 - u) + midG * u));
            b = static_cast<int>(std::round(0.0 * (1.0 - u) + midB * u));
        } else {
            double u = (t - 0.5) / 0.5;
            r = static_cast<int>(std::round(midR * (1.0 - u) + 255.0 * u));
            g = static_cast<int>(std::round(midG * (1.0 - u) + 0.0 * u));
            b = static_cast<int>(std::round(midB * (1.0 - u) + 0.0 * u));
        }
    } else if (cents > 0) {
        const int midR = 255, midG = 153, midB = 0;
        if (t <= 0.5) {
            double u = t / 0.5;
            r = static_cast<int>(std::round(0.0 * (1.0 - u) + midR * u));
            g = static_cast<int>(std::round(255.0 * (1.0 - u) + midG * u));
            b = static_cast<int>(std::round(0.0 * (1.0 - u) + midB * u));
        } else {
            double u = (t - 0.5) / 0.5;
            r = static_cast<int>(std::round(midR * (1.0 - u) + 255.0 * u));
            g = static_cast<int>(std::round(midG * (1.0 - u) + 0.0 * u));
            b = static_cast<int>(std::round(midB * (1.0 - u) + 0.0 * u));
        }
    } else {
        r = 0; g = 255; b = 0;
    }
    r = std::max(0, std::min(255, r));
    g = std::max(0, std::min(255, g));
    b = std::max(0, std::min(255, b));
    return QString("#%1%2%3")
        .arg(r, 2, 16, QLatin1Char('0'))
        .arg(g, 2, 16, QLatin1Char('0'))
        .arg(b, 2, 16, QLatin1Char('0'));
}

#endif // PITCHCOLOR_H

