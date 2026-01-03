#pragma once

#include <QDialog>

#include "music/BassProfile.h"

class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QDialogButtonBox;
class QGroupBox;
class QComboBox;
class QPushButton;

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

    // Presets
    QComboBox* m_presetCombo = nullptr;
    QPushButton* m_loadPresetBtn = nullptr;
    QCheckBox* m_keepRouting = nullptr;
    QCheckBox* m_keepEnable = nullptr;

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
    QDoubleSpinBox* m_phraseArc = nullptr;
    QDoubleSpinBox* m_sectionArc = nullptr;

    QSpinBox* m_jitterMs = nullptr;
    QSpinBox* m_laidBackMs = nullptr;
    QSpinBox* m_pushMs = nullptr;
    QSpinBox* m_driftMaxMs = nullptr;
    QDoubleSpinBox* m_driftRate = nullptr;
    QSpinBox* m_attackVarMs = nullptr;
    QSpinBox* m_noteLengthMs = nullptr;
    QDoubleSpinBox* m_gatePct = nullptr;
    QDoubleSpinBox* m_swingAmount = nullptr;
    QDoubleSpinBox* m_swingRatio = nullptr;

    QDoubleSpinBox* m_chromaticism = nullptr;
    QCheckBox* m_honorSlash = nullptr;
    QDoubleSpinBox* m_slashProb = nullptr;

    // Evolution/variation (advanced)
    QGroupBox* m_advBox = nullptr;
    QDoubleSpinBox* m_intensityBase = nullptr;
    QDoubleSpinBox* m_intensityVar = nullptr;
    QDoubleSpinBox* m_evolutionRate = nullptr;
    QDoubleSpinBox* m_sectionRamp = nullptr;
    QSpinBox* m_phraseBars = nullptr;
    QDoubleSpinBox* m_ghostProb = nullptr;
    QSpinBox* m_ghostVel = nullptr;
    QDoubleSpinBox* m_ghostGate = nullptr;
    QDoubleSpinBox* m_pickup8thProb = nullptr;
    QDoubleSpinBox* m_fillPhraseEnd = nullptr;
    QDoubleSpinBox* m_syncopProb = nullptr;
    QDoubleSpinBox* m_twoFeelProb = nullptr;
    QDoubleSpinBox* m_brokenTimeProb = nullptr;
    QDoubleSpinBox* m_restProb = nullptr;
    QDoubleSpinBox* m_tieProb = nullptr;
    QDoubleSpinBox* m_motifProb = nullptr;
    QDoubleSpinBox* m_motifStrength = nullptr;
    QDoubleSpinBox* m_motifVariation = nullptr;

    QDoubleSpinBox* m_wRoot = nullptr;
    QDoubleSpinBox* m_wThird = nullptr;
    QDoubleSpinBox* m_wFifth = nullptr;
    QDoubleSpinBox* m_wSeventh = nullptr;

    QDoubleSpinBox* m_wAppChrom = nullptr;
    QDoubleSpinBox* m_wAppDia = nullptr;
    QDoubleSpinBox* m_wAppEncl = nullptr;

    // VST articulations / FX toggles
    QGroupBox* m_vstBox = nullptr;
    QSpinBox* m_ampleOffsetSemis = nullptr;
    QCheckBox* m_artSustainAccent = nullptr;
    QCheckBox* m_artNaturalHarmonic = nullptr;
    QCheckBox* m_artPalmMute = nullptr;
    QCheckBox* m_artSlideInOut = nullptr;
    QCheckBox* m_artLegatoSlide = nullptr;
    QCheckBox* m_artHammerPull = nullptr;

    QCheckBox* m_fxHitRimMute = nullptr;
    QCheckBox* m_fxHitTopPalmMute = nullptr;
    QCheckBox* m_fxHitTopFingerMute = nullptr;
    QCheckBox* m_fxHitTopOpen = nullptr;
    QCheckBox* m_fxHitRimOpen = nullptr;
    QCheckBox* m_fxScratch = nullptr;
    QCheckBox* m_fxBreath = nullptr;
    QCheckBox* m_fxSingleStringSlap = nullptr;
    QCheckBox* m_fxLeftHandSlapNoise = nullptr;
    QCheckBox* m_fxRightHandSlapNoise = nullptr;
    QCheckBox* m_fxSlideTurn4 = nullptr;
    QCheckBox* m_fxSlideTurn3 = nullptr;
    QCheckBox* m_fxSlideDown4 = nullptr;
    QCheckBox* m_fxSlideDown3 = nullptr;

    QDialogButtonBox* m_buttons = nullptr;
};

