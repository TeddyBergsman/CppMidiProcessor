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
        "backingTracksLoaded",
        "trackList",
        "backingTrackStateChanged",
        "trackIndex",
        "QMediaPlayer::PlaybackState",
        "state",
        "_internal_playTrack",
        "url",
        "_internal_pauseTrack",
        "_internal_resumeTrack",
        "backingTrackPositionChanged",
        "position",
        "backingTrackDurationChanged",
        "duration",
        "backingTrackTimelineUpdated",
        "timelineJson",
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
        "guitarVelocityUpdated",
        "applyProgram",
        "programIndex",
        "toggleTrack",
        "setVerbose",
        "verbose",
        "playTrack",
        "index",
        "pauseTrack",
        "seekToPosition",
        "positionMs",
        "setVoiceControlEnabled",
        "enabled",
        "setTranspose",
        "semitones",
        "applyTranspose",
        "loadTrackTimeline",
        "sendVirtualNoteOn",
        "channel",
        "note",
        "velocity",
        "sendVirtualNoteOff",
        "sendVirtualAllNotesOff",
        "pollLogQueue",
        "onPlayerStateChanged",
        "onInternalPlay",
        "onInternalPause",
        "onInternalResume",
        "onPlayerPositionChanged",
        "onPlayerDurationChanged"
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
        // Signal 'backingTracksLoaded'
        QtMocHelpers::SignalData<void(const QStringList &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 11 },
        }}),
        // Signal 'backingTrackStateChanged'
        QtMocHelpers::SignalData<void(int, QMediaPlayer::PlaybackState)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 13 }, { 0x80000000 | 14, 15 },
        }}),
        // Signal '_internal_playTrack'
        QtMocHelpers::SignalData<void(const QUrl &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QUrl, 17 },
        }}),
        // Signal '_internal_pauseTrack'
        QtMocHelpers::SignalData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal '_internal_resumeTrack'
        QtMocHelpers::SignalData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'backingTrackPositionChanged'
        QtMocHelpers::SignalData<void(qint64)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 21 },
        }}),
        // Signal 'backingTrackDurationChanged'
        QtMocHelpers::SignalData<void(qint64)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 23 },
        }}),
        // Signal 'backingTrackTimelineUpdated'
        QtMocHelpers::SignalData<void(const QString &)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 25 },
        }}),
        // Signal 'guitarPitchUpdated'
        QtMocHelpers::SignalData<void(int, double)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 27 }, { QMetaType::Double, 28 },
        }}),
        // Signal 'voicePitchUpdated'
        QtMocHelpers::SignalData<void(int, double)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 27 }, { QMetaType::Double, 28 },
        }}),
        // Signal 'guitarHzUpdated'
        QtMocHelpers::SignalData<void(double)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 31 },
        }}),
        // Signal 'voiceHzUpdated'
        QtMocHelpers::SignalData<void(double)>(32, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 31 },
        }}),
        // Signal 'guitarAftertouchUpdated'
        QtMocHelpers::SignalData<void(int)>(33, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 34 },
        }}),
        // Signal 'voiceCc2Updated'
        QtMocHelpers::SignalData<void(int)>(35, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 34 },
        }}),
        // Signal 'guitarVelocityUpdated'
        QtMocHelpers::SignalData<void(int)>(36, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 34 },
        }}),
        // Slot 'applyProgram'
        QtMocHelpers::SlotData<void(int)>(37, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 38 },
        }}),
        // Slot 'toggleTrack'
        QtMocHelpers::SlotData<void(const std::string &)>(39, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 },
        }}),
        // Slot 'setVerbose'
        QtMocHelpers::SlotData<void(bool)>(40, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 41 },
        }}),
        // Slot 'playTrack'
        QtMocHelpers::SlotData<void(int)>(42, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 43 },
        }}),
        // Slot 'pauseTrack'
        QtMocHelpers::SlotData<void()>(44, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'seekToPosition'
        QtMocHelpers::SlotData<void(qint64)>(45, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 46 },
        }}),
        // Slot 'setVoiceControlEnabled'
        QtMocHelpers::SlotData<void(bool)>(47, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'setTranspose'
        QtMocHelpers::SlotData<void(int)>(49, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 50 },
        }}),
        // Slot 'applyTranspose'
        QtMocHelpers::SlotData<void(int)>(51, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 50 },
        }}),
        // Slot 'loadTrackTimeline'
        QtMocHelpers::SlotData<void(int)>(52, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 43 },
        }}),
        // Slot 'sendVirtualNoteOn'
        QtMocHelpers::SlotData<void(int, int, int)>(53, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 54 }, { QMetaType::Int, 55 }, { QMetaType::Int, 56 },
        }}),
        // Slot 'sendVirtualNoteOff'
        QtMocHelpers::SlotData<void(int, int)>(57, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 54 }, { QMetaType::Int, 55 },
        }}),
        // Slot 'sendVirtualAllNotesOff'
        QtMocHelpers::SlotData<void(int)>(58, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 54 },
        }}),
        // Slot 'pollLogQueue'
        QtMocHelpers::SlotData<void()>(59, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPlayerStateChanged'
        QtMocHelpers::SlotData<void(QMediaPlayer::PlaybackState)>(60, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 14, 15 },
        }}),
        // Slot 'onInternalPlay'
        QtMocHelpers::SlotData<void(const QUrl &)>(61, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QUrl, 17 },
        }}),
        // Slot 'onInternalPause'
        QtMocHelpers::SlotData<void()>(62, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onInternalResume'
        QtMocHelpers::SlotData<void()>(63, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPlayerPositionChanged'
        QtMocHelpers::SlotData<void(qint64)>(64, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 21 },
        }}),
        // Slot 'onPlayerDurationChanged'
        QtMocHelpers::SlotData<void(qint64)>(65, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 23 },
        }}),
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
        case 3: _t->backingTracksLoaded((*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 4: _t->backingTrackStateChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QMediaPlayer::PlaybackState>>(_a[2]))); break;
        case 5: _t->_internal_playTrack((*reinterpret_cast< std::add_pointer_t<QUrl>>(_a[1]))); break;
        case 6: _t->_internal_pauseTrack(); break;
        case 7: _t->_internal_resumeTrack(); break;
        case 8: _t->backingTrackPositionChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 9: _t->backingTrackDurationChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 10: _t->backingTrackTimelineUpdated((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->guitarPitchUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 12: _t->voicePitchUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 13: _t->guitarHzUpdated((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 14: _t->voiceHzUpdated((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 15: _t->guitarAftertouchUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 16: _t->voiceCc2Updated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 17: _t->guitarVelocityUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 18: _t->applyProgram((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 19: _t->toggleTrack((*reinterpret_cast< std::add_pointer_t<std::string>>(_a[1]))); break;
        case 20: _t->setVerbose((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 21: _t->playTrack((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 22: _t->pauseTrack(); break;
        case 23: _t->seekToPosition((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 24: _t->setVoiceControlEnabled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 25: _t->setTranspose((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 26: _t->applyTranspose((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 27: _t->loadTrackTimeline((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 28: _t->sendVirtualNoteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 29: _t->sendVirtualNoteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 30: _t->sendVirtualAllNotesOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 31: _t->pollLogQueue(); break;
        case 32: _t->onPlayerStateChanged((*reinterpret_cast< std::add_pointer_t<QMediaPlayer::PlaybackState>>(_a[1]))); break;
        case 33: _t->onInternalPlay((*reinterpret_cast< std::add_pointer_t<QUrl>>(_a[1]))); break;
        case 34: _t->onInternalPause(); break;
        case 35: _t->onInternalResume(); break;
        case 36: _t->onPlayerPositionChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 37: _t->onPlayerDurationChanged((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
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
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(const QStringList & )>(_a, &MidiProcessor::backingTracksLoaded, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int , QMediaPlayer::PlaybackState )>(_a, &MidiProcessor::backingTrackStateChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(const QUrl & )>(_a, &MidiProcessor::_internal_playTrack, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)()>(_a, &MidiProcessor::_internal_pauseTrack, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)()>(_a, &MidiProcessor::_internal_resumeTrack, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(qint64 )>(_a, &MidiProcessor::backingTrackPositionChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(qint64 )>(_a, &MidiProcessor::backingTrackDurationChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(const QString & )>(_a, &MidiProcessor::backingTrackTimelineUpdated, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int , double )>(_a, &MidiProcessor::guitarPitchUpdated, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int , double )>(_a, &MidiProcessor::voicePitchUpdated, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(double )>(_a, &MidiProcessor::guitarHzUpdated, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(double )>(_a, &MidiProcessor::voiceHzUpdated, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::guitarAftertouchUpdated, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::voiceCc2Updated, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (MidiProcessor::*)(int )>(_a, &MidiProcessor::guitarVelocityUpdated, 17))
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
        if (_id < 38)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 38;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 38)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 38;
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
void MidiProcessor::backingTracksLoaded(const QStringList & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void MidiProcessor::backingTrackStateChanged(int _t1, QMediaPlayer::PlaybackState _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void MidiProcessor::_internal_playTrack(const QUrl & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void MidiProcessor::_internal_pauseTrack()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void MidiProcessor::_internal_resumeTrack()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void MidiProcessor::backingTrackPositionChanged(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void MidiProcessor::backingTrackDurationChanged(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void MidiProcessor::backingTrackTimelineUpdated(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void MidiProcessor::guitarPitchUpdated(int _t1, double _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2);
}

// SIGNAL 12
void MidiProcessor::voicePitchUpdated(int _t1, double _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1, _t2);
}

// SIGNAL 13
void MidiProcessor::guitarHzUpdated(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1);
}

// SIGNAL 14
void MidiProcessor::voiceHzUpdated(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1);
}

// SIGNAL 15
void MidiProcessor::guitarAftertouchUpdated(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1);
}

// SIGNAL 16
void MidiProcessor::voiceCc2Updated(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 16, nullptr, _t1);
}

// SIGNAL 17
void MidiProcessor::guitarVelocityUpdated(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 17, nullptr, _t1);
}
QT_WARNING_POP
