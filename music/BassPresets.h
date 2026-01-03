#pragma once

#include <QString>
#include <QVector>

#include "music/BassProfile.h"

namespace music {

struct BassPreset {
    QString id;    // stable identifier
    QString name;  // display name
    BassProfile profile;
};

class BassPresets {
public:
    static QVector<BassPreset> all();
    static bool getById(const QString& id, BassPreset& out);
    static bool getByName(const QString& name, BassPreset& out);
};

} // namespace music

