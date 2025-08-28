#ifndef PRESETLOADER_H
#define PRESETLOADER_H

#include <QString>
#include <QXmlStreamReader>
#include "PresetData.h"

class PresetLoader {
public:
    PresetLoader();
    Preset loadPreset(const QString& filePath);

private:
    void parseSettings(QXmlStreamReader& xml, Preset& preset);
    void parseDefaultTrackStates(QXmlStreamReader& xml, Preset& preset); // NEW
    void parseToggles(QXmlStreamReader& xml, Preset& preset);
    void parsePrograms(QXmlStreamReader& xml, Preset& preset);
    void parseProgram(QXmlStreamReader& xml, Program& program);
    void parseProgramTags(QXmlStreamReader& xml, Program& program);
};

#endif // PRESETLOADER_H