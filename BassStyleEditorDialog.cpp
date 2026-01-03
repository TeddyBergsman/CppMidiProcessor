#include "BassStyleEditorDialog.h"

#include "music/BassPresets.h"
#include "playback/BandPlaybackEngine.h"

#include <QtWidgets>

BassStyleEditorDialog::BassStyleEditorDialog(const music::BassProfile& initial,
                                             playback::BandPlaybackEngine* playback,
                                             QWidget* parent)
    : QDialog(parent),
      m_initial(initial),
      m_playback(playback) {
    setWindowTitle("Bass Style");
    setModal(true);
    buildUi();
    setUiFromProfile(initial);
    emitPreview();
}

void BassStyleEditorDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto makeSpin = [](int lo, int hi) {
        auto* s = new QSpinBox;
        s->setRange(lo, hi);
        return s;
    };
    auto makeD = [](double lo, double hi, double step, int decimals = 2) {
        auto* d = new QDoubleSpinBox;
        d->setRange(lo, hi);
        d->setSingleStep(step);
        d->setDecimals(decimals);
        return d;
    };

    m_enabled = new QCheckBox("Enable bass");

    // Presets row
    {
        auto* presetsRow = new QWidget(this);
        auto* h = new QHBoxLayout(presetsRow);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);

        auto* presetLbl = new QLabel("Preset:", presetsRow);
        presetLbl->setStyleSheet("QLabel { color: #ddd; }");
        m_presetCombo = new QComboBox(presetsRow);
        m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        const auto presets = music::BassPresets::all();
        for (const auto& p : presets) {
            m_presetCombo->addItem(p.name, p.id);
        }

        m_loadPresetBtn = new QPushButton("Load", presetsRow);
        m_loadPresetBtn->setFixedWidth(64);
        m_keepRouting = new QCheckBox("Keep routing/range", presetsRow);
        m_keepRouting->setChecked(true);
        m_keepEnable = new QCheckBox("Keep enable/channel", presetsRow);
        m_keepEnable->setChecked(true);

        h->addWidget(presetLbl, 0);
        h->addWidget(m_presetCombo, 1);
        h->addWidget(m_loadPresetBtn, 0);
        h->addWidget(m_keepRouting, 0);
        h->addWidget(m_keepEnable, 0);

        presetsRow->setLayout(h);
        root->addWidget(presetsRow);
    }

    // --- Routing / range ---
    auto* rangeBox = new QGroupBox("Routing & Range");
    auto* rangeForm = new QFormLayout(rangeBox);
    m_channel = makeSpin(1, 16);
    m_minNote = makeSpin(0, 127);
    m_maxNote = makeSpin(0, 127);
    m_registerCenter = makeSpin(0, 127);
    m_registerRange = makeSpin(0, 60);
    m_maxLeap = makeSpin(0, 24);
    rangeForm->addRow("MIDI channel", m_channel);
    rangeForm->addRow("Min MIDI note", m_minNote);
    rangeForm->addRow("Max MIDI note", m_maxNote);
    rangeForm->addRow("Register center", m_registerCenter);
    rangeForm->addRow("Register range (+/-)", m_registerRange);
    rangeForm->addRow("Max leap (semitones)", m_maxLeap);

    // --- Feel ---
    auto* feelBox = new QGroupBox("Timing / Articulation");
    auto* feelForm = new QFormLayout(feelBox);
    m_feelStyle = new QComboBox(feelBox);
    m_feelStyle->addItem("Ballad swing (2-feel default)", (int)music::BassFeelStyle::BalladSwing);
    m_feelStyle->addItem("Walking swing (4-to-the-bar)", (int)music::BassFeelStyle::WalkingSwing);
    m_jitterMs = makeSpin(0, 50);
    m_laidBackMs = makeSpin(-50, 50);
    m_pushMs = makeSpin(-50, 50);
    m_driftMaxMs = makeSpin(0, 80);
    m_driftRate = makeD(0.0, 1.0, 0.01, 2);
    m_attackVarMs = makeSpin(0, 40);
    m_noteLengthMs = makeSpin(0, 2000);
    m_gatePct = makeD(0.05, 1.0, 0.01, 2);
    m_swingAmount = makeD(0.0, 1.0, 0.01, 2);
    m_swingRatio = makeD(1.2, 4.0, 0.05, 2);
    feelForm->addRow("Feel style", m_feelStyle);
    feelForm->addRow("Micro jitter (ms +/-)", m_jitterMs);
    feelForm->addRow("Laid back (ms)", m_laidBackMs);
    feelForm->addRow("Push (ms)", m_pushMs);
    feelForm->addRow("Timing drift max (ms)", m_driftMaxMs);
    feelForm->addRow("Timing drift rate", m_driftRate);
    feelForm->addRow("Attack variance (ms +/-)", m_attackVarMs);
    feelForm->addRow("Note length (ms; 0=gate)", m_noteLengthMs);
    feelForm->addRow("Gate (% of beat)", m_gatePct);
    feelForm->addRow("Swing amount", m_swingAmount);
    feelForm->addRow("Swing ratio (e.g. 2.0=2:1)", m_swingRatio);

    // --- Dynamics ---
    auto* dynBox = new QGroupBox("Dynamics");
    auto* dynForm = new QFormLayout(dynBox);
    m_baseVelocity = makeSpin(1, 127);
    m_velocityVariance = makeSpin(0, 64);
    m_accent1 = makeD(0.1, 2.0, 0.02, 2);
    m_accent2 = makeD(0.1, 2.0, 0.02, 2);
    m_accent3 = makeD(0.1, 2.0, 0.02, 2);
    m_accent4 = makeD(0.1, 2.0, 0.02, 2);
    m_phraseArc = makeD(0.0, 1.0, 0.01, 2);
    m_sectionArc = makeD(0.0, 1.0, 0.01, 2);
    dynForm->addRow("Base velocity", m_baseVelocity);
    dynForm->addRow("Velocity variance (+/-)", m_velocityVariance);
    dynForm->addRow("Accent beat 1", m_accent1);
    dynForm->addRow("Accent beat 2", m_accent2);
    dynForm->addRow("Accent beat 3", m_accent3);
    dynForm->addRow("Accent beat 4", m_accent4);
    dynForm->addRow("Phrase arc strength", m_phraseArc);
    dynForm->addRow("Section arc strength", m_sectionArc);

    // --- Musical line ---
    auto* lineBox = new QGroupBox("Line & Harmony");
    auto* lineForm = new QFormLayout(lineBox);
    m_chromaticism = makeD(0.0, 1.0, 0.01, 2);
    m_honorSlash = new QCheckBox("Honor slash bass");
    m_slashProb = makeD(0.0, 1.0, 0.01, 2);
    lineForm->addRow("Chromaticism", m_chromaticism);
    lineForm->addRow(m_honorSlash);
    lineForm->addRow("Slash probability", m_slashProb);

    // --- Advanced evolution / variation ---
    m_advBox = new QGroupBox("Advanced: Evolution & Variation");
    m_advBox->setCheckable(true);
    m_advBox->setChecked(true);
    auto* advForm = new QFormLayout(m_advBox);
    m_intensityBase = makeD(0.0, 1.0, 0.01, 2);
    m_intensityVar = makeD(0.0, 1.0, 0.01, 2);
    m_evolutionRate = makeD(0.0, 1.0, 0.01, 2);
    m_sectionRamp = makeD(0.0, 1.0, 0.01, 2);
    m_phraseBars = makeSpin(1, 16);
    m_ghostProb = makeD(0.0, 1.0, 0.01, 2);
    m_ghostVel = makeSpin(1, 60);
    m_ghostGate = makeD(0.05, 0.8, 0.01, 2);
    m_pickup8thProb = makeD(0.0, 1.0, 0.01, 2);
    m_fillPhraseEnd = makeD(0.0, 1.0, 0.01, 2);
    m_syncopProb = makeD(0.0, 1.0, 0.01, 2);
    m_twoFeelProb = makeD(0.0, 1.0, 0.01, 2);
    m_brokenTimeProb = makeD(0.0, 1.0, 0.01, 2);
    m_restProb = makeD(0.0, 1.0, 0.01, 2);
    m_tieProb = makeD(0.0, 1.0, 0.01, 2);
    m_motifProb = makeD(0.0, 1.0, 0.01, 2);
    m_motifStrength = makeD(0.0, 1.0, 0.01, 2);
    m_motifVariation = makeD(0.0, 1.0, 0.01, 2);
    // Extra human features
    auto* twoBeatRun = makeD(0.0, 1.0, 0.01, 2);
    twoBeatRun->setObjectName("twoBeatRunProb");
    auto* enclosure = makeD(0.0, 1.0, 0.01, 2);
    enclosure->setObjectName("enclosureProb");
    auto* introRestraint = makeD(0.0, 1.0, 0.01, 2);
    introRestraint->setObjectName("sectionIntroRestraint");
    advForm->addRow("Intensity base", m_intensityBase);
    advForm->addRow("Intensity variance", m_intensityVar);
    advForm->addRow("Evolution rate", m_evolutionRate);
    advForm->addRow("Section ramp", m_sectionRamp);
    advForm->addRow("Phrase length (bars)", m_phraseBars);
    advForm->addRow("Ghost note probability", m_ghostProb);
    advForm->addRow("Ghost velocity", m_ghostVel);
    advForm->addRow("Ghost gate (% beat)", m_ghostGate);
    advForm->addRow("Pickup 8th probability", m_pickup8thProb);
    advForm->addRow("Phrase-end fill boost", m_fillPhraseEnd);
    advForm->addRow("Syncopation probability", m_syncopProb);
    advForm->addRow("2-feel phrase probability", m_twoFeelProb);
    advForm->addRow("Broken-time phrase probability", m_brokenTimeProb);
    advForm->addRow("Broken-time rest probability", m_restProb);
    advForm->addRow("Broken-time tie probability", m_tieProb);
    advForm->addRow("Motif probability", m_motifProb);
    advForm->addRow("Motif strength", m_motifStrength);
    advForm->addRow("Motif variation", m_motifVariation);
    advForm->addRow("2-beat run probability (beats 3–4)", twoBeatRun);
    advForm->addRow("Enclosure probability (beat 4)", enclosure);
    advForm->addRow("Section intro restraint", introRestraint);

    auto* weightsBox = new QGroupBox("Chord-tone target weights (beats 1 & 3)");
    auto* weightsForm = new QFormLayout(weightsBox);
    m_wRoot = makeD(0.0, 3.0, 0.05, 2);
    m_wThird = makeD(0.0, 3.0, 0.05, 2);
    m_wFifth = makeD(0.0, 3.0, 0.05, 2);
    m_wSeventh = makeD(0.0, 3.0, 0.05, 2);
    weightsForm->addRow("Root", m_wRoot);
    weightsForm->addRow("3rd", m_wThird);
    weightsForm->addRow("5th", m_wFifth);
    weightsForm->addRow("7th", m_wSeventh);

    auto* appBox = new QGroupBox("Approach weights (beat 4)");
    auto* appForm = new QFormLayout(appBox);
    m_wAppChrom = makeD(0.0, 1.0, 0.01, 2);
    m_wAppDia = makeD(0.0, 1.0, 0.01, 2);
    m_wAppEncl = makeD(0.0, 1.0, 0.01, 2);
    appForm->addRow("Chromatic", m_wAppChrom);
    appForm->addRow("Diatonic-ish", m_wAppDia);
    appForm->addRow("Enclosure-ish", m_wAppEncl);

    // --- VST articulations / FX (Ample Bass Upright) ---
    m_vstBox = new QGroupBox("VST: Articulations & FX (Ample Bass Upright)");
    auto* vstGrid = new QGridLayout(m_vstBox);
    vstGrid->setHorizontalSpacing(10);
    vstGrid->setVerticalSpacing(6);

    auto* vstTop = new QWidget(m_vstBox);
    auto* vstTopRow = new QHBoxLayout(vstTop);
    vstTopRow->setContentsMargins(0, 0, 0, 0);
    vstTopRow->setSpacing(8);
    auto* offLbl = new QLabel("Note name offset (semitones):", vstTop);
    offLbl->setToolTip("Some VST manuals use a different octave naming.\n"
                       "If the manual says F#4 but you see we send 66, set this to +12 so F#4 becomes 78.");
    m_ampleOffsetSemis = makeSpin(-24, 24);
    m_ampleOffsetSemis->setToolTip(offLbl->toolTip());
    vstTopRow->addWidget(offLbl);
    vstTopRow->addWidget(m_ampleOffsetSemis);
    vstTopRow->addStretch(1);

    auto* artBox = new QGroupBox("Articulations (Keyswitches)");
    auto* artLayout = new QVBoxLayout(artBox);
    m_artSustainAccent = new QCheckBox("Sustain & Accent (C0; vel >= 126 = Accent)");
    m_artNaturalHarmonic = new QCheckBox("Natural Harmonic (C#0)");
    m_artPalmMute = new QCheckBox("Palm Mute (D0)");
    m_artSlideInOut = new QCheckBox("Slide In / Out (D#0)");
    m_artLegatoSlide = new QCheckBox("Legato Slide (E0; overlapping notes)");
    m_artHammerPull = new QCheckBox("Hammer-On / Pull-Off (F0; overlapping notes)");
    artLayout->addWidget(m_artSustainAccent);
    artLayout->addWidget(m_artNaturalHarmonic);
    artLayout->addWidget(m_artPalmMute);
    artLayout->addWidget(m_artSlideInOut);
    artLayout->addWidget(m_artLegatoSlide);
    artLayout->addWidget(m_artHammerPull);

    auto* fxBox = new QGroupBox("FX Sounds (Notes)");
    auto* fxLayout = new QVBoxLayout(fxBox);
    m_fxHitRimMute = new QCheckBox("Hit Rim (Mute) F#4");
    m_fxHitTopPalmMute = new QCheckBox("Hit Top (Palm Mute) G4");
    m_fxHitTopFingerMute = new QCheckBox("Hit Top (Finger Mute) G#4");
    m_fxHitTopOpen = new QCheckBox("Hit Top (Open) A4");
    m_fxHitRimOpen = new QCheckBox("Hit Rim (Open) A#4");
    m_fxScratch = new QCheckBox("Scratch F5");
    m_fxBreath = new QCheckBox("Breath F#5");
    m_fxSingleStringSlap = new QCheckBox("Single String Slap G5");
    m_fxLeftHandSlapNoise = new QCheckBox("Left-Hand Slap Noise G#5");
    m_fxRightHandSlapNoise = new QCheckBox("Right-Hand Slap Noise A5");
    m_fxSlideTurn4 = new QCheckBox("Fx Slide Turn 4 A#5");
    m_fxSlideTurn3 = new QCheckBox("Fx Slide Turn 3 B5");
    m_fxSlideDown4 = new QCheckBox("Fx Slide Down 4 C6");
    m_fxSlideDown3 = new QCheckBox("Fx Slide Down 3 C#6");
    fxLayout->addWidget(m_fxHitRimMute);
    fxLayout->addWidget(m_fxHitTopPalmMute);
    fxLayout->addWidget(m_fxHitTopFingerMute);
    fxLayout->addWidget(m_fxHitTopOpen);
    fxLayout->addWidget(m_fxHitRimOpen);
    fxLayout->addSpacing(6);
    fxLayout->addWidget(m_fxScratch);
    fxLayout->addWidget(m_fxBreath);
    fxLayout->addWidget(m_fxSingleStringSlap);
    fxLayout->addWidget(m_fxLeftHandSlapNoise);
    fxLayout->addWidget(m_fxRightHandSlapNoise);
    fxLayout->addSpacing(6);
    fxLayout->addWidget(m_fxSlideTurn4);
    fxLayout->addWidget(m_fxSlideTurn3);
    fxLayout->addWidget(m_fxSlideDown4);
    fxLayout->addWidget(m_fxSlideDown3);

    vstGrid->addWidget(vstTop, 0, 0, 1, 2);
    vstGrid->addWidget(artBox, 1, 0);
    vstGrid->addWidget(fxBox, 1, 1);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    grid->addWidget(rangeBox, 0, 0);
    grid->addWidget(feelBox, 0, 1);
    grid->addWidget(dynBox, 1, 0);
    grid->addWidget(lineBox, 1, 1);
    grid->addWidget(weightsBox, 2, 0);
    grid->addWidget(appBox, 2, 1);
    grid->addWidget(m_vstBox, 3, 0, 1, 2);
    grid->addWidget(m_advBox, 4, 0, 1, 2);

    root->addWidget(m_enabled);

    // Make the editor scrollable (it can be taller than the screen).
    QWidget* content = new QWidget(this);
    content->setLayout(grid);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    // --- Live output reasoning log (learning aid) ---
    {
        auto* box = new QGroupBox("Live output log (what/why the bass just played)");
        auto* v = new QVBoxLayout(box);
        v->setContentsMargins(10, 8, 10, 10);
        v->setSpacing(6);

        auto* top = new QWidget(box);
        auto* h = new QHBoxLayout(top);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);

        m_reasoningLogEnabled = new QCheckBox("Enable live reasoning log", top);
        m_reasoningLogEnabled->setToolTip("When enabled, the bass engine emits a human-readable explanation\n"
                                          "for each played note/event. Keep this off if you don't need it.");
        m_clearLogBtn = new QPushButton("Clear", top);
        m_clearLogBtn->setFixedWidth(64);

        h->addWidget(m_reasoningLogEnabled, 0);
        h->addStretch(1);
        h->addWidget(m_clearLogBtn, 0);
        top->setLayout(h);

        // IMPORTANT: use a list-based log (no text-edit/pasteboard integration).
        // This avoids a macOS AppKit crash seen when opening the dialog with a text-edit control.
        m_liveLog = new QListWidget(box);
        m_liveLog->setSelectionMode(QAbstractItemView::NoSelection);
        m_liveLog->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_liveLog->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_liveLog->setWordWrap(false);
        m_liveLog->setMinimumHeight(140);
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPointSize(std::max(9, f.pointSize()));
        m_liveLog->setFont(f);
        m_liveLog->setStyleSheet("QListWidget { background-color: #0b0b0b; color: #e6e6e6; border: 1px solid #333; }");

        v->addWidget(top);
        v->addWidget(m_liveLog, 1);
        box->setLayout(v);

        root->addWidget(box, 0);

        // Flush timer batches UI updates to avoid hammering CoreAnimation.
        m_logFlushTimer = new QTimer(this);
        m_logFlushTimer->setInterval(50);
        m_logFlushTimer->setSingleShot(false);
        connect(m_logFlushTimer, &QTimer::timeout, this, &BassStyleEditorDialog::flushPendingLog);

        connect(m_clearLogBtn, &QPushButton::clicked, this, [this]() {
            if (m_liveLog) m_liveLog->clear();
        });

        // IMPORTANT: only connect to the playback engine when the user enables logging,
        // and disconnect when disabled. This prevents bursts of UI work during dialog show/CA commit.
        connect(m_reasoningLogEnabled, &QCheckBox::toggled, this, [this](bool on) {
            setLiveLogActive(on);
        });
    }

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    root->addWidget(m_buttons);

    auto hook = [this]() { emitPreview(); };
    if (m_feelStyle) {
        connect(m_feelStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { emitPreview(); });
    }
    const auto widgets = findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        if (auto* sb = qobject_cast<QSpinBox*>(w)) connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, hook);
        else if (auto* db = qobject_cast<QDoubleSpinBox*>(w)) connect(db, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, hook);
        else if (auto* cb = qobject_cast<QCheckBox*>(w)) connect(cb, &QCheckBox::toggled, this, hook);
    }

    connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
        const auto p = profileFromUi();
        emit profileCommitted(p);
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, [this]() {
        reject();
    });
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        const auto p = profileFromUi();
        emit profileCommitted(p);
    });

    connect(m_loadPresetBtn, &QPushButton::clicked, this, [this]() {
        if (!m_presetCombo) return;
        const QString id = m_presetCombo->currentData().toString();
        music::BassPreset preset;
        if (!music::BassPresets::getById(id, preset)) return;

        // Merge preset into current UI state based on “keep” toggles.
        music::BassProfile cur = profileFromUi();
        music::BassProfile p = preset.profile;
        p.name = preset.name;

        // Keep deterministic per-song randomness unless explicitly changed by user.
        p.humanizeSeed = cur.humanizeSeed;

        if (m_keepEnable && m_keepEnable->isChecked()) {
            p.enabled = cur.enabled;
            p.midiChannel = cur.midiChannel;
        }
        if (m_keepRouting && m_keepRouting->isChecked()) {
            p.minMidiNote = cur.minMidiNote;
            p.maxMidiNote = cur.maxMidiNote;
            p.registerCenterMidi = cur.registerCenterMidi;
            p.registerRange = cur.registerRange;
            p.maxLeap = cur.maxLeap;
        }

        // Apply to UI + preview.
        setUiFromProfile(p);
        emitPreview();
    });
}

