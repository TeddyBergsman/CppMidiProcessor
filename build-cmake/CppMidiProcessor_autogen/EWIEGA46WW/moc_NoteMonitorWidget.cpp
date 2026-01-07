/****************************************************************************
** Meta object code from reading C++ file 'NoteMonitorWidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.9.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../NoteMonitorWidget.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'NoteMonitorWidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN17NoteMonitorWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto NoteMonitorWidget::qt_create_metaobjectdata<qt_meta_tag_ZN17NoteMonitorWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "NoteMonitorWidget",
        "virtuosoTheoryEventJson",
        "",
        "json",
        "virtuosoPlannedTheoryEventJson",
        "virtuosoLookaheadPlanJson",
        "stopAllPlayback",
        "requestVirtuosoLookaheadOnce",
        "setVirtuosoAgentEnergyMultiplier",
        "agent",
        "mult01to2",
        "setGuitarNote",
        "midiNote",
        "cents",
        "setVoiceNote",
        "setGuitarHz",
        "hz",
        "setVoiceHz",
        "setGuitarAmplitude",
        "aftertouch",
        "setVoiceAmplitude",
        "cc2",
        "setGuitarVelocity",
        "velocity"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'virtuosoTheoryEventJson'
        QtMocHelpers::SignalData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'virtuosoPlannedTheoryEventJson'
        QtMocHelpers::SignalData<void(const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'virtuosoLookaheadPlanJson'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'stopAllPlayback'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'requestVirtuosoLookaheadOnce'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setVirtuosoAgentEnergyMultiplier'
        QtMocHelpers::SlotData<void(const QString &, double)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 9 }, { QMetaType::Double, 10 },
        }}),
        // Slot 'setGuitarNote'
        QtMocHelpers::SlotData<void(int, double)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 }, { QMetaType::Double, 13 },
        }}),
        // Slot 'setVoiceNote'
        QtMocHelpers::SlotData<void(int, double)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 }, { QMetaType::Double, 13 },
        }}),
        // Slot 'setGuitarHz'
        QtMocHelpers::SlotData<void(double)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 16 },
        }}),
        // Slot 'setVoiceHz'
        QtMocHelpers::SlotData<void(double)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 16 },
        }}),
        // Slot 'setGuitarAmplitude'
        QtMocHelpers::SlotData<void(int)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 19 },
        }}),
        // Slot 'setVoiceAmplitude'
        QtMocHelpers::SlotData<void(int)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 21 },
        }}),
        // Slot 'setGuitarVelocity'
        QtMocHelpers::SlotData<void(int)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 23 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<NoteMonitorWidget, qt_meta_tag_ZN17NoteMonitorWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject NoteMonitorWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17NoteMonitorWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17NoteMonitorWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17NoteMonitorWidgetE_t>.metaTypes,
    nullptr
} };

void NoteMonitorWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<NoteMonitorWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->virtuosoTheoryEventJson((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->virtuosoPlannedTheoryEventJson((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->virtuosoLookaheadPlanJson((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->stopAllPlayback(); break;
        case 4: _t->requestVirtuosoLookaheadOnce(); break;
        case 5: _t->setVirtuosoAgentEnergyMultiplier((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 6: _t->setGuitarNote((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 7: _t->setVoiceNote((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 8: _t->setGuitarHz((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 9: _t->setVoiceHz((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 10: _t->setGuitarAmplitude((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 11: _t->setVoiceAmplitude((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 12: _t->setGuitarVelocity((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (NoteMonitorWidget::*)(const QString & )>(_a, &NoteMonitorWidget::virtuosoTheoryEventJson, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (NoteMonitorWidget::*)(const QString & )>(_a, &NoteMonitorWidget::virtuosoPlannedTheoryEventJson, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (NoteMonitorWidget::*)(const QString & )>(_a, &NoteMonitorWidget::virtuosoLookaheadPlanJson, 2))
            return;
    }
}

const QMetaObject *NoteMonitorWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *NoteMonitorWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17NoteMonitorWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int NoteMonitorWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
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
void NoteMonitorWidget::virtuosoTheoryEventJson(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void NoteMonitorWidget::virtuosoPlannedTheoryEventJson(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void NoteMonitorWidget::virtuosoLookaheadPlanJson(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}
QT_WARNING_POP
