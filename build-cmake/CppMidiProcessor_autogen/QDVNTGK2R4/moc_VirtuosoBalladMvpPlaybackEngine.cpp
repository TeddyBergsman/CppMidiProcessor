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
        "play",
        "stop",
        "onTick"
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
        // Slot 'play'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'stop'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onTick'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
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
        case 2: _t->play(); break;
        case 3: _t->stop(); break;
        case 4: _t->onTick(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (VirtuosoBalladMvpPlaybackEngine::*)(int )>(_a, &VirtuosoBalladMvpPlaybackEngine::currentCellChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (VirtuosoBalladMvpPlaybackEngine::*)(const QString & )>(_a, &VirtuosoBalladMvpPlaybackEngine::theoryEventJson, 1))
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
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
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
QT_WARNING_POP