void BassStyleEditorDialog::setUiFromProfile(const music::BassProfile& p) {
    // Align preset dropdown if names match.
    if (m_presetCombo) {
        music::BassPreset found;
        if (music::BassPresets::getByName(p.name, found)) {
            const int idx = m_presetCombo->findData(found.id);
            if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
        }
    }

    m_enabled->setChecked(p.enabled);
    m_channel->setValue(p.midiChannel);
    m_minNote->setValue(p.minMidiNote);
    m_maxNote->setValue(p.maxMidiNote);
    m_registerCenter->setValue(p.registerCenterMidi);
    m_registerRange->setValue(p.registerRange);
    m_maxLeap->setValue(p.maxLeap);

    if (m_feelStyle) {
        const int idx = m_feelStyle->findData((int)p.feelStyle);
        if (idx >= 0) m_feelStyle->setCurrentIndex(idx);
    }

    m_baseVelocity->setValue(p.baseVelocity);
    m_velocityVariance->setValue(p.velocityVariance);
    m_accent1->setValue(p.accentBeat1);
    m_accent2->setValue(p.accentBeat2);
    m_accent3->setValue(p.accentBeat3);
    m_accent4->setValue(p.accentBeat4);
    if (m_phraseArc) m_phraseArc->setValue(p.phraseArcStrength);
    if (m_sectionArc) m_sectionArc->setValue(p.sectionArcStrength);

    m_jitterMs->setValue(p.microJitterMs);
    m_laidBackMs->setValue(p.laidBackMs);
    m_pushMs->setValue(p.pushMs);
    if (m_driftMaxMs) m_driftMaxMs->setValue(p.driftMaxMs);
    if (m_driftRate) m_driftRate->setValue(p.driftRate);
    if (m_attackVarMs) m_attackVarMs->setValue(p.attackVarianceMs);
    m_noteLengthMs->setValue(p.noteLengthMs);
    m_gatePct->setValue(p.gatePct);
    if (m_swingAmount) m_swingAmount->setValue(p.swingAmount);
    if (m_swingRatio) m_swingRatio->setValue(p.swingRatio);

    m_chromaticism->setValue(p.chromaticism);
    m_honorSlash->setChecked(p.honorSlashBass);
    m_slashProb->setValue(p.slashBassProb);

    if (m_intensityBase) m_intensityBase->setValue(p.intensityBase);
    if (m_intensityVar) m_intensityVar->setValue(p.intensityVariance);
    if (m_evolutionRate) m_evolutionRate->setValue(p.evolutionRate);
    if (m_sectionRamp) m_sectionRamp->setValue(p.sectionRampStrength);
    if (m_phraseBars) m_phraseBars->setValue(p.phraseLengthBars);
    if (m_ghostProb) m_ghostProb->setValue(p.ghostNoteProb);
    if (m_ghostVel) m_ghostVel->setValue(p.ghostVelocity);
    if (m_ghostGate) m_ghostGate->setValue(p.ghostGatePct);
    if (m_pickup8thProb) m_pickup8thProb->setValue(p.pickup8thProb);
    if (m_fillPhraseEnd) m_fillPhraseEnd->setValue(p.fillProbPhraseEnd);
    if (m_syncopProb) m_syncopProb->setValue(p.syncopationProb);
    if (m_twoFeelProb) m_twoFeelProb->setValue(p.twoFeelPhraseProb);
    if (m_brokenTimeProb) m_brokenTimeProb->setValue(p.brokenTimePhraseProb);
    if (m_restProb) m_restProb->setValue(p.restProb);
    if (m_tieProb) m_tieProb->setValue(p.tieProb);
    if (m_motifProb) m_motifProb->setValue(p.motifProb);
    if (m_motifStrength) m_motifStrength->setValue(p.motifStrength);
    if (m_motifVariation) m_motifVariation->setValue(p.motifVariation);
    if (auto* w = findChild<QDoubleSpinBox*>("twoBeatRunProb")) w->setValue(p.twoBeatRunProb);
    if (auto* w = findChild<QDoubleSpinBox*>("enclosureProb")) w->setValue(p.enclosureProb);
    if (auto* w = findChild<QDoubleSpinBox*>("sectionIntroRestraint")) w->setValue(p.sectionIntroRestraint);

    m_wRoot->setValue(p.wRoot);
    m_wThird->setValue(p.wThird);
    m_wFifth->setValue(p.wFifth);
    m_wSeventh->setValue(p.wSeventh);

    m_wAppChrom->setValue(p.wApproachChromatic);
    m_wAppDia->setValue(p.wApproachDiatonic);
    m_wAppEncl->setValue(p.wApproachEnclosure);

    if (m_ampleOffsetSemis) m_ampleOffsetSemis->setValue(p.ampleNoteNameOffsetSemitones);
    if (m_artSustainAccent) m_artSustainAccent->setChecked(p.artSustainAccent);
    if (m_artNaturalHarmonic) m_artNaturalHarmonic->setChecked(p.artNaturalHarmonic);
    if (m_artPalmMute) m_artPalmMute->setChecked(p.artPalmMute);
    if (m_artSlideInOut) m_artSlideInOut->setChecked(p.artSlideInOut);
    if (m_artLegatoSlide) m_artLegatoSlide->setChecked(p.artLegatoSlide);
    if (m_artHammerPull) m_artHammerPull->setChecked(p.artHammerPull);

    if (m_fxHitRimMute) m_fxHitRimMute->setChecked(p.fxHitRimMute);
    if (m_fxHitTopPalmMute) m_fxHitTopPalmMute->setChecked(p.fxHitTopPalmMute);
    if (m_fxHitTopFingerMute) m_fxHitTopFingerMute->setChecked(p.fxHitTopFingerMute);
    if (m_fxHitTopOpen) m_fxHitTopOpen->setChecked(p.fxHitTopOpen);
    if (m_fxHitRimOpen) m_fxHitRimOpen->setChecked(p.fxHitRimOpen);
    if (m_fxScratch) m_fxScratch->setChecked(p.fxScratch);
    if (m_fxBreath) m_fxBreath->setChecked(p.fxBreath);
    if (m_fxSingleStringSlap) m_fxSingleStringSlap->setChecked(p.fxSingleStringSlap);
    if (m_fxLeftHandSlapNoise) m_fxLeftHandSlapNoise->setChecked(p.fxLeftHandSlapNoise);
    if (m_fxRightHandSlapNoise) m_fxRightHandSlapNoise->setChecked(p.fxRightHandSlapNoise);
    if (m_fxSlideTurn4) m_fxSlideTurn4->setChecked(p.fxSlideTurn4);
    if (m_fxSlideTurn3) m_fxSlideTurn3->setChecked(p.fxSlideTurn3);
    if (m_fxSlideDown4) m_fxSlideDown4->setChecked(p.fxSlideDown4);
    if (m_fxSlideDown3) m_fxSlideDown3->setChecked(p.fxSlideDown3);

    // NOTE: We intentionally do NOT auto-activate the live log on dialog open, even if it was
    // previously enabled. On some macOS setups, attaching live-updating views during window show
    // can trigger unstable AppKit/CoreAnimation behavior. The user can re-enable it explicitly.
    if (m_reasoningLogEnabled) {
        const bool prev = m_reasoningLogEnabled->blockSignals(true);
        m_reasoningLogEnabled->setChecked(p.reasoningLogEnabled);
        m_reasoningLogEnabled->blockSignals(prev);
    }
    setLiveLogActive(false);
}

