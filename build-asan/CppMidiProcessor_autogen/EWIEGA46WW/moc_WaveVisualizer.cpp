/****************************************************************************
** Meta object code from reading C++ file 'WaveVisualizer.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../WaveVisualizer.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'WaveVisualizer.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10WaveCanvasE_t {};
} // unnamed namespace

template <> constexpr inline auto WaveCanvas::qt_create_metaobjectdata<qt_meta_tag_ZN10WaveCanvasE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "WaveCanvas",
        "setGuitarHz",
        "",
        "hz",
        "setVoiceHz",
        "setGuitarAmplitude",
        "aftertouch01to127",
        "setVoiceAmplitude",
        "cc201to127",
        "setGuitarVelocity",
        "velocity01to127",
        "setGuitarColor",
        "color",
        "setVoiceColor"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'setGuitarHz'
        QtMocHelpers::SlotData<void(double)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 3 },
        }}),
        // Slot 'setVoiceHz'
        QtMocHelpers::SlotData<void(double)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 3 },
        }}),
        // Slot 'setGuitarAmplitude'
        QtMocHelpers::SlotData<void(int)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Slot 'setVoiceAmplitude'
        QtMocHelpers::SlotData<void(int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 8 },
        }}),
        // Slot 'setGuitarVelocity'
        QtMocHelpers::SlotData<void(int)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'setGuitarColor'
        QtMocHelpers::SlotData<void(const QColor &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QColor, 12 },
        }}),
        // Slot 'setVoiceColor'
        QtMocHelpers::SlotData<void(const QColor &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QColor, 12 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<WaveCanvas, qt_meta_tag_ZN10WaveCanvasE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject WaveCanvas::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10WaveCanvasE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10WaveCanvasE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10WaveCanvasE_t>.metaTypes,
    nullptr
} };

void WaveCanvas::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<WaveCanvas *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setGuitarHz((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 1: _t->setVoiceHz((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 2: _t->setGuitarAmplitude((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->setVoiceAmplitude((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->setGuitarVelocity((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->setGuitarColor((*reinterpret_cast< std::add_pointer_t<QColor>>(_a[1]))); break;
        case 6: _t->setVoiceColor((*reinterpret_cast< std::add_pointer_t<QColor>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *WaveCanvas::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WaveCanvas::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10WaveCanvasE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int WaveCanvas::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
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
namespace {
struct qt_meta_tag_ZN14WaveVisualizerE_t {};
} // unnamed namespace

template <> constexpr inline auto WaveVisualizer::qt_create_metaobjectdata<qt_meta_tag_ZN14WaveVisualizerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "WaveVisualizer",
        "setGuitarHz",
        "",
        "hz",
        "setVoiceHz",
        "setGuitarAmplitude",
        "val",
        "setVoiceAmplitude",
        "setGuitarVelocity",
        "setGuitarColor",
        "color",
        "setVoiceColor",
        "setGuitarCentsText",
        "text",
        "setVoiceCentsText"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'setGuitarHz'
        QtMocHelpers::SlotData<void(double)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 3 },
        }}),
        // Slot 'setVoiceHz'
        QtMocHelpers::SlotData<void(double)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 3 },
        }}),
        // Slot 'setGuitarAmplitude'
        QtMocHelpers::SlotData<void(int)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Slot 'setVoiceAmplitude'
        QtMocHelpers::SlotData<void(int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Slot 'setGuitarVelocity'
        QtMocHelpers::SlotData<void(int)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 6 },
        }}),
        // Slot 'setGuitarColor'
        QtMocHelpers::SlotData<void(const QColor &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QColor, 10 },
        }}),
        // Slot 'setVoiceColor'
        QtMocHelpers::SlotData<void(const QColor &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QColor, 10 },
        }}),
        // Slot 'setGuitarCentsText'
        QtMocHelpers::SlotData<void(const QString &)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 13 },
        }}),
        // Slot 'setVoiceCentsText'
        QtMocHelpers::SlotData<void(const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 13 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<WaveVisualizer, qt_meta_tag_ZN14WaveVisualizerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject WaveVisualizer::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14WaveVisualizerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14WaveVisualizerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14WaveVisualizerE_t>.metaTypes,
    nullptr
} };

void WaveVisualizer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<WaveVisualizer *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->setGuitarHz((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 1: _t->setVoiceHz((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 2: _t->setGuitarAmplitude((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->setVoiceAmplitude((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->setGuitarVelocity((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->setGuitarColor((*reinterpret_cast< std::add_pointer_t<QColor>>(_a[1]))); break;
        case 6: _t->setVoiceColor((*reinterpret_cast< std::add_pointer_t<QColor>>(_a[1]))); break;
        case 7: _t->setGuitarCentsText((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->setVoiceCentsText((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *WaveVisualizer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WaveVisualizer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14WaveVisualizerE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int WaveVisualizer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}
QT_WARNING_POP
