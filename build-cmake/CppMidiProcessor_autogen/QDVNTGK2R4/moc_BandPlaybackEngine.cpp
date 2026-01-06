/****************************************************************************
** Meta object code from reading C++ file 'BandPlaybackEngine.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../playback/BandPlaybackEngine.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'BandPlaybackEngine.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8playback18BandPlaybackEngineE_t {};
} // unnamed namespace

template <> constexpr inline auto playback::BandPlaybackEngine::qt_create_metaobjectdata<qt_meta_tag_ZN8playback18BandPlaybackEngineE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "playback::BandPlaybackEngine",
        "currentCellChanged",
        "",
        "cellIndex",
        "bassNoteOn",
        "channel",
        "note",
        "velocity",
        "bassNoteOff",
        "bassAllNotesOff",
        "bassLogLine",
        "line",
        "pianoNoteOn",
        "pianoNoteOff",
        "pianoAllNotesOff",
        "pianoCC",
        "cc",
        "value",
        "pianoLogLine",
        "play",
        "stop",
        "onTick",
        "onDispatch"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'currentCellChanged'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'bassNoteOn'
        QtMocHelpers::SignalData<void(int, int, int)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 6 }, { QMetaType::Int, 7 },
        }}),
        // Signal 'bassNoteOff'
        QtMocHelpers::SignalData<void(int, int)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 6 },
        }}),
        // Signal 'bassAllNotesOff'
        QtMocHelpers::SignalData<void(int)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 },
        }}),
        // Signal 'bassLogLine'
        QtMocHelpers::SignalData<void(const QString &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 },
        }}),
        // Signal 'pianoNoteOn'
        QtMocHelpers::SignalData<void(int, int, int)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 6 }, { QMetaType::Int, 7 },
        }}),
        // Signal 'pianoNoteOff'
        QtMocHelpers::SignalData<void(int, int)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 6 },
        }}),
        // Signal 'pianoAllNotesOff'
        QtMocHelpers::SignalData<void(int)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 },
        }}),
        // Signal 'pianoCC'
        QtMocHelpers::SignalData<void(int, int, int)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 16 }, { QMetaType::Int, 17 },
        }}),
        // Signal 'pianoLogLine'
        QtMocHelpers::SignalData<void(const QString &)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 },
        }}),
        // Slot 'play'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onTick'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDispatch'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BandPlaybackEngine, qt_meta_tag_ZN8playback18BandPlaybackEngineE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject playback::BandPlaybackEngine::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8playback18BandPlaybackEngineE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8playback18BandPlaybackEngineE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8playback18BandPlaybackEngineE_t>.metaTypes,
    nullptr
} };

void playback::BandPlaybackEngine::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BandPlaybackEngine *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->currentCellChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->bassNoteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 2: _t->bassNoteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 3: _t->bassAllNotesOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->bassLogLine((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->pianoNoteOn((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 6: _t->pianoNoteOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 7: _t->pianoAllNotesOff((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->pianoCC((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3]))); break;
        case 9: _t->pianoLogLine((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->play(); break;
        case 11: _t->stop(); break;
        case 12: _t->onTick(); break;
        case 13: _t->onDispatch(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(int )>(_a, &BandPlaybackEngine::currentCellChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(int , int , int )>(_a, &BandPlaybackEngine::bassNoteOn, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(int , int )>(_a, &BandPlaybackEngine::bassNoteOff, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(int )>(_a, &BandPlaybackEngine::bassAllNotesOff, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(const QString & )>(_a, &BandPlaybackEngine::bassLogLine, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(int , int , int )>(_a, &BandPlaybackEngine::pianoNoteOn, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(int , int )>(_a, &BandPlaybackEngine::pianoNoteOff, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(int )>(_a, &BandPlaybackEngine::pianoAllNotesOff, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(int , int , int )>(_a, &BandPlaybackEngine::pianoCC, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (BandPlaybackEngine::*)(const QString & )>(_a, &BandPlaybackEngine::pianoLogLine, 9))
            return;
    }
}

const QMetaObject *playback::BandPlaybackEngine::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *playback::BandPlaybackEngine::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8playback18BandPlaybackEngineE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int playback::BandPlaybackEngine::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    return _id;
}

// SIGNAL 0
void playback::BandPlaybackEngine::currentCellChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void playback::BandPlaybackEngine::bassNoteOn(int _t1, int _t2, int _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3);
}

// SIGNAL 2
void playback::BandPlaybackEngine::bassNoteOff(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void playback::BandPlaybackEngine::bassAllNotesOff(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void playback::BandPlaybackEngine::bassLogLine(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void playback::BandPlaybackEngine::pianoNoteOn(int _t1, int _t2, int _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2, _t3);
}

// SIGNAL 6
void playback::BandPlaybackEngine::pianoNoteOff(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2);
}

// SIGNAL 7
void playback::BandPlaybackEngine::pianoAllNotesOff(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void playback::BandPlaybackEngine::pianoCC(int _t1, int _t2, int _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1, _t2, _t3);
}

// SIGNAL 9
void playback::BandPlaybackEngine::pianoLogLine(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}
QT_WARNING_POP
