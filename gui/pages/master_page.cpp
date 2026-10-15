#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "pages/pages.h"
#include "theme.h"
#include "widgets.h"

#if SANITY_HAVE_QTMM
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#endif

using namespace sanity;

namespace gui {

namespace {
QString pct(double v) { return QStringLiteral("%1 %").arg(std::lround(v * 100)); }
}  // namespace

MasterPage::MasterPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* page = new Page("Sanity Master",
                          "Pulizia e livellamento automatico: ronzio, click, rumore, riverbero, "
                          "sibilanti, filler e loudness secondo gli standard.",
                          "In attesa", this);
    outer->addWidget(page);
    auto* col = page->column();
    const int at = col->count() - 1;

    // ------------------------------------------------------------- audio
    audio_card_ = new Card(QStringLiteral("Materiale"),
                           QStringLiteral("Registra dal vivo oppure carica un file gia esistente."));
    {
        auto* load = new QPushButton(QStringLiteral("Carica file audio..."));
        connect(load, &QPushButton::clicked, this, [this] {
            // The file dialog lives in the main window; ask it through the
            // status bar for a single entry point.
            AppState::get().status_message(
                QStringLiteral("Usa File > Apri per caricare un audio"));
        });
        auto* info = new QLabel();
        info->setObjectName("Mono");
        info->setWordWrap(true);
        audio_card_->add_widget(load);
        audio_card_->add_widget(info);
        connect(&AppState::get(), &AppState::audio_changed, this, [info] {
            const Session& s = AppState::get().session();
            info->setText(s.has_audio ? describe_audio(s.audio)
                                      : QStringLiteral("Nessun audio caricato."));
        });
        const Session& s = AppState::get().session();
        info->setText(s.has_audio ? describe_audio(s.audio)
                                  : QStringLiteral("Nessun audio caricato."));
    }
    col->insertWidget(at, audio_card_);

    // ------------------------------------------------------------ waveform
    {
        auto* wave_card = new Card(QStringLiteral("Forma d'onda"),
                                   QStringLiteral("Le zone evidenziate sono i filler rimossi."));
        wave_ = new WaveformView();
        wave_card->add_widget(wave_);
        col->insertWidget(at + 1, wave_card);
    }

    // --------------------------------------------------------------- chain
    chain_card_ = new Card(QStringLiteral("Catena di elaborazione"),
                           QStringLiteral("Ogni blocco e indipendente: spegni quello che non "
                                          "serve e riascolta."));
    {
        preset_ = new QComboBox();
        preset_->addItem(QStringLiteral("Voce (parlato)"), 0);
        preset_->addItem(QStringLiteral("Musica"), 1);
        connect(preset_, &QComboBox::currentIndexChanged, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("Preset"), preset_,
                                         QStringLiteral("Voce usa il target mono -19 LUFS, "
                                                        "musica il target stereo -16 LUFS.")));

        power_clean_ = new QCheckBox(QStringLiteral("Attivo"));
        connect(power_clean_, &QCheckBox::toggled, this, [this](bool on) {
            dereverb_->setChecked(on);
            sync_params();
        });
        chain_card_->add_widget(make_row(QStringLiteral("Power Clean (denoise + de-reverb)"),
                                         power_clean_,
                                         QStringLiteral("Rumore di fondo e riverbero insieme.")));

        denoise_ = new QSlider(Qt::Horizontal);
        denoise_->setRange(0, 200);
        denoise_->setValue(160);
        denoise_label_ = new QLabel(QStringLiteral("1.60"));
        denoise_label_->setObjectName("Mono");
        connect(denoise_, &QSlider::valueChanged, this, [this](int v) {
            denoise_label_->setText(QString::number(v / 100.0, 'f', 2));
            sync_params();
        });
        auto* dn = new QWidget();
        auto* dnhl = new QHBoxLayout(dn);
        dnhl->setContentsMargins(0, 0, 0, 0);
        dnhl->addWidget(denoise_, 1);
        dnhl->addWidget(denoise_label_);
        chain_card_->add_widget(make_row(QStringLiteral("Intensita denoiser"), dn,
                                         QStringLiteral("1.6 e il valore di Power Clean; "
                                                        "0 disattiva.")));

