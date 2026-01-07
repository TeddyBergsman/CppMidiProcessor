/****************************************************************************
** Meta object code from reading C++ file 'VirtuosoBalladMvpPlaybackEngine.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../playback/VirtuosoBalladMvpPlaybackEngine.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'VirtuosoBalladMvpPlaybackEngine.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8playback31VirtuosoBalladMvpPlaybackEngineE_t {};
} // unnamed namespace

template <> constexpr inline auto playback::VirtuosoBalladMvpPlaybackEngine::qt_create_metaobjectdata<qt_meta_tag_ZN8playback31VirtuosoBalladMvpPlaybackEngineE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "playback::VirtuosoBalladMvpPlaybackEngine",
        "currentCellChanged",
        "",
        "cellIndex",
        "theoryEventJson",
        "json",
        "plannedTheoryEventJson",
        "lookaheadPlanJson",
        "debugStatus",
        "text",
        "debugEnergy",
        "energy01",
        "isAuto",
        "play",
        "stop",
        "emitLookaheadPlanOnce",
        "applyLookaheadResult",
        "jobId",
        "stepNow",
        "buildMs",
        "setDebugEnergyAuto",
        "on",
        "setDebugEnergy",
        "setAgentEnergyMultiplier",
        "agent",
        "mult01to2",
        "setVirtuosityAuto",
        "setVirtuosity",
        "harmonicRisk01",
        "rhythmicComplexity01",
        "interaction01",
        "toneDark01",
        "onTick",
        "onGuitarNoteOn",
        "note",
        "vel",
        "onGuitarNoteOff",
        "onVoiceCc2Stream",
        "cc2",
        "onVoiceNoteOn",
        "onVoiceNoteOff"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'currentCellChanged'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'theoryEventJson'
        QtMocHelpers::SignalData<void(const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Signal 'plannedTheoryEventJson'
        QtMocHelpers::SignalData<void(const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Signal 'lookaheadPlanJson'
        QtMocHelpers::SignalData<void(const QString &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 },
        }}),
        // Signal 'debugStatus'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 },
        }}),
        // Signal 'debugEnergy'
        QtMocHelpers::SignalData<void(double, bool)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 11 }, { QMetaType::Bool, 12 },
        }}),
        // Slot 'play'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'emitLookaheadPlanOnce'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'applyLookaheadResult'
        QtMocHelpers::SlotData<void(quint64, int, const QString &, int)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 17 }, { QMetaType::Int, 18 }, { QMetaType::QString, 5 }, { QMetaType::Int, 19 },
        }}),
        // Slot 'setDebugEnergyAuto'
        QtMocHelpers::SlotData<void(bool)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 21 },
        }}),
        // Slot 'setDebugEnergy'
        QtMocHelpers::SlotData<void(double)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 11 },
        }}),
        // Slot 'setAgentEnergyMultiplier'
        QtMocHelpers::SlotData<void(const QString &, double)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 24 }, { QMetaType::Double, 25 },
        }}),
        // Slot 'setVirtuosityAuto'
        QtMocHelpers::SlotData<void(bool)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 21 },
        }}),
        // Slot 'setVirtuosity'
        QtMocHelpers::SlotData<void(double, double, double, double)>(27, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 28 }, { QMetaType::Double, 29 }, { QMetaType::Double, 30 }, { QMetaType::Double, 31 },
        }}),
        // Slot 'onTick'
        QtMocHelpers::SlotData<void()>(32, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onGuitarNoteOn'
        QtMocHelpers::SlotData<void(int, int)>(33, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 34 }, { QMetaType::Int, 35 },
        }}),
        // Slot 'onGuitarNoteOff'
        QtMocHelpers::SlotData<void(int)>(36, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 34 },
        }}),
        // Slot 'onVoiceCc2Stream'
        QtMocHelpers::SlotData<void(int)>(37, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 38 },
        }}),
        // Slot 'onVoiceNoteOn'
        QtMocHelpers::SlotData<void(int, int)>(39, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 34 }, { QMetaType::Int, 35 },
        }}),
        // Slot 'onVoiceNoteOff'
        QtMocHelpers::SlotData<void(int)>(40, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 34 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<VirtuosoBalladMvpPlaybackEngine, qt_meta_tag_ZN8playback31VirtuosoBalladMvpPlaybackEngineE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject playback::VirtuosoBalladMvpPlaybackEngine::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8playback31VirtuosoBalladMvpPlaybackEngineE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8playback31VirtuosoBalladMvpPlaybackEngineE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8playback31VirtuosoBalladMvpPlaybackEngineE_t>.metaTypes,
    nullptr
} };

void playback::VirtuosoBalladMvpPlaybackEngine::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<VirtuosoBalladMvpPlaybackEngine *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->currentCellChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->theoryEventJson((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->plannedTheoryEventJson((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->lookaheadPlanJson((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->debugStatus((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->debugEnergy((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 6: _t->play(); break;
        case 7: _t->stop(); break;
        case 8: _t->emitLookaheadPlanOnce(); break;
        case 9: _t->applyLookaheadResult((*reinterpret_cast< std::add_pointer_t<quint64>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[4]))); break;
        case 10: _t->setDebugEnergyAuto((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 11: _t->setDebugEnergy((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 12: _t->setAgentEnergyMultiplier((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 13: _t->setVirtuosityAuto((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 14: _t->setVirtuosity((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[4]))); break;
        case 15: _t->onTick(); break;
        case 16: _t->onGuitarNoteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 17: _t->onGuitarNoteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 18: _t->onVoiceCc2Stream((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 19: _t->onVoiceNoteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 20: _t->onVoiceNoteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (VirtuosoBalladMvpPlaybackEngine::*)(int )>(_a, &VirtuosoBalladMvpPlaybackEngine::currentCellChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoBalladMvpPlaybackEngine::*)(const QString & )>(_a, &VirtuosoBalladMvpPlaybackEngine::theoryEventJson, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoBalladMvpPlaybackEngine::*)(const QString & )>(_a, &VirtuosoBalladMvpPlaybackEngine::plannedTheoryEventJson, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoBalladMvpPlaybackEngine::*)(const QString & )>(_a, &VirtuosoBalladMvpPlaybackEngine::lookaheadPlanJson, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoBalladMvpPlaybackEngine::*)(const QString & )>(_a, &VirtuosoBalladMvpPlaybackEngine::debugStatus, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoBalladMvpPlaybackEngine::*)(double , bool )>(_a, &VirtuosoBalladMvpPlaybackEngine::debugEnergy, 5))
            return;
    }
}

const QMetaObject *playback::VirtuosoBalladMvpPlaybackEngine::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *playback::VirtuosoBalladMvpPlaybackEngine::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8playback31VirtuosoBalladMvpPlaybackEngineE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int playback::VirtuosoBalladMvpPlaybackEngine::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 21)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 21;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 21)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 21;
    }
    return _id;
}

// SIGNAL 0
void playback::VirtuosoBalladMvpPlaybackEngine::currentCellChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void playback::VirtuosoBalladMvpPlaybackEngine::theoryEventJson(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void playback::VirtuosoBalladMvpPlaybackEngine::plannedTheoryEventJson(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void playback::VirtuosoBalladMvpPlaybackEngine::lookaheadPlanJson(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void playback::VirtuosoBalladMvpPlaybackEngine::debugStatus(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void playback::VirtuosoBalladMvpPlaybackEngine::debugEnergy(double _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}
QT_WARNING_POP
