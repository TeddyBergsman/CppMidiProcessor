/****************************************************************************
** Meta object code from reading C++ file 'VirtuosoEngine.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../virtuoso/engine/VirtuosoEngine.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'VirtuosoEngine.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8virtuoso6engine14VirtuosoEngineE_t {};
} // unnamed namespace

template <> constexpr inline auto virtuoso::engine::VirtuosoEngine::qt_create_metaobjectdata<qt_meta_tag_ZN8virtuoso6engine14VirtuosoEngineE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "virtuoso::engine::VirtuosoEngine",
        "noteOn",
        "",
        "channel",
        "note",
        "velocity",
        "noteOff",
        "allNotesOff",
        "cc",
        "value",
        "theoryEventJson",
        "json",
        "start",
        "stop",
        "scheduleNote",
        "AgentIntentNote",
        "scheduleHumanizedNote",
        "agent",
        "virtuoso::groove::HumanizedEvent",
        "he",
        "logicTag"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'noteOn'
        QtMocHelpers::SignalData<void(int, int, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 }, { QMetaType::Int, 5 },
        }}),
        // Signal 'noteOff'
        QtMocHelpers::SignalData<void(int, int)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 4 },
        }}),
        // Signal 'allNotesOff'
        QtMocHelpers::SignalData<void(int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'cc'
        QtMocHelpers::SignalData<void(int, int, int)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { QMetaType::Int, 8 }, { QMetaType::Int, 9 },
        }}),
        // Signal 'theoryEventJson'
        QtMocHelpers::SignalData<void(const QString &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 },
        }}),
        // Slot 'start'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'scheduleNote'
        QtMocHelpers::SlotData<void(const AgentIntentNote &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 15, 4 },
        }}),
        // Slot 'scheduleHumanizedNote'
        QtMocHelpers::SlotData<void(const QString &, int, int, const virtuoso::groove::HumanizedEvent &, const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 17 }, { QMetaType::Int, 3 }, { QMetaType::Int, 4 }, { 0x80000000 | 18, 19 },
            { QMetaType::QString, 20 },
        }}),
        // Slot 'scheduleHumanizedNote'
        QtMocHelpers::SlotData<void(const QString &, int, int, const virtuoso::groove::HumanizedEvent &)>(16, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 17 }, { QMetaType::Int, 3 }, { QMetaType::Int, 4 }, { 0x80000000 | 18, 19 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<VirtuosoEngine, qt_meta_tag_ZN8virtuoso6engine14VirtuosoEngineE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject virtuoso::engine::VirtuosoEngine::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8virtuoso6engine14VirtuosoEngineE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8virtuoso6engine14VirtuosoEngineE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8virtuoso6engine14VirtuosoEngineE_t>.metaTypes,
    nullptr
} };

void virtuoso::engine::VirtuosoEngine::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<VirtuosoEngine *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->noteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 1: _t->noteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 2: _t->allNotesOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->cc((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 4: _t->theoryEventJson((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->start(); break;
        case 6: _t->stop(); break;
        case 7: _t->scheduleNote((*reinterpret_cast< std::add_pointer_t<AgentIntentNote>>(_a[1]))); break;
        case 8: _t->scheduleHumanizedNote((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<virtuoso::groove::HumanizedEvent>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[5]))); break;
        case 9: _t->scheduleHumanizedNote((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<virtuoso::groove::HumanizedEvent>>(_a[4]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (VirtuosoEngine::*)(int , int , int )>(_a, &VirtuosoEngine::noteOn, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoEngine::*)(int , int )>(_a, &VirtuosoEngine::noteOff, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoEngine::*)(int )>(_a, &VirtuosoEngine::allNotesOff, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoEngine::*)(int , int , int )>(_a, &VirtuosoEngine::cc, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoEngine::*)(const QString & )>(_a, &VirtuosoEngine::theoryEventJson, 4))
            return;
    }
}

const QMetaObject *virtuoso::engine::VirtuosoEngine::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *virtuoso::engine::VirtuosoEngine::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8virtuoso6engine14VirtuosoEngineE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int virtuoso::engine::VirtuosoEngine::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void virtuoso::engine::VirtuosoEngine::noteOn(int _t1, int _t2, int _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2, _t3);
}

// SIGNAL 1
void virtuoso::engine::VirtuosoEngine::noteOff(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void virtuoso::engine::VirtuosoEngine::allNotesOff(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void virtuoso::engine::VirtuosoEngine::cc(int _t1, int _t2, int _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3);
}

// SIGNAL 4
void virtuoso::engine::VirtuosoEngine::theoryEventJson(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}
QT_WARNING_POP