        dereverb_ = new QCheckBox(QStringLiteral("Attivo"));
        connect(dereverb_, &QCheckBox::toggled, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("De-reverb"), dereverb_,
                                         QStringLiteral("Riduce la coda della stanza.")));

        highpass_ = new QCheckBox(QStringLiteral("Attivo"));
        highpass_->setChecked(true);
        connect(highpass_, &QCheckBox::toggled, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("Passa-alto adattivo"), highpass_,
                                         QStringLiteral("Taglia il rimbombo sotto la fondamentale "
                                                        "della voce.")));

        dehum_ = new QCheckBox(QStringLiteral("Attivo"));
        dehum_->setChecked(true);
        connect(dehum_, &QCheckBox::toggled, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("Anti-ronzio di rete"), dehum_));

        declick_ = new QCheckBox(QStringLiteral("Attivo"));
        declick_->setChecked(true);
        connect(declick_, &QCheckBox::toggled, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("Riparazione click"), declick_));

        deess_ = new QCheckBox(QStringLiteral("Attivo"));
        deess_->setChecked(true);
        connect(deess_, &QCheckBox::toggled, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("De-esser"), deess_));

        leveler_ = new QCheckBox(QStringLiteral("Attivo"));
        leveler_->setChecked(true);
        connect(leveler_, &QCheckBox::toggled, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("Smart Adaptive Leveling"), leveler_,
                                         QStringLiteral("Uniforma il livello delle frasi.")));

        leveler_strength_ = new QSlider(Qt::Horizontal);
        leveler_strength_->setRange(0, 100);
        leveler_strength_->setValue(100);
        leveler_strength_label_ = new QLabel(QStringLiteral("100 %"));
        leveler_strength_label_->setObjectName("Mono");
        connect(leveler_strength_, &QSlider::valueChanged, this, [this](int v) {
            leveler_strength_label_->setText(pct(v / 100.0));
            sync_params();
        });
        auto* ls = new QWidget();
        auto* lshl = new QHBoxLayout(ls);
        lshl->setContentsMargins(0, 0, 0, 0);
        lshl->addWidget(leveler_strength_, 1);
        lshl->addWidget(leveler_strength_label_);
        chain_card_->add_widget(make_row(QStringLiteral("Forza leveler"), ls));

        compress_ = new QCheckBox(QStringLiteral("Attivo"));
        compress_->setChecked(true);
        connect(compress_, &QCheckBox::toggled, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("Compressore"), compress_));

        normalize_ = new QCheckBox(QStringLiteral("Attivo"));
        normalize_->setChecked(true);
        connect(normalize_, &QCheckBox::toggled, this, [this] { sync_params(); });
        chain_card_->add_widget(make_row(QStringLiteral("Normalizzazione loudness + true peak"),
                                         normalize_,
                                         QStringLiteral("Porta il mix al target e al tetto "
                                                        "dBTP.")));
    }
    col->insertWidget(at + 2, chain_card_);

    // -------------------------------------------------------------- filler
    filler_card_ = new Card(QStringLiteral("Filler Killer"),
                            QStringLiteral("Rimuove esitazioni, 'ehm' e pause vuote senza "
                                           "toccare le parole."));
    {
        filler_ = new QComboBox();
        filler_->addItem(QStringLiteral("Disattivato"),
                         static_cast<int>(sanity::FillerMode::Off));
        filler_->addItem(QStringLiteral("Prudente (consigliato)"),
                         static_cast<int>(sanity::FillerMode::Cautious));
        filler_->addItem(QStringLiteral("Bilanciato"),
                         static_cast<int>(sanity::FillerMode::Balanced));
        filler_->addItem(QStringLiteral("Aggressivo"),
                         static_cast<int>(sanity::FillerMode::Aggressive));
        filler_->setCurrentIndex(1);
        connect(filler_, &QComboBox::currentIndexChanged, this, [this] { sync_params(); });
        filler_card_->add_widget(make_row(QStringLiteral("Modalita"), filler_,
                                          QStringLiteral("Prudente elimina i filler con margine "
                                                         "di sicurezza, senza tagliare parole.")));
        candidates_btn_ = new QPushButton(QStringLiteral("Mostra cosa rimuoverebbe"));
        connect(candidates_btn_, &QPushButton::clicked, this, &MasterPage::show_candidates);
        filler_card_->add_widget(candidates_btn_);
        candidates_ = new QTableWidget(0, 4);
        candidates_->setHorizontalHeaderLabels({QStringLiteral("Inizio"),
                                                QStringLiteral("Durata"),
                                                QStringLiteral("Punteggio"),
                                                QStringLiteral("Azione")});
        candidates_->horizontalHeader()->setStretchLastSection(true);
        candidates_->verticalHeader()->setVisible(false);
        candidates_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        candidates_->setMaximumHeight(180);
        filler_card_->add_widget(candidates_);
    }
    col->insertWidget(at + 3, filler_card_);

    // -------------------------------------------------------------- result
    result_card_ = new Card(QStringLiteral("Risultato"),
                            QStringLiteral("Confronto prima e dopo, con lo stato di conformita."));
    {
        auto* tiles = new QGridLayout();
        tiles->setHorizontalSpacing(10);
        tiles->setVerticalSpacing(10);
        tile_lufs_before_ = new StatTile(QStringLiteral("LUFS prima"), QStringLiteral("LUFS"));
        tile_lufs_after_ = new StatTile(QStringLiteral("LUFS dopo"), QStringLiteral("LUFS"));
        tile_tp_before_ = new StatTile(QStringLiteral("True peak prima"), QStringLiteral("dBTP"));
        tile_tp_after_ = new StatTile(QStringLiteral("True peak dopo"), QStringLiteral("dBTP"));
        tile_fillers_ = new StatTile(QStringLiteral("Filler rimossi"), QStringLiteral(""));
        tile_time_ = new StatTile(QStringLiteral("Tempo di calcolo"), QStringLiteral(""));
        tiles->addWidget(tile_lufs_before_, 0, 0);
        tiles->addWidget(tile_lufs_after_, 0, 1);
        tiles->addWidget(tile_tp_before_, 0, 2);
        tiles->addWidget(tile_tp_after_, 0, 3);
        tiles->addWidget(tile_fillers_, 1, 0);
        tiles->addWidget(tile_time_, 1, 1);
        auto* host = new QWidget();
        host->setLayout(tiles);
        result_card_->add_widget(host);

        verdict_ = new QLabel(QStringLiteral("Nessuna elaborazione eseguita."));
        verdict_->setWordWrap(true);
        result_card_->add_widget(verdict_);

        progress_ = new QProgressBar();
        progress_->setRange(0, 0);
        progress_->hide();
        elapsed_ = new QLabel(QStringLiteral(""));
        elapsed_->setObjectName("Hint");
        process_ = new QPushButton(QStringLiteral("Elabora con Sanity Master"));
        process_->setObjectName("Primary");
        connect(process_, &QPushButton::clicked, this, &MasterPage::process);
        result_card_->add_widget(progress_);
        result_card_->add_widget(make_row(QStringLiteral("Stato"), elapsed_));
        result_card_->add_widget(process_);

        play_before_ = new QPushButton(QStringLiteral("Ascolta originale"));
        play_after_ = new QPushButton(QStringLiteral("Ascolta master"));
        play_before_->setEnabled(false);
        play_after_->setEnabled(false);
        connect(play_before_, &QPushButton::clicked, this, [this] { play("before"); });
        connect(play_after_, &QPushButton::clicked, this, [this] { play("after"); });
        auto* ab = new QWidget();
        auto* abhl = new QHBoxLayout(ab);
        abhl->setContentsMargins(0, 0, 0, 0);
        abhl->addWidget(play_before_);
        abhl->addWidget(play_after_);
        abhl->addStretch(1);
        result_card_->add_widget(ab);
    }
    col->insertWidget(at + 4, result_card_);

    // -------------------------------------------------------------- stages
    stages_card_ = new Card(QStringLiteral("Diagnostica per stadio"),
                            QStringLiteral("Tempi, RTF e metriche di ogni blocco: la stessa "
                                           "informazione del report JSON."));
    stages_ = new QTableWidget(0, 5);
    stages_->setHorizontalHeaderLabels({QStringLiteral("Stadio"), QStringLiteral("Attivo"),
                                        QStringLiteral("Tempo"), QStringLiteral("RTF"),
                                        QStringLiteral("Metriche")});
    stages_->horizontalHeader()->setStretchLastSection(true);
    stages_->verticalHeader()->setVisible(false);
    stages_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stages_->setMinimumHeight(240);
    stages_card_->add_widget(stages_);
    col->insertWidget(at + 5, stages_card_);

    connect(&AppState::get(), &AppState::mastering_started, this, [this] {
        processing_ = true;
        since_start_.start();
        progress_->show();
        process_->setEnabled(false);
        result_card_->set_state(State::Busy, QStringLiteral("Elaborazione"));
        if (!elapsed_timer_) {
            elapsed_timer_ = new QTimer(this);
            elapsed_timer_->setInterval(200);
            connect(elapsed_timer_, &QTimer::timeout, this, [this] {
                elapsed_->setText(QStringLiteral("Elaborazione in corso da %1 s. La pipeline "
                                                 "elabora a circa 5-6x la velocita reale.")
                                      .arg(since_start_.elapsed() / 1000.0, 0, 'f', 1));
            });
        }
        elapsed_timer_->start();
    });
    connect(&AppState::get(), &AppState::mastering_finished, this, [this](bool ok, const QString& err) {
        processing_ = false;
        progress_->hide();
        if (elapsed_timer_) elapsed_timer_->stop();
        process_->setEnabled(true);
        if (!ok) {
            result_card_->set_state(State::Error, QStringLiteral("Errore"));
            verdict_->setText(err);
            return;
        }
        const Report& r = AppState::get().session().report;
        elapsed_->setText(QStringLiteral("Completato in %1 ms (RTF %2).")
                              .arg(std::lround(r.total_ms))
                              .arg(QString::number(r.rtf, 'f', 2)));
        show_report();
    });
    connect(&AppState::get(), &AppState::audio_changed, this, &MasterPage::refresh_from_state);
    load_params(AppState::get().default_params());
}