music::BassProfile BassStyleEditorDialog::profileFromUi() const {
    music::BassProfile p = m_initial;

    p.enabled = m_enabled->isChecked();
    p.midiChannel = m_channel->value();
    p.minMidiNote = m_minNote->value();
    p.maxMidiNote = m_maxNote->value();
    if (p.minMidiNote > p.maxMidiNote) std::swap(p.minMidiNote, p.maxMidiNote);
    p.registerCenterMidi = m_registerCenter->value();
    p.registerRange = m_registerRange->value();
    p.maxLeap = m_maxLeap->value();

    if (m_feelStyle) {
        p.feelStyle = (music::BassFeelStyle)m_feelStyle->currentData().toInt();
    }

    p.baseVelocity = m_baseVelocity->value();
    p.velocityVariance = m_velocityVariance->value();
    p.accentBeat1 = m_accent1->value();
    p.accentBeat2 = m_accent2->value();
    p.accentBeat3 = m_accent3->value();
    p.accentBeat4 = m_accent4->value();
    if (m_phraseArc) p.phraseArcStrength = m_phraseArc->value();
    if (m_sectionArc) p.sectionArcStrength = m_sectionArc->value();

    p.microJitterMs = m_jitterMs->value();
    p.laidBackMs = m_laidBackMs->value();
    p.pushMs = m_pushMs->value();
    if (m_driftMaxMs) p.driftMaxMs = m_driftMaxMs->value();
    if (m_driftRate) p.driftRate = m_driftRate->value();
    if (m_attackVarMs) p.attackVarianceMs = m_attackVarMs->value();
    p.noteLengthMs = m_noteLengthMs->value();
    p.gatePct = m_gatePct->value();
    if (m_swingAmount) p.swingAmount = m_swingAmount->value();
    if (m_swingRatio) p.swingRatio = m_swingRatio->value();

    p.chromaticism = m_chromaticism->value();
    p.honorSlashBass = m_honorSlash->isChecked();
    p.slashBassProb = m_slashProb->value();

    if (m_intensityBase) p.intensityBase = m_intensityBase->value();
    if (m_intensityVar) p.intensityVariance = m_intensityVar->value();
    if (m_evolutionRate) p.evolutionRate = m_evolutionRate->value();
    if (m_sectionRamp) p.sectionRampStrength = m_sectionRamp->value();
    if (m_phraseBars) p.phraseLengthBars = m_phraseBars->value();
    if (m_ghostProb) p.ghostNoteProb = m_ghostProb->value();
    if (m_ghostVel) p.ghostVelocity = m_ghostVel->value();
    if (m_ghostGate) p.ghostGatePct = m_ghostGate->value();
    if (m_pickup8thProb) p.pickup8thProb = m_pickup8thProb->value();
    if (m_fillPhraseEnd) p.fillProbPhraseEnd = m_fillPhraseEnd->value();
    if (m_syncopProb) p.syncopationProb = m_syncopProb->value();
    if (m_twoFeelProb) p.twoFeelPhraseProb = m_twoFeelProb->value();
    if (m_brokenTimeProb) p.brokenTimePhraseProb = m_brokenTimeProb->value();
    if (m_restProb) p.restProb = m_restProb->value();
    if (m_tieProb) p.tieProb = m_tieProb->value();
    if (m_motifProb) p.motifProb = m_motifProb->value();
    if (m_motifStrength) p.motifStrength = m_motifStrength->value();
    if (m_motifVariation) p.motifVariation = m_motifVariation->value();
    if (auto* w = findChild<QDoubleSpinBox*>("twoBeatRunProb")) p.twoBeatRunProb = w->value();
    if (auto* w = findChild<QDoubleSpinBox*>("enclosureProb")) p.enclosureProb = w->value();
    if (auto* w = findChild<QDoubleSpinBox*>("sectionIntroRestraint")) p.sectionIntroRestraint = w->value();

    // Update label to match preset dropdown (if present).
    if (m_presetCombo) {
        p.name = m_presetCombo->currentText().trimmed();
    }

    p.wRoot = m_wRoot->value();
    p.wThird = m_wThird->value();
    p.wFifth = m_wFifth->value();
    p.wSeventh = m_wSeventh->value();

    p.wApproachChromatic = m_wAppChrom->value();
    p.wApproachDiatonic = m_wAppDia->value();
    p.wApproachEnclosure = m_wAppEncl->value();

    if (m_ampleOffsetSemis) p.ampleNoteNameOffsetSemitones = m_ampleOffsetSemis->value();
    if (m_artSustainAccent) p.artSustainAccent = m_artSustainAccent->isChecked();
    if (m_artNaturalHarmonic) p.artNaturalHarmonic = m_artNaturalHarmonic->isChecked();
    if (m_artPalmMute) p.artPalmMute = m_artPalmMute->isChecked();
    if (m_artSlideInOut) p.artSlideInOut = m_artSlideInOut->isChecked();
    if (m_artLegatoSlide) p.artLegatoSlide = m_artLegatoSlide->isChecked();
    if (m_artHammerPull) p.artHammerPull = m_artHammerPull->isChecked();

    if (m_fxHitRimMute) p.fxHitRimMute = m_fxHitRimMute->isChecked();
    if (m_fxHitTopPalmMute) p.fxHitTopPalmMute = m_fxHitTopPalmMute->isChecked();
    if (m_fxHitTopFingerMute) p.fxHitTopFingerMute = m_fxHitTopFingerMute->isChecked();
    if (m_fxHitTopOpen) p.fxHitTopOpen = m_fxHitTopOpen->isChecked();
    if (m_fxHitRimOpen) p.fxHitRimOpen = m_fxHitRimOpen->isChecked();
    if (m_fxScratch) p.fxScratch = m_fxScratch->isChecked();
    if (m_fxBreath) p.fxBreath = m_fxBreath->isChecked();
    if (m_fxSingleStringSlap) p.fxSingleStringSlap = m_fxSingleStringSlap->isChecked();
    if (m_fxLeftHandSlapNoise) p.fxLeftHandSlapNoise = m_fxLeftHandSlapNoise->isChecked();
    if (m_fxRightHandSlapNoise) p.fxRightHandSlapNoise = m_fxRightHandSlapNoise->isChecked();
    if (m_fxSlideTurn4) p.fxSlideTurn4 = m_fxSlideTurn4->isChecked();
    if (m_fxSlideTurn3) p.fxSlideTurn3 = m_fxSlideTurn3->isChecked();
    if (m_fxSlideDown4) p.fxSlideDown4 = m_fxSlideDown4->isChecked();
    if (m_fxSlideDown3) p.fxSlideDown3 = m_fxSlideDown3->isChecked();

    if (m_reasoningLogEnabled) p.reasoningLogEnabled = m_reasoningLogEnabled->isChecked();

    return p;
}

