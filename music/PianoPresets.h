#pragma once

#include <QString>
#include <QVector>

#include "music/PianoProfile.h"

namespace music {

struct PianoPreset {
    QString id;    // stable identifier
    QString name;  // display name
    PianoProfile profile;
};

class PianoPresets {
public:
    static QVector<PianoPreset> all();
    static bool getById(const QString& id, PianoPreset& out);
    static bool getByName(const QString& name, PianoPreset& out);
};

} // namespace music

