#include "BassStyleEditorDialog.h"

#include <QtWidgets>

BassStyleEditorDialog::BassStyleEditorDialog(const music::BassProfile& initial, QWidget* parent)
    : QDialog(parent),
      m_initial(initial) {
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
    m_jitterMs = makeSpin(0, 50);
    m_laidBackMs = makeSpin(-50, 50);
    m_pushMs = makeSpin(-50, 50);
    m_noteLengthMs = makeSpin(0, 2000);
    m_gatePct = makeD(0.05, 1.0, 0.01, 2);
    feelForm->addRow("Micro jitter (ms +/-)", m_jitterMs);
    feelForm->addRow("Laid back (ms)", m_laidBackMs);
    feelForm->addRow("Push (ms)", m_pushMs);
    feelForm->addRow("Note length (ms; 0=gate)", m_noteLengthMs);
    feelForm->addRow("Gate (% of beat)", m_gatePct);

    // --- Dynamics ---
    auto* dynBox = new QGroupBox("Dynamics");
    auto* dynForm = new QFormLayout(dynBox);
    m_baseVelocity = makeSpin(1, 127);
    m_velocityVariance = makeSpin(0, 64);
    m_accent1 = makeD(0.1, 2.0, 0.02, 2);
    m_accent2 = makeD(0.1, 2.0, 0.02, 2);
    m_accent3 = makeD(0.1, 2.0, 0.02, 2);
    m_accent4 = makeD(0.1, 2.0, 0.02, 2);
    dynForm->addRow("Base velocity", m_baseVelocity);
    dynForm->addRow("Velocity variance (+/-)", m_velocityVariance);
    dynForm->addRow("Accent beat 1", m_accent1);
    dynForm->addRow("Accent beat 2", m_accent2);
    dynForm->addRow("Accent beat 3", m_accent3);
    dynForm->addRow("Accent beat 4", m_accent4);

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

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    grid->addWidget(rangeBox, 0, 0);
    grid->addWidget(feelBox, 0, 1);
    grid->addWidget(dynBox, 1, 0);
    grid->addWidget(lineBox, 1, 1);
    grid->addWidget(weightsBox, 2, 0);
    grid->addWidget(appBox, 2, 1);
    grid->addWidget(m_advBox, 3, 0, 1, 2);

    root->addWidget(m_enabled);

    // Make the editor scrollable (it can be taller than the screen).
    QWidget* content = new QWidget(this);
    content->setLayout(grid);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    root->addWidget(m_buttons);

    auto hook = [this]() { emitPreview(); };
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
}

void BassStyleEditorDialog::setUiFromProfile(const music::BassProfile& p) {
    m_enabled->setChecked(p.enabled);
    m_channel->setValue(p.midiChannel);
    m_minNote->setValue(p.minMidiNote);
    m_maxNote->setValue(p.maxMidiNote);
    m_registerCenter->setValue(p.registerCenterMidi);
    m_registerRange->setValue(p.registerRange);
    m_maxLeap->setValue(p.maxLeap);

    m_baseVelocity->setValue(p.baseVelocity);
    m_velocityVariance->setValue(p.velocityVariance);
    m_accent1->setValue(p.accentBeat1);
    m_accent2->setValue(p.accentBeat2);
    m_accent3->setValue(p.accentBeat3);
    m_accent4->setValue(p.accentBeat4);

    m_jitterMs->setValue(p.microJitterMs);
    m_laidBackMs->setValue(p.laidBackMs);
    m_pushMs->setValue(p.pushMs);
    m_noteLengthMs->setValue(p.noteLengthMs);
    m_gatePct->setValue(p.gatePct);

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

    p.baseVelocity = m_baseVelocity->value();
    p.velocityVariance = m_velocityVariance->value();
    p.accentBeat1 = m_accent1->value();
    p.accentBeat2 = m_accent2->value();
    p.accentBeat3 = m_accent3->value();
    p.accentBeat4 = m_accent4->value();

    p.microJitterMs = m_jitterMs->value();
    p.laidBackMs = m_laidBackMs->value();
    p.pushMs = m_pushMs->value();
    p.noteLengthMs = m_noteLengthMs->value();
    p.gatePct = m_gatePct->value();

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
    if (auto* w = findChild<QDoubleSpinBox*>("twoBeatRunProb")) p.twoBeatRunProb = w->value();
    if (auto* w = findChild<QDoubleSpinBox*>("enclosureProb")) p.enclosureProb = w->value();
    if (auto* w = findChild<QDoubleSpinBox*>("sectionIntroRestraint")) p.sectionIntroRestraint = w->value();

    p.wRoot = m_wRoot->value();
    p.wThird = m_wThird->value();
    p.wFifth = m_wFifth->value();
    p.wSeventh = m_wSeventh->value();

    p.wApproachChromatic = m_wAppChrom->value();
    p.wApproachDiatonic = m_wAppDia->value();
    p.wApproachEnclosure = m_wAppEncl->value();

    return p;
}

void BassStyleEditorDialog::emitPreview() {
    emit profilePreview(profileFromUi());
}