void MasterPage::load_params(const sanity::Params& p) {
    preset_->setCurrentIndex(p.preset == sanity::Preset::Music ? 1 : 0);
    power_clean_->setChecked(p.power_clean);
    dereverb_->setChecked(p.dereverb);
    denoise_->setValue(static_cast<int>(std::lround((p.denoise_strength < 0 ? 1.6
                                                                           : p.denoise_strength) *
                                                    100)));
    highpass_->setChecked(p.adaptive_highpass);
    dehum_->setChecked(p.dehum);
    declick_->setChecked(p.declick);
    deess_->setChecked(p.deess);
    leveler_->setChecked(p.adaptive_leveler);
    leveler_strength_->setValue(static_cast<int>(std::lround(p.leveler_strength * 100)));
    compress_->setChecked(p.compress);
    normalize_->setChecked(p.normalize);
    for (int i = 0; i < filler_->count(); i++)
        if (filler_->itemData(i).toInt() == static_cast<int>(p.filler)) filler_->setCurrentIndex(i);
}

void MasterPage::sync_params() {
    // Start from the live parameters so the targets set on the standards page
    // survive, then overwrite the chain switches this page owns.
    sanity::Params p = AppState::get().session().params;
    p.preset = preset_->currentIndex() == 1 ? sanity::Preset::Music : sanity::Preset::Talk;
    p.power_clean = power_clean_->isChecked();
    p.dereverb = dereverb_->isChecked();
    p.denoise_strength = denoise_->value() / 100.0;
    p.adaptive_highpass = highpass_->isChecked();
    p.dehum = dehum_->isChecked();
    p.declick = declick_->isChecked();
    p.deess = deess_->isChecked();
    p.adaptive_leveler = leveler_->isChecked();
    p.leveler_strength = leveler_strength_->value() / 100.0;
    p.compress = compress_->isChecked();
    p.normalize = normalize_->isChecked();
    p.filler = static_cast<sanity::FillerMode>(filler_->currentData().toInt());
    // Keep the standards page targets.
    AppState::get().set_params(p);
}

