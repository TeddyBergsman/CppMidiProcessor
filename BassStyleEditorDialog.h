#pragma once

#include <QDialog>

#include "music/BassProfile.h"

class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QDialogButtonBox;

class BassStyleEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit BassStyleEditorDialog(const music::BassProfile& initial, QWidget* parent = nullptr);

signals:
    // Fires on any control change for live preview (does NOT imply persistence).
    void profilePreview(const music::BassProfile& profile);
    // Fires on Apply/OK. Caller should persist per-song.
    void profileCommitted(const music::BassProfile& profile);

private:
    void buildUi();
    void setUiFromProfile(const music::BassProfile& p);
    music::BassProfile profileFromUi() const;
    void emitPreview();

    music::BassProfile m_initial;

    QCheckBox* m_enabled = nullptr;
    QSpinBox* m_channel = nullptr;
    QSpinBox* m_minNote = nullptr;
    QSpinBox* m_maxNote = nullptr;
    QSpinBox* m_registerCenter = nullptr;
    QSpinBox* m_registerRange = nullptr;
    QSpinBox* m_maxLeap = nullptr;

    QSpinBox* m_baseVelocity = nullptr;
    QSpinBox* m_velocityVariance = nullptr;
    QDoubleSpinBox* m_accent1 = nullptr;
    QDoubleSpinBox* m_accent2 = nullptr;
    QDoubleSpinBox* m_accent3 = nullptr;
    QDoubleSpinBox* m_accent4 = nullptr;

    QSpinBox* m_jitterMs = nullptr;
    QSpinBox* m_laidBackMs = nullptr;
    QSpinBox* m_pushMs = nullptr;
    QSpinBox* m_noteLengthMs = nullptr;
    QDoubleSpinBox* m_gatePct = nullptr;

    QDoubleSpinBox* m_chromaticism = nullptr;
    QCheckBox* m_honorSlash = nullptr;
    QDoubleSpinBox* m_slashProb = nullptr;

    QDoubleSpinBox* m_wRoot = nullptr;
    QDoubleSpinBox* m_wThird = nullptr;
    QDoubleSpinBox* m_wFifth = nullptr;
    QDoubleSpinBox* m_wSeventh = nullptr;

    QDoubleSpinBox* m_wAppChrom = nullptr;
    QDoubleSpinBox* m_wAppDia = nullptr;
    QDoubleSpinBox* m_wAppEncl = nullptr;

    QDialogButtonBox* m_buttons = nullptr;
};

