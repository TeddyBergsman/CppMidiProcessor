/****************************************************************************
** Meta object code from reading C++ file 'midiprocessor.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../midiprocessor.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'midiprocessor.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.9.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN13MidiProcessorE_t {};
} // unnamed namespace

template <> constexpr inline auto MidiProcessor::qt_create_metaobjectdata<qt_meta_tag_ZN13MidiProcessorE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MidiProcessor",
        "programChanged",
        "",
        "newProgramIndex",
        "trackStateUpdated",
        "std::string",
        "trackId",
        "newState",
        "logMessage",
        "message",
        "guitarPitchUpdated",
        "midiNote",
        "cents",
        "voicePitchUpdated",
        "guitarHzUpdated",
        "hz",
        "voiceHzUpdated",
        "guitarAftertouchUpdated",
        "value",
        "voiceCc2Updated",
        "voiceCc2Stream",
        "guitarVelocityUpdated",
        "guitarNoteOn",
        "velocity",
        "guitarNoteOff",
        "voiceNoteOn",
        "voiceNoteOff",
        "applyProgram",
        "programIndex",
        "toggleTrack",
        "setVerbose",
        "verbose",
        "setVoiceControlEnabled",
        "enabled",
        "setTranspose",
        "semitones",
        "applyTranspose",
        "sendVirtualNoteOn",
        "channel",
        "note",
        "sendVirtualNoteOff",
        "sendVirtualAllNotesOff",
        "sendVirtualCC",
        "cc",
        "panicAllChannels",
        "pollLogQueue"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'programChanged'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'trackStateUpdated'
        QtMocHelpers::SignalData<void(const std::string &, bool)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 }, { QMetaType::Bool, 7 },
        }}),
        // Signal 'logMessage'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Signal 'guitarPitchUpdated'
        QtMocHelpers::SignalData<void(int, double)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 }, { QMetaType::Double, 12 },
        }}),
        // Signal 'voicePitchUpdated'
        QtMocHelpers::SignalData<void(int, double)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 }, { QMetaType::Double, 12 },
        }}),
        // Signal 'guitarHzUpdated'
        QtMocHelpers::SignalData<void(double)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 15 },
        }}),
        // Signal 'voiceHzUpdated'
        QtMocHelpers::SignalData<void(double)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 15 },
        }}),
        // Signal 'guitarAftertouchUpdated'
        QtMocHelpers::SignalData<void(int)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 18 },
        }}),
        // Signal 'voiceCc2Updated'
        QtMocHelpers::SignalData<void(int)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 18 },
        }}),
        // Signal 'voiceCc2Stream'
        QtMocHelpers::SignalData<void(int)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 18 },
        }}),
        // Signal 'guitarVelocityUpdated'
        QtMocHelpers::SignalData<void(int)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 18 },
        }}),
        // Signal 'guitarNoteOn'
        QtMocHelpers::SignalData<void(int, int)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 }, { QMetaType::Int, 23 },
        }}),
        // Signal 'guitarNoteOff'
        QtMocHelpers::SignalData<void(int)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 },
        }}),
        // Signal 'voiceNoteOn'
        QtMocHelpers::SignalData<void(int, int)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 }, { QMetaType::Int, 23 },
        }}),
        // Signal 'voiceNoteOff'
        QtMocHelpers::SignalData<void(int)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 },
        }}),
        // Slot 'applyProgram'
        QtMocHelpers::SlotData<void(int)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 28 },
        }}),
        // Slot 'toggleTrack'
        QtMocHelpers::SlotData<void(const std::string &)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 },
        }}),
        // Slot 'setVerbose'
        QtMocHelpers::SlotData<void(bool)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 31 },
        }}),
        // Slot 'setVoiceControlEnabled'
        QtMocHelpers::SlotData<void(bool)>(32, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 33 },
        }}),
        // Slot 'setTranspose'
        QtMocHelpers::SlotData<void(int)>(34, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 35 },
        }}),
        // Slot 'applyTranspose'
        QtMocHelpers::SlotData<void(int)>(36, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 35 },
        }}),
        // Slot 'sendVirtualNoteOn'
        QtMocHelpers::SlotData<void(int, int, int)>(37, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 38 }, { QMetaType::Int, 39 }, { QMetaType::Int, 23 },
        }}),
        // Slot 'sendVirtualNoteOff'
        QtMocHelpers::SlotData<void(int, int)>(40, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 38 }, { QMetaType::Int, 39 },
        }}),
        // Slot 'sendVirtualAllNotesOff'
        QtMocHelpers::SlotData<void(int)>(41, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 38 },
        }}),
        // Slot 'sendVirtualCC'
        QtMocHelpers::SlotData<void(int, int, int)>(42, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 38 }, { QMetaType::Int, 43 }, { QMetaType::Int, 18 },
        }}),
        // Slot 'panicAllChannels'
        QtMocHelpers::SlotData<void()>(44, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'pollLogQueue'
        QtMocHelpers::SlotData<void()>(45, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MidiProcessor, qt_meta_tag_ZN13MidiProcessorE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MidiProcessor::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13MidiProcessorE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13MidiProcessorE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN13MidiProcessorE_t>.metaTypes,
    nullptr
} };

void MidiProcessor::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MidiProcessor *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->programChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->trackStateUpdated((*reinterpret_cast< std::add_pointer_t<std::string>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 2: _t->logMessage((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->guitarPitchUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 4: _t->voicePitchUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 5: _t->guitarHzUpdated((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 6: _t->voiceHzUpdated((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 7: _t->guitarAftertouchUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->voiceCc2Updated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->voiceCc2Stream((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 10: _t->guitarVelocityUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 11: _t->guitarNoteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 12: _t->guitarNoteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 13: _t->voiceNoteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 14: _t->voiceNoteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 15: _t->applyProgram((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 16: _t->toggleTrack((*reinterpret_cast< std::add_pointer_t<std::string>>(_a[1]))); break;
        case 17: _t->setVerbose((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 18: _t->setVoiceControlEnabled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 19: _t->setTranspose((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 20: _t->applyTranspose((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 21: _t->sendVirtualNoteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 22: _t->sendVirtualNoteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 23: _t->sendVirtualAllNotesOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 24: _t->sendVirtualCC((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 25: _t->panicAllChannels(); break;
        case 26: _t->pollLogQueue(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::programChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(const std::string & , bool )>(_a, &MidiProcessor::trackStateUpdated, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(const QString & )>(_a, &MidiProcessor::logMessage, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int , double )>(_a, &MidiProcessor::guitarPitchUpdated, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int , double )>(_a, &MidiProcessor::voicePitchUpdated, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(double )>(_a, &MidiProcessor::guitarHzUpdated, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(double )>(_a, &MidiProcessor::voiceHzUpdated, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::guitarAftertouchUpdated, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::voiceCc2Updated, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::voiceCc2Stream, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::guitarVelocityUpdated, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int , int )>(_a, &MidiProcessor::guitarNoteOn, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::guitarNoteOff, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int , int )>(_a, &MidiProcessor::voiceNoteOn, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::voiceNoteOff, 14))
            return;
    }
}

const QMetaObject *MidiProcessor::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MidiProcessor::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN13MidiProcessorE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int MidiProcessor::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 27)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 27;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 27)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 27;
    }
    return _id;
}

// SIGNAL 0
void MidiProcessor::programChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void MidiProcessor::trackStateUpdated(const std::string & _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void MidiProcessor::logMessage(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void MidiProcessor::guitarPitchUpdated(int _t1, double _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}

// SIGNAL 4
void MidiProcessor::voicePitchUpdated(int _t1, double _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void MidiProcessor::guitarHzUpdated(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void MidiProcessor::voiceHzUpdated(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void MidiProcessor::guitarAftertouchUpdated(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void MidiProcessor::voiceCc2Updated(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void MidiProcessor::voiceCc2Stream(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void MidiProcessor::guitarVelocityUpdated(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void MidiProcessor::guitarNoteOn(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2);
}

// SIGNAL 12
void MidiProcessor::guitarNoteOff(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1);
}

// SIGNAL 13
void MidiProcessor::voiceNoteOn(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2);
}

// SIGNAL 14
void MidiProcessor::voiceNoteOff(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1);
}
QT_WARNING_POP