void MasterPage::process() {
    if (processing_) return;
    sync_params();
    AppState::get().start_mastering(AppState::get().session().params);
}

void MasterPage::refresh_from_state() {
    const Session& s = AppState::get().session();
    if (s.has_audio) {
        wave_->set_samples(sanity::to_mono(s.audio), s.audio.sample_rate);
        audio_card_->set_state(State::Ready, QStringLiteral("Caricato"));
    } else {
        wave_->clear();
        audio_card_->set_state(State::Idle, QStringLiteral("Vuoto"));
    }
}

void MasterPage::build_waveform() {
    const Session& s = AppState::get().session();
    if (!s.has_audio) return;
    if (s.has_mastered)
        wave_->set_samples(sanity::to_mono(s.mastered), s.mastered.sample_rate);
    else
        wave_->set_samples(sanity::to_mono(s.audio), s.audio.sample_rate);
    QVector<QPair<double, double>> ranges;
    const double rate = s.audio.sample_rate;
    for (const FillerHit& h : s.report.fillers)
        ranges.push_back({h.start / rate, h.end / rate});
    wave_->set_markers(ranges.isEmpty()
                           ? QString()
                           : QStringLiteral("%1 filler rimossi").arg(ranges.size()),
                       ranges);
}

void MasterPage::show_report() {
    const Report& r = AppState::get().session().report;
    tile_lufs_before_->set_value(fmt_lufs(r.before.integrated_lufs));
    tile_lufs_after_->set_value(fmt_lufs(r.after.integrated_lufs));
    tile_tp_before_->set_value(fmt_db(r.before.true_peak_db));
    tile_tp_after_->set_value(fmt_db(r.after.true_peak_db));
    tile_fillers_->set_value(QString::number(r.fillers_removed));
    tile_time_->set_value(fmt_ms(r.total_ms));

    const double target = 0.0;
    (void)target;
    tile_lufs_after_->set_state(r.conformant_loudness ? State::Ready : State::Warning);
    tile_tp_after_->set_state(r.conformant_true_peak ? State::Ready : State::Warning);

    verdict_->setText(QStringLiteral("Loudness %1 LUFS, true peak %2 dBTP. %3")
                          .arg(fmt_lufs(r.after.integrated_lufs))
                          .arg(fmt_db(r.after.true_peak_db))
                          .arg(r.conformant ? QStringLiteral("Conforme agli standard.")
                                            : QStringLiteral("Non conforme: controlla i target.")));
    result_card_->set_state(r.conformant ? State::Ready : State::Warning,
                            r.conformant ? QStringLiteral("Conforme")
                                         : QStringLiteral("Da rivedere"));
    play_before_->setEnabled(true);
    play_after_->setEnabled(true);

    build_stage_table();
    build_waveform();

    for (const std::string& w : r.warnings) verdict_->setText(verdict_->text() + "\n" +
                                                              QString::fromStdString(w));
}

