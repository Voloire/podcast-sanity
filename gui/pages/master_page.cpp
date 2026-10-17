#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "pages/pages.h"
#include "sanity/dynamics.hpp"
#include "sanity/music_bed.hpp"
#include "theme.h"
#include "widgets.h"
#include "studio_widgets.h"

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
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Apri un audio"),
                QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                QString::fromStdString(sanity::audio_read_filter()));
            if (path.isEmpty()) return;
            QString error;
            if (!AppState::get().open_audio_file(path, &error))
                QMessageBox::warning(this, QStringLiteral("Apri"),
                    QStringLiteral("Impossibile leggere il file:\n%1").arg(error));
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

    music_card_ = new Card(QStringLiteral("Sottofondo musicale"),
        QStringLiteral("Musica sotto la voce: abbassata mentre parli, in primo piano nelle pause, con dissolvenze."));
    music_card_->setObjectName("MusicBedCard");
    music_pick_ = new QPushButton(QStringLiteral("Scegli musica..."));
    connect(music_pick_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Scegli musica"), {},
            QString::fromStdString(sanity::audio_read_filter()));
        if (path.isEmpty()) return;
        QString error;
        if (!AppState::get().set_music(path, &error)) music_info_->setText(error);
        else music_enabled_->setChecked(true);
    });
    music_card_->add_widget(music_pick_);
    music_info_ = new QLabel();
    music_info_->setObjectName("Mono");
    music_info_->setWordWrap(true);
    music_card_->add_widget(music_info_);
    music_enabled_ = new QCheckBox(QStringLiteral("Attivo"));
    connect(music_enabled_, &QCheckBox::toggled, this, [this] { sync_params(); });
    music_card_->add_widget(music_enabled_);
    auto slider = [this](QSlider*& control, QLabel*& label, int low, int high,
                         const QString& title, const QString& hint) {
        control = new QSlider(Qt::Horizontal);
        control->setRange(low, high);
        label = new QLabel();
        label->setObjectName("Mono");
        auto* host = new QWidget();
        auto* row = new QHBoxLayout(host);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(control, 1);
        row->addWidget(label);
        connect(control, &QSlider::valueChanged, this, [this, label](int value) {
            label->setText(QStringLiteral("%1 dB").arg(value));
            sync_params();
        });
        music_card_->add_widget(make_row(title, host, hint));
    };
    slider(music_level_, music_level_label_, -30, -6, QStringLiteral("Livello nelle pause"),
           QStringLiteral("Livello della musica nelle pause, rispetto alla voce."));
    slider(music_duck_, music_duck_label_, -24, 0, QStringLiteral("Ducking"),
           QStringLiteral("Quanto scende mentre parli."));
    auto seconds = [this](const QString& title, double maximum) {
        auto* control = new QDoubleSpinBox();
        control->setRange(0, maximum);
        control->setDecimals(1);
        control->setSingleStep(0.5);
        control->setSuffix(QStringLiteral(" s"));
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] { sync_params(); });
        music_card_->add_widget(make_row(title, control));
        return control;
    };
    music_fade_in_ = seconds(QStringLiteral("Dissolvenza iniziale"), 10);
    music_fade_out_ = seconds(QStringLiteral("Dissolvenza finale"), 10);
    music_offset_ = seconds(QStringLiteral("Inizio sottofondo"), 60);
    music_tail_ = seconds(QStringLiteral("Coda dopo la voce"), 30);
    music_loop_ = new QCheckBox(QStringLiteral("Ripeti se piu corta dell'episodio"));
    connect(music_loop_, &QCheckBox::toggled, this, [this] { sync_params(); });
    music_card_->add_widget(music_loop_);
    music_preview_ = new QPushButton(QStringLiteral("Anteprima 15 s"));
    music_preview_stop_ = new QPushButton(QStringLiteral("Stop"));
    connect(music_preview_, &QPushButton::clicked, this, &MasterPage::preview_music);
    connect(music_preview_stop_, &QPushButton::clicked, this, &MasterPage::stop_playback);
    auto* preview = new QWidget();
    auto* preview_row = new QHBoxLayout(preview);
    preview_row->setContentsMargins(0, 0, 0, 0);
    preview_row->addWidget(music_preview_);
    preview_row->addWidget(music_preview_stop_);
    music_card_->add_widget(preview);
    col->insertWidget(at + 4, music_card_);

    // -------------------------------------------------------------- result
    result_card_ = new Card(QStringLiteral("Risultato"),
                            QStringLiteral("Confronto prima e dopo, con lo stato di conformita."));
    result_card_->setObjectName("MasterResultCard");
    {
        auto* meters = new QWidget();
        auto* row = new QHBoxLayout(meters);
        row->setContentsMargins(0, 0, 0, 0);
        lufs_before_ = new VuMeter(QStringLiteral("Loudness prima"));
        lufs_after_ = new VuMeter(QStringLiteral("Loudness dopo"));
        for (auto* meter : {lufs_before_, lufs_after_}) {
            meter->set_scale(-30, -10, QStringLiteral("LUFS"));
            meter->set_reference(0.0);
            meter->set_ticks({-30, -27, -24, -21, -19, -16, -14, -12, -10});
            row->addWidget(meter);
        }
        result_card_->add_widget(meters);
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
        cancel_ = new QPushButton(QStringLiteral("Annulla"));
        cancel_->setObjectName("Ghost");
        cancel_->hide();
        connect(cancel_, &QPushButton::clicked, this, [] { AppState::get().cancel_mastering(); });
        auto* progress_row = new QWidget();
        auto* progress_hl = new QHBoxLayout(progress_row);
        progress_hl->setContentsMargins(0, 0, 0, 0);
        progress_hl->addWidget(progress_, 1);
        progress_hl->addWidget(cancel_);
        elapsed_ = new QLabel(QStringLiteral(""));
        elapsed_->setObjectName("Hint");
        process_ = new QPushButton(QStringLiteral("Elabora con Sanity Master"));
        process_->setObjectName("Primary");
        connect(process_, &QPushButton::clicked, this, &MasterPage::process);
        result_card_->add_widget(progress_row);
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
    col->insertWidget(at + 5, result_card_);

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
    col->insertWidget(at + 6, stages_card_);

    connect(&AppState::get(), &AppState::mastering_started, this, [this] {
        processing_ = true;
        since_start_.start();
        progress_->show();
        cancel_->show();
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
        cancel_->hide();
        if (elapsed_timer_) elapsed_timer_->stop();
        process_->setEnabled(true);
        if (!ok) {
            if (err == QStringLiteral("Annullato")) {
                result_card_->set_state(State::Idle, QStringLiteral("Annullato"));
                verdict_->setText(QStringLiteral("Elaborazione annullata."));
                return;
            }
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
    connect(&AppState::get(), &AppState::music_changed, this, &MasterPage::refresh_music);
    connect(&AppState::get(), &AppState::params_changed, this, [this] {
        ++candidates_generation_;
        load_params(AppState::get().session().params);
    });
    load_params(AppState::get().default_params());
    refresh_music();
}

MasterPage::~MasterPage() {
    if (music_thread_) music_thread_->wait();
    if (candidates_thread_) candidates_thread_->wait();
}

void MasterPage::load_params(const sanity::Params& p) {
    loading_params_ = true;
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
    music_enabled_->setChecked(p.music_bed.enabled);
    music_level_->setValue(static_cast<int>(std::lround(p.music_bed.bed_level_db)));
    music_duck_->setValue(static_cast<int>(std::lround(p.music_bed.duck_db)));
    music_level_label_->setText(QStringLiteral("%1 dB").arg(music_level_->value()));
    music_duck_label_->setText(QStringLiteral("%1 dB").arg(music_duck_->value()));
    music_fade_in_->setValue(p.music_bed.fade_in_s);
    music_fade_out_->setValue(p.music_bed.fade_out_s);
    music_offset_->setValue(p.music_bed.offset_s);
    music_tail_->setValue(p.music_bed.tail_s);
    music_loop_->setChecked(p.music_bed.loop);
    loading_params_ = false;
    refresh_music();
}

void MasterPage::sync_params() {
    if (loading_params_) return;
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
    p.music_bed.enabled = music_enabled_->isChecked();
    p.music_bed.bed_level_db = music_level_->value();
    p.music_bed.duck_db = music_duck_->value();
    p.music_bed.fade_in_s = music_fade_in_->value();
    p.music_bed.fade_out_s = music_fade_out_->value();
    p.music_bed.offset_s = music_offset_->value();
    p.music_bed.tail_s = music_tail_->value();
    p.music_bed.loop = music_loop_->isChecked();
    // Keep the standards page targets.
    AppState::get().set_params(p);
}

void MasterPage::process() {
    if (processing_) return;
    sync_params();
    AppState::get().start_mastering(AppState::get().session().params);
}

void MasterPage::refresh_from_state() {
    ++candidates_generation_;
    candidates_->setRowCount(0);
    lufs_before_->reset();
    lufs_after_->reset();
    refresh_music();
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

    const double target = r.params.target_lufs == 0.0 ? (r.after.channels > 1 ? -16.0 : -19.0)
                                                     : r.params.target_lufs;
    lufs_before_->set_value_dbfs(r.before.integrated_lufs);
    lufs_after_->set_value_dbfs(r.after.integrated_lufs);
    lufs_before_->set_target(target);
    lufs_after_->set_target(target);
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
    for (const auto& stage : r.stages) {
        if (stage.name != "music_bed" || !stage.enabled) continue;
        auto metric = [&stage](const char* key) {
            for (const auto& value : stage.metrics) if (value.first == key) return value.second;
            return 0.0;
        };
        verdict_->setText(verdict_->text() + QStringLiteral("\nSottofondo: livello %1 dB nelle pause, ducking su %2 % del tempo, coda %3 s.")
            .arg(metric("bed_level_db"), 0, 'f', 1).arg(metric("duck_ratio") * 100, 0, 'f', 0)
            .arg(metric("tail_s"), 0, 'f', 1));
    }
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
    if (candidates_thread_) return;
    const Session& s = AppState::get().session();
    if (!s.has_audio) {
        candidates_->setRowCount(0);
        return;
    }
    const auto mode = static_cast<sanity::FillerMode>(filler_->currentData().toInt());
    const int generation = ++candidates_generation_;
    candidates_btn_->setEnabled(false);
    filler_card_->set_state(State::Busy, QStringLiteral("Analisi"));
    auto* worker = QThread::create([this, audio = s.audio, mode, generation] {
        std::vector<FillerCandidate> cands;
        QString error;
        try {
            if (mode != sanity::FillerMode::Off)
                sanity::detect_fillers(sanity::to_mono(audio), audio.sample_rate, mode, &cands);
            std::sort(cands.begin(), cands.end(),
                [](const FillerCandidate& a, const FillerCandidate& b) { return a.score > b.score; });
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        } catch (...) {
            error = QStringLiteral("Analisi non riuscita");
        }
        QMetaObject::invokeMethod(this, [this, cands = std::move(cands), fs = audio.sample_rate, generation, error] {
            if (generation != candidates_generation_) {
                filler_card_->set_state(State::Idle, QStringLiteral("Da aggiornare"));
                return;
            }
            if (!error.isEmpty()) {
                filler_card_->set_state(State::Error, QStringLiteral("Errore"));
                AppState::get().status_message(error);
                return;
            }
            candidates_->setRowCount(static_cast<int>(cands.size()));
            for (int i = 0; i < static_cast<int>(cands.size()); i++) {
                const FillerCandidate& c = cands[i];
                candidates_->setItem(i, 0, new QTableWidgetItem(fmt_seconds(c.start / static_cast<double>(fs))));
                candidates_->setItem(i, 1, new QTableWidgetItem(fmt_ms(c.duration_ms)));
                candidates_->setItem(i, 2, new QTableWidgetItem(QString::number(c.score, 'f', 3)));
                candidates_->setItem(i, 3, new QTableWidgetItem(c.removed ? QStringLiteral("Rimosso")
                                                                       : QStringLiteral("Mantenuto")));
            }
            candidates_->resizeColumnsToContents();
            filler_card_->set_state(State::Ready, QStringLiteral("%1 candidati").arg(cands.size()));
        }, Qt::QueuedConnection);
    });
    candidates_thread_ = worker;
    worker->setParent(this);
    connect(worker, &QThread::finished, this, [this] {
        candidates_thread_ = nullptr;
        candidates_btn_->setEnabled(true);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MasterPage::play(const QString& which) {
    const Session& s = AppState::get().session();
    const sanity::AudioBuffer& b = which == "after" ? s.mastered : s.audio;
    if (b.data.empty()) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = dir + QStringLiteral("/sanity-preview-%1.wav").arg(which);
    std::string err;
    if (!sanity::wav_write(path.toStdString(), b, true, &err)) return;
    play_file(path);
}

void MasterPage::play_file(const QString& path) {
#if SANITY_HAVE_QTMM
    if (!player_) {
        player_ = new QMediaPlayer(this);
        auto* audio = new QAudioOutput(this);
        player_->setAudioOutput(audio);
        audio->setVolume(0.8f);
    }
    player_->setSource(QUrl::fromLocalFile(path));
    player_->play();
    AppState::get().status_message(QStringLiteral("Riproduzione: %1").arg(QFileInfo(path).fileName()));
#else
    (void)path;
    AppState::get().status_message(QStringLiteral("Riproduzione non disponibile in questa build"));
#endif
}

void MasterPage::stop_playback() {
    ++preview_generation_;
#if SANITY_HAVE_QTMM
    if (player_) player_->stop();
#endif
    refresh_music();
}

void MasterPage::refresh_music() {
    const Session& s = AppState::get().session();
    music_info_->setText(s.has_music ? s.music_path + "\n" + describe_audio(*s.music)
                                    : QStringLiteral("Nessuna musica caricata."));
    const bool active = s.has_music && s.params.music_bed.enabled;
    music_card_->set_state(active ? State::Ready : State::Idle,
                          active ? QStringLiteral("Attivo") : QStringLiteral("Disattivato"));
    music_preview_->setEnabled(!music_thread_ && s.has_music && s.has_audio);
}

void MasterPage::preview_music() {
    const Session& s = AppState::get().session();
    if (!s.has_audio || !s.has_music) {
        AppState::get().status_message(QStringLiteral("Carica un audio e una musica per l'anteprima"));
        return;
    }
    if (music_thread_) return;
    sync_params();
    const sanity::Params p = s.params;
    std::vector<float> mono = sanity::to_mono(s.audio);
    const int fs = s.audio.sample_rate;
    if (fs <= 0 || mono.empty()) return;
    sanity::Segmenter seg(sanity::SegmenterConfig{fs, 10.0, 35.0});
    std::vector<sanity::FrameFeat> feats;
    seg.features(mono, feats);
    std::size_t first = 0;
    for (std::size_t f = 0; f < feats.size(); f++) if (feats[f].speech) { first = f; break; }
    const std::size_t hop = static_cast<std::size_t>(sanity::segmenter_hop(fs));
    const std::size_t start = first > 100 ? (first - 100) * hop : 0;
    const std::size_t len = std::min(mono.size() - start, static_cast<std::size_t>(15) * fs);
    std::vector<float> excerpt(mono.begin() + static_cast<long>(start), mono.begin() + static_cast<long>(start + len));
    std::vector<sanity::FrameFeat> ef(feats.begin() + static_cast<long>(start / hop),
                                      feats.begin() + static_cast<long>(std::min(feats.size(), (start + len) / hop)));
    auto music = s.music;
    const int generation = ++preview_generation_;
    music_preview_->setEnabled(false);
    music_card_->set_state(State::Busy, QStringLiteral("Anteprima"));
    auto* worker = QThread::create([this, excerpt, ef, music, p, fs, generation]() mutable {
        QString path;
        std::string err;
        bool ok = false;
        try {
            sanity::MusicBedOptions o = p.music_bed;
            o.enabled = true;
            o.tail_s = std::min(o.tail_s, 3.0);
            sanity::mix_music_bed(excerpt, fs, ef, *music, o);
            sanity::normalize_loudness(excerpt, fs, -19.0, -1.0);
            path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                .filePath(QStringLiteral("sanity-preview-bed.wav"));
            ok = sanity::wav_write(path.toStdString(), sanity::from_mono(fs, excerpt), true, &err);
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "Preview failed";
        }
        QMetaObject::invokeMethod(this, [this, ok, path, err, generation] {
            if (generation != preview_generation_) return;
            music_card_->set_state(ok ? State::Ready : State::Error,
                ok ? QStringLiteral("In riproduzione") : QStringLiteral("Errore"));
            if (ok) play_file(path);
            else AppState::get().status_message(QString::fromStdString(err));
        }, Qt::QueuedConnection);
    });
    music_thread_ = worker;
    worker->setParent(this);
    connect(worker, &QThread::finished, this, [this] {
        music_thread_ = nullptr;
        music_preview_->setEnabled(AppState::get().session().has_music && AppState::get().session().has_audio);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

}  // namespace gui