void BassStyleEditorDialog::emitPreview() {
    emit profilePreview(profileFromUi());
}

void BassStyleEditorDialog::appendLiveLogLine(const QString& line) {
    // Do not touch UI here; this may be delivered during sensitive CA transactions.
    if (!m_reasoningLogEnabled || !m_reasoningLogEnabled->isChecked()) return;
    const QString t = line.trimmed();
    if (t.isEmpty()) return;
    m_pendingLog.push_back(t);
}

void BassStyleEditorDialog::setLiveLogActive(bool active) {
    // Disconnect first to be safe.
    if (m_logConn) {
        QObject::disconnect(m_logConn);
        m_logConn = QMetaObject::Connection{};
    }
    if (m_logFlushTimer) {
        if (!active) m_logFlushTimer->stop();
        else if (!m_logFlushTimer->isActive()) m_logFlushTimer->start();
    }
    if (!active) {
        m_pendingLog.clear();
        return;
    }
    if (!m_playback) return;
    // Connect only while enabled.
    m_logConn = connect(m_playback, &playback::BandPlaybackEngine::bassLogLine,
                        this, &BassStyleEditorDialog::appendLiveLogLine, Qt::QueuedConnection);
}

void BassStyleEditorDialog::flushPendingLog() {
    if (!m_liveLog || !m_reasoningLogEnabled || !m_reasoningLogEnabled->isChecked()) return;
    if (m_pendingLog.isEmpty()) return;

    // Drain at most N lines per tick to keep UI smooth.
    constexpr int kMaxDrain = 40;
    const int n = std::min<int>(kMaxDrain, int(m_pendingLog.size()));
    for (int i = 0; i < n; ++i) {
        m_liveLog->addItem(m_pendingLog[i]);
    }
    m_pendingLog.erase(m_pendingLog.begin(), m_pendingLog.begin() + n);

    // Keep bounded history (avoid memory growth).
    constexpr int kMaxLines = 300;
    while (m_liveLog->count() > kMaxLines) {
        delete m_liveLog->takeItem(0);
    }
    m_liveLog->scrollToBottom();
}

