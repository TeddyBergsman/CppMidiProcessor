#include "PresetLoader.h"
#include <QFile>
#include <QDebug>

PresetLoader::PresetLoader() {}

Preset PresetLoader::loadPreset(const QString& filePath) {
    Preset preset;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open preset file:" << filePath;
        return preset;
    }

    QXmlStreamReader xml(&file);

    if (xml.readNextStartElement()) {
        if (xml.name().toString() == "MidiProcessorPreset") {
            preset.name = xml.attributes().value("name").toString();
            while (xml.readNextStartElement()) {
                if (xml.name().toString() == "Settings") {
                    parseSettings(xml, preset);
                } else if (xml.name().toString() == "Toggles") {
                    parseToggles(xml, preset);
                } else if (xml.name().toString() == "Programs") {
                    parsePrograms(xml, preset);
                } else {
                    xml.skipCurrentElement();
                }
            }
        }
    }

    if (xml.hasError()) {
        qWarning() << "XML parsing error:" << xml.errorString();
        preset.isValid = false;
        return preset;
    }

    preset.isValid = true;
    return preset;
}

void PresetLoader::parseSettings(QXmlStreamReader& xml, Preset& preset) {
    while (xml.readNextStartElement()) {
        QString elementName = xml.name().toString();
        if (elementName == "InputPort" || elementName == "OutputPort") {
            QString portId = xml.attributes().value("name").toString();
            QString portName = xml.readElementText();
            preset.settings.ports[portId] = portName;
        } else if (elementName == "CommandNote") {
            preset.settings.commandNote = xml.readElementText().toInt();
        } else if (elementName == "DefaultTrackStates") {
            parseDefaultTrackStates(xml, preset);
        } else if (elementName == "PitchBendDeadZoneCents") { // NEW
            preset.settings.pitchBendDeadZoneCents = xml.readElementText().toInt();
        } else if (elementName == "PitchBendDownRangeCents") { // NEW
            preset.settings.pitchBendDownRangeCents = xml.readElementText().toInt();
        } else if (elementName == "PitchBendUpRangeCents") { // NEW
            preset.settings.pitchBendUpRangeCents = xml.readElementText().toInt();
        } else {
            xml.skipCurrentElement();
        }
    }
}

// NEW function to parse the default states
void PresetLoader::parseDefaultTrackStates(QXmlStreamReader& xml, Preset& preset) {
    while (xml.readNextStartElement()) {
        if (xml.name().toString() == "DefaultState") {
            QString toggleId = xml.attributes().value("toggleId").toString();
            bool enabled = (xml.attributes().value("enabled").toString() == "true");
            preset.settings.defaultTrackStates[toggleId] = enabled;
        }
        xml.skipCurrentElement();
    }
}

void PresetLoader::parseToggles(QXmlStreamReader& xml, Preset& preset) {
    while (xml.readNextStartElement()) {
        if (xml.name().toString() == "Toggle") {
            Toggle t;
            t.id = xml.attributes().value("id").toString();
            t.name = xml.attributes().value("name").toString();
            t.note = xml.attributes().value("note").toInt();
            t.channel = xml.attributes().value("channel").toInt();
            t.velocity = xml.attributes().value("velocity").toInt();
            preset.toggles.append(t);
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

void PresetLoader::parsePrograms(QXmlStreamReader& xml, Preset& preset) {
    while (xml.readNextStartElement()) {
        if (xml.name().toString() == "Program") {
            Program p;
            p.name = xml.attributes().value("name").toString();
            p.triggerNote = xml.attributes().value("triggerNote").toInt();
            p.programCC = xml.attributes().value("programCC").toInt();
            p.programValue = xml.attributes().value("programValue").toInt();
            p.volumeCC = xml.attributes().value("volumeCC").toInt();
            p.volumeValue = xml.attributes().value("volumeValue").toInt();
            parseProgram(xml, p);
            preset.programs.append(p);
        } else {
            xml.skipCurrentElement();
        }
    }
}

void PresetLoader::parseProgram(QXmlStreamReader& xml, Program& program) {
    while (xml.readNextStartElement()) {
        if (xml.name().toString() == "InitialState") {
            QString toggleId = xml.attributes().value("toggleId").toString();
            bool enabled = (xml.attributes().value("enabled").toString() == "true");
            program.initialStates[toggleId] = enabled;
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}