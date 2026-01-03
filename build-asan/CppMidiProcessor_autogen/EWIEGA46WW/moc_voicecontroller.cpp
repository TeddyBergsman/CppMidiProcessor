/****************************************************************************
** Meta object code from reading C++ file 'voicecontroller.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../voicecontroller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'voicecontroller.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN15VoiceControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto VoiceController::qt_create_metaobjectdata<qt_meta_tag_ZN15VoiceControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "VoiceController",
        "transcriptionReceived",
        "",
        "text",
        "confidence",
        "detectedTriggers",
        "detectedTargets",
        "connectionStatusChanged",
        "connected",
        "errorOccurred",
        "error",
        "programCommandDetected",
        "programIndex",
        "trackCommandDetected",
        "trackIndex",
        "play",
        "toggleCommandDetected",
        "toggleId",
        "onProgramChanged"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'transcriptionReceived'
        QtMocHelpers::SignalData<void(const QString &, double, const QStringList &, const QStringList &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Double, 4 }, { QMetaType::QStringList, 5 }, { QMetaType::QStringList, 6 },
        }}),
        // Signal 'connectionStatusChanged'
        QtMocHelpers::SignalData<void(bool)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Signal 'errorOccurred'
        QtMocHelpers::SignalData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Signal 'programCommandDetected'
        QtMocHelpers::SignalData<void(int)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
        // Signal 'trackCommandDetected'
        QtMocHelpers::SignalData<void(int, bool)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 14 }, { QMetaType::Bool, 15 },
        }}),
        // Signal 'toggleCommandDetected'
        QtMocHelpers::SignalData<void(const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'onProgramChanged'
        QtMocHelpers::SlotData<void(int)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<VoiceController, qt_meta_tag_ZN15VoiceControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject VoiceController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15VoiceControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15VoiceControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15VoiceControllerE_t>.metaTypes,
    nullptr
} };

void VoiceController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<VoiceController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->transcriptionReceived((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[4]))); break;
        case 1: _t->connectionStatusChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->errorOccurred((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->programCommandDetected((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->trackCommandDetected((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 5: _t->toggleCommandDetected((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->onProgramChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (VoiceController::*)(const QString & , double , const QStringList & , const QStringList & )>(_a, &VoiceController::transcriptionReceived, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceController::*)(bool )>(_a, &VoiceController::connectionStatusChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceController::*)(const QString & )>(_a, &VoiceController::errorOccurred, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceController::*)(int )>(_a, &VoiceController::programCommandDetected, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceController::*)(int , bool )>(_a, &VoiceController::trackCommandDetected, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceController::*)(const QString & )>(_a, &VoiceController::toggleCommandDetected, 5))
            return;
    }
}

const QMetaObject *VoiceController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *VoiceController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15VoiceControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int VoiceController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void VoiceController::transcriptionReceived(const QString & _t1, double _t2, const QStringList & _t3, const QStringList & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 1
void VoiceController::connectionStatusChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void VoiceController::errorOccurred(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void VoiceController::programCommandDetected(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void VoiceController::trackCommandDetected(int _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void VoiceController::toggleCommandDetected(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN21VoiceControllerWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto VoiceControllerWorker::qt_create_metaobjectdata<qt_meta_tag_ZN21VoiceControllerWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "VoiceControllerWorker",
        "transcriptionReceived",
        "",
        "text",
        "confidence",
        "detectedTriggers",
        "detectedTargets",
        "connectionStatusChanged",
        "connected",
        "errorOccurred",
        "error",
        "programCommandDetected",
        "programIndex",
        "trackCommandDetected",
        "trackIndex",
        "play",
        "toggleCommandDetected",
        "toggleId",
        "start",
        "stop",
        "setEnabled",
        "enabled",
        "onProgramChanged",
        "onProcessReadyRead",
        "onProcessError",
        "QProcess::ProcessError",
        "onProcessFinished",
        "exitCode",
        "QProcess::ExitStatus",
        "exitStatus"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'transcriptionReceived'
        QtMocHelpers::SignalData<void(const QString &, double, const QStringList &, const QStringList &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Double, 4 }, { QMetaType::QStringList, 5 }, { QMetaType::QStringList, 6 },
        }}),
        // Signal 'connectionStatusChanged'
        QtMocHelpers::SignalData<void(bool)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 8 },
        }}),
        // Signal 'errorOccurred'
        QtMocHelpers::SignalData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Signal 'programCommandDetected'
        QtMocHelpers::SignalData<void(int)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
        // Signal 'trackCommandDetected'
        QtMocHelpers::SignalData<void(int, bool)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 14 }, { QMetaType::Bool, 15 },
        }}),
        // Signal 'toggleCommandDetected'
        QtMocHelpers::SignalData<void(const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 17 },
        }}),
        // Slot 'start'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setEnabled'
        QtMocHelpers::SlotData<void(bool)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 21 },
        }}),
        // Slot 'onProgramChanged'
        QtMocHelpers::SlotData<void(int)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
        // Slot 'onProcessReadyRead'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onProcessError'
        QtMocHelpers::SlotData<void(QProcess::ProcessError)>(24, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 25, 10 },
        }}),
        // Slot 'onProcessFinished'
        QtMocHelpers::SlotData<void(int, QProcess::ExitStatus)>(26, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 27 }, { 0x80000000 | 28, 29 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<VoiceControllerWorker, qt_meta_tag_ZN21VoiceControllerWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject VoiceControllerWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21VoiceControllerWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21VoiceControllerWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN21VoiceControllerWorkerE_t>.metaTypes,
    nullptr
} };

void VoiceControllerWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<VoiceControllerWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->transcriptionReceived((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[4]))); break;
        case 1: _t->connectionStatusChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->errorOccurred((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->programCommandDetected((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->trackCommandDetected((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 5: _t->toggleCommandDetected((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->start(); break;
        case 7: _t->stop(); break;
        case 8: _t->setEnabled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->onProgramChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 10: _t->onProcessReadyRead(); break;
        case 11: _t->onProcessError((*reinterpret_cast< std::add_pointer_t<QProcess::ProcessError>>(_a[1]))); break;
        case 12: _t->onProcessFinished((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QProcess::ExitStatus>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (VoiceControllerWorker::*)(const QString & , double , const QStringList & , const QStringList & )>(_a, &VoiceControllerWorker::transcriptionReceived, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceControllerWorker::*)(bool )>(_a, &VoiceControllerWorker::connectionStatusChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceControllerWorker::*)(const QString & )>(_a, &VoiceControllerWorker::errorOccurred, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceControllerWorker::*)(int )>(_a, &VoiceControllerWorker::programCommandDetected, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceControllerWorker::*)(int , bool )>(_a, &VoiceControllerWorker::trackCommandDetected, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (VoiceControllerWorker::*)(const QString & )>(_a, &VoiceControllerWorker::toggleCommandDetected, 5))
            return;
    }
}

const QMetaObject *VoiceControllerWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *VoiceControllerWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN21VoiceControllerWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int VoiceControllerWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 13;
    }
    return _id;
}

// SIGNAL 0
void VoiceControllerWorker::transcriptionReceived(const QString & _t1, double _t2, const QStringList & _t3, const QStringList & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 1
void VoiceControllerWorker::connectionStatusChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void VoiceControllerWorker::errorOccurred(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void VoiceControllerWorker::programCommandDetected(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void VoiceControllerWorker::trackCommandDetected(int _t1, bool _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void VoiceControllerWorker::toggleCommandDetected(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}
QT_WARNING_POP