void MasterPage::build_stage_table() {
    const Report& r = AppState::get().session().report;
    stages_->setRowCount(static_cast<int>(r.stages.size()));
    for (int i = 0; i < static_cast<int>(r.stages.size()); i++) {
        const StageTiming& s = r.stages[i];
        stages_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(s.name)));
        stages_->setItem(i, 1, new QTableWidgetItem(s.enabled ? QStringLiteral("si")
                                                             : QStringLiteral("-")));
        stages_->setItem(i, 2, new QTableWidgetItem(s.enabled ? fmt_ms(s.ms) : QString()));
        stages_->setItem(i, 3,
                         new QTableWidgetItem(s.enabled ? QString::number(s.rtf, 'f', 3) : QString()));
        QStringList m;
        for (const auto& kv : s.metrics)
            m << QStringLiteral("%1=%2").arg(QString::fromStdString(kv.first))
                     .arg(QString::number(kv.second, 'f', 2));
        stages_->setItem(i, 4, new QTableWidgetItem(m.join(QStringLiteral(", "))));
    }
    stages_->resizeColumnsToContents();
}

void MasterPage::show_candidates() {
    const Session& s = AppState::get().session();
    if (!s.has_audio) {
        candidates_->setRowCount(0);
        return;
    }
    std::vector<FillerCandidate> cands;
    const auto mode = static_cast<sanity::FillerMode>(filler_->currentData().toInt());
    const std::vector<float> mono = sanity::to_mono(s.audio);
    if (mode != sanity::FillerMode::Off)
        sanity::detect_fillers(mono, s.audio.sample_rate, mode, &cands);
    std::sort(cands.begin(), cands.end(),
              [](const FillerCandidate& a, const FillerCandidate& b) { return a.score > b.score; });
    candidates_->setRowCount(static_cast<int>(cands.size()));
    for (int i = 0; i < static_cast<int>(cands.size()); i++) {
        const FillerCandidate& c = cands[i];
        candidates_->setItem(i, 0, new QTableWidgetItem(
                                       fmt_seconds(c.start / static_cast<double>(s.audio.sample_rate))));
        candidates_->setItem(i, 1, new QTableWidgetItem(fmt_ms(c.duration_ms)));
        candidates_->setItem(i, 2, new QTableWidgetItem(QString::number(c.score, 'f', 3)));
        candidates_->setItem(i, 3, new QTableWidgetItem(c.removed ? QStringLiteral("Rimosso")
                                                                 : QStringLiteral("Mantenuto")));
    }
    candidates_->resizeColumnsToContents();
    filler_card_->set_state(State::Ready,
                            QStringLiteral("%1 candidati").arg(cands.size()));
}

void MasterPage::play(const QString& which) {
#if SANITY_HAVE_QTMM
    const Session& s = AppState::get().session();
    const sanity::AudioBuffer& b = which == "after" ? s.mastered : s.audio;
    if (b.data.empty()) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = dir + QStringLiteral("/sanity-preview-%1.wav").arg(which);
    std::string err;
    if (!sanity::wav_write(path.toStdString(), b, true, &err)) return;
    static QMediaPlayer* player = nullptr;
    static QAudioOutput* audio = nullptr;
    if (!player) {
        player = new QMediaPlayer(this);
        audio = new QAudioOutput(this);
        player->setAudioOutput(audio);
        audio->setVolume(0.8);
    }
    player->setSource(QUrl::fromLocalFile(path));
    player->play();
    AppState::get().status_message(QStringLiteral("Riproduzione %1")
                                       .arg(which == "after" ? QStringLiteral("del master")
                                                             : QStringLiteral("dell'originale")));
#else
    (void)which;
    AppState::get().status_message(QStringLiteral("Riproduzione non disponibile in questa build"));
#endif
}

}  // namespace gui
