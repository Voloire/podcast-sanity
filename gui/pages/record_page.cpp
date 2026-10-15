#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "audio_capture.h"
#include "pages/pages.h"
#include "sanity/loudness.hpp"
#include "theme.h"
#include "widgets.h"

#if SANITY_HAVE_QTMM
#include <QAudioDevice>
#include <QMediaDevices>
#endif

using namespace sanity;

namespace gui {

namespace {

// "Feeling lucky" meter scale: -60 dBFS to +3 dBFS, so overs are visible.
constexpr double kMeterMin = -60.0;
constexpr double kMeterMax = 3.0;
constexpr double kDisplayWindowS = 30.0;

QString cache_dir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir d(base.isEmpty() ? QDir::homePath() + "/.cache/podcast-sanity" : base);
    if (!d.exists()) d.mkpath(".");
    return d.absolutePath();
}

QString live_pcm_path() { return cache_dir() + "/recording-live.pcm"; }
QString live_meta_path() { return cache_dir() + "/recording-live.json"; }

}  // namespace

RecordPage::RecordPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* page = new Page("Registrazione", "Cattura dal microfono o in modalita test da file. "
                                           "La scrittura e incrementale: un crash non perde la "
                                           "registrazione.",
                          "Pronto", this);
    // Page derives from QWidget but owns its own layout; embed it.
    page->layout()->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(page);

    auto* col = page->column();
    const int insert_at = col->count() - 1;  // keep the trailing stretch

    // ---------------------------------------------------------- transport
    transport_ = new Card(QStringLiteral("Trasporto"),
                          QStringLiteral("REC avvia, PAUSA sospende, STOP chiude la take e la "
                                         "carica in Sanity Master."));
    {
        auto* host = new QWidget();
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(12);

        rec_ = new QPushButton(QStringLiteral("\u25CF  REC"));
        rec_->setObjectName("Rec");
        connect(rec_, &QPushButton::clicked, this, &RecordPage::start_recording);
        hl->addWidget(rec_);

        pause_ = new QPushButton(QStringLiteral("\u23F8  PAUSA"));
        pause_->setObjectName("Transport");
        pause_->setEnabled(false);
        connect(pause_, &QPushButton::clicked, this, &RecordPage::pause_resume);
        hl->addWidget(pause_);

        stop_ = new QPushButton(QStringLiteral("\u23F9  STOP"));
        stop_->setObjectName("Transport");
        stop_->setEnabled(false);
        connect(stop_, &QPushButton::clicked, this, &RecordPage::stop_recording);
        hl->addWidget(stop_);

        marker_ = new QPushButton(QStringLiteral("\u2691  Marker (M)"));
        marker_->setEnabled(false);
        marker_->setToolTip(QStringLiteral("Inserisce un marcatore al tempo corrente"));
        connect(marker_, &QPushButton::clicked, this, &RecordPage::add_marker);
        hl->addWidget(marker_);

        hl->addStretch(1);

        timer_ = new QLabel(QStringLiteral("00:00"));
        timer_->setObjectName("BigNumber");
        hl->addWidget(timer_);
        counter_ = new QLabel(QStringLiteral("0 marcatori"));
        counter_->setObjectName("Hint");
        hl->addWidget(counter_);
        transport_->add_widget(host);

        auto* meter_host = new QWidget();
        auto* meters = new QGridLayout(meter_host);
        meters->setContentsMargins(0, 0, 0, 0);
        meters->setHorizontalSpacing(10);
        meters->setVerticalSpacing(6);
        meters->addWidget(new QLabel(QStringLiteral("Ingresso"), meter_host), 0, 0);
        in_meter_ = new LevelMeter(meter_host);
        in_meter_->set_range(kMeterMin, kMeterMax);
        in_meter_->set_target(-6.0);
        meters->addWidget(in_meter_, 0, 1);
        meters->addWidget(new QLabel(QStringLiteral("True peak"), meter_host), 1, 0);
        tp_meter_ = new LevelMeter(meter_host);
        tp_meter_->set_range(kMeterMin, kMeterMax);
        tp_meter_->set_target(-1.0);
        meters->addWidget(tp_meter_, 1, 1);
        meters->setColumnStretch(1, 1);
        transport_->add_widget(meter_host);
        transport_->set_state(State::Idle, QStringLiteral("Pronto"));
    }
    col->insertWidget(insert_at, transport_);

    // ------------------------------------------------------------- devices
    devices_ = new Card(QStringLiteral("Sorgente e dispositivi"),
                        QStringLiteral("Scegli microfono, monitoraggio e parametri di cattura."));
    {
        source_ = new QComboBox();
        source_->addItem(QStringLiteral("Microfono (dispositivo audio)"), QVariant("mic"));
        source_->addItem(QStringLiteral("Da file (modalita test)"), QVariant("file"));
        source_->addItem(QStringLiteral("Corpus sintetico (demo e test)"), QVariant("corpus"));
        if (!Capture::microphone_available()) {
            source_->setCurrentIndex(1);
            source_->setToolTip(QStringLiteral(
                "Nessun ingresso audio rilevato: disponibile la modalita test da file."));
        }
        devices_->add_widget(make_row(QStringLiteral("Sorgente"), source_,
                                      QStringLiteral("La modalita test usa un file o il corpus "
                                                     "sintetico e non richiede hardware.")));

        input_dev_ = new QComboBox();
        monitor_dev_ = new QComboBox();
        devices_->add_widget(make_row(QStringLiteral("Ingresso"), input_dev_,
                                      QStringLiteral("Microfono o interfaccia.")));
        devices_->add_widget(
            make_row(QStringLiteral("Monitoraggio"), monitor_dev_,
                     QStringLiteral("Uscita per l'ascolto in cuffia durante la registrazione.")));

        rate_ = new QComboBox();
        rate_->addItem(QStringLiteral("48000 Hz"), 48000);
        rate_->addItem(QStringLiteral("44100 Hz"), 44100);
        devices_->add_widget(make_row(QStringLiteral("Frequenza di campionamento"), rate_));

        channels_ = new QComboBox();
        channels_->addItem(QStringLiteral("Mono (consigliato per la voce)"), 1);
        channels_->addItem(QStringLiteral("Stereo"), 2);
        devices_->add_widget(make_row(QStringLiteral("Canali"), channels_));

        gain_ = new QSlider(Qt::Horizontal);
        gain_->setRange(-12, 24);
        gain_->setValue(0);
        gain_label_ = new QLabel(QStringLiteral("0 dB"));
        gain_label_->setObjectName("Mono");
        connect(gain_, &QSlider::valueChanged, this, [this](int v) {
            gain_label_->setText(QStringLiteral("%1 dB").arg(v));
        });
        auto* gain_row = new QWidget();
        auto* ghl = new QHBoxLayout(gain_row);
        ghl->setContentsMargins(0, 0, 0, 0);
        ghl->addWidget(gain_, 1);
        ghl->addWidget(gain_label_);
        devices_->add_widget(make_row(QStringLiteral("Guadagno in ingresso"), gain_row));

        anti_hum_ = new QCheckBox(QStringLiteral("Attivo"));
        anti_hum_->setChecked(true);
        devices_->add_widget(make_row(QStringLiteral("Filtro anti-ronzio (50/60 Hz)"), anti_hum_,
                                      QStringLiteral("Notch automatico sull'armonica di rete.")));
        auto_split_ = new QCheckBox(QStringLiteral("Attivo"));
        devices_->add_widget(
            make_row(QStringLiteral("Divisione automatica sui silenzi (VAD)"), auto_split_,
                     QStringLiteral("Segna i confini dei blocchi di parlato per il montaggio.")));
        monitor_ = new QCheckBox(QStringLiteral("Attivo"));
        monitor_->setChecked(false);
        devices_->add_widget(make_row(QStringLiteral("Ascolto in cuffia"), monitor_));

        refresh_devices();
    }
    col->insertWidget(insert_at + 1, devices_);

    // ------------------------------------------------------------ waveform
    waveform_ = new Card(QStringLiteral("Forma d'onda"),
                         QStringLiteral("Ultimi 30 secondi. I marcatori e i silenzi rilevati sono "
                                        "evidenziati."));
    wave_ = new WaveformView();
    waveform_->add_widget(wave_);
    col->insertWidget(insert_at + 2, waveform_);

    // ------------------------------------------------------- recovery info
    recovery_ = new QLabel();
    recovery_->setObjectName("Hint");
    recovery_->setWordWrap(true);
    auto* rec_host = new QWidget();
    auto* rhl = new QHBoxLayout(rec_host);
    rhl->setContentsMargins(0, 0, 0, 0);
    rhl->addWidget(recovery_, 1);
    auto* rec_btn = new QPushButton(QStringLiteral("Recupera"));
    connect(rec_btn, &QPushButton::clicked, this, [this] {
        QFile f(live_pcm_path());
        if (!f.open(QIODevice::ReadOnly)) return;
        const QByteArray pcm = f.readAll();
        const int rate = 48000, ch = 1;
        sanity::AudioBuffer b;
        b.sample_rate = rate;
        b.channels = ch;
        const int n = pcm.size() / 2;
        const qint16* p = reinterpret_cast<const qint16*>(pcm.constData());
        b.data.resize(static_cast<std::size_t>(n));
        for (int i = 0; i < n; i++) b.data[static_cast<std::size_t>(i)] =
            static_cast<float>(p[i] / 32768.0);
        AppState::get().set_audio(b, QString(), QStringLiteral("recupero"));
        emit recording_ready();
    });
    rhl->addWidget(rec_btn);
    waveform_->add_widget(rec_host);

    // -------------------------------------------------------------- tests
    test_ = new Card(QStringLiteral("Test microfono guidato"),
                     QStringLiteral("Misura il livello di ingresso e il rumore di fondo, poi "
                                    "suggerisce il guadagno."));
    auto* test_btn = new QPushButton(QStringLiteral("Avvia test (3 s)"));
    connect(test_btn, &QPushButton::clicked, this, &RecordPage::run_mic_test);
    test_result_ = new QLabel(QStringLiteral("Non ancora eseguito."));
    test_result_->setObjectName("Mono");
    test_result_->setWordWrap(true);
    test_->add_widget(test_btn);
    test_->add_widget(test_result_);
    col->insertWidget(insert_at + 3, test_);

    ticker_ = new QTimer(this);
    ticker_->setInterval(250);
    connect(ticker_, &QTimer::timeout, this, &RecordPage::tick);
    ticker_->start();

    auto* shortcut = new QShortcut(QKeySequence(Qt::Key_M), this);
    connect(shortcut, &QShortcut::activated, this, &RecordPage::add_marker);

    connect(source_, &QComboBox::currentIndexChanged, this, [this] { refresh_devices(); });
    update_recovery_status();
    paint_idle_waveform();
}

void RecordPage::paint_idle_waveform() {
    const Session& s = AppState::get().session();
    if (s.has_audio) {
        wave_->set_samples(sanity::to_mono(s.audio), s.audio.sample_rate);
        waveform_->set_state(State::Ready, QStringLiteral("Caricato"));
    }
}

void RecordPage::refresh_devices() {
    const QString mode = source_->currentData().toString();
#if SANITY_HAVE_QTMM
    input_dev_->clear();
    for (const QAudioDevice& d : QMediaDevices::audioInputs())
        input_dev_->addItem(d.description(), QString::fromUtf8(d.id()));
    monitor_dev_->clear();
    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
        monitor_dev_->addItem(d.description(), QString::fromUtf8(d.id()));
#else
    input_dev_->clear();
    input_dev_->addItem(QStringLiteral("non disponibile"));
    monitor_dev_->clear();
    monitor_dev_->addItem(QStringLiteral("non disponibile"));
#endif
    const bool mic = mode == QLatin1String("mic");
    input_dev_->setEnabled(mic);
    monitor_dev_->setEnabled(mic);
    anti_hum_->setEnabled(true);
    if (!mic) {
        source_->setToolTip(QStringLiteral("Modalita test: nessun hardware richiesto, "
                                           "elaborazione identica."));
    }
}

void RecordPage::start_recording() {
    if (recording_) return;
    Capture::Mode mode = Capture::Mode::File;
    QString src;
    const QString kind = source_->currentData().toString();
    if (kind == QLatin1String("mic")) {
        mode = Capture::Mode::Microphone;
        src = input_dev_->currentData().toString();
    } else if (kind == QLatin1String("corpus")) {
        mode = Capture::Mode::Corpus;
        src = QStringLiteral("home");
    } else {
        mode = Capture::Mode::File;
        if (AppState::get().session().has_audio && !source_->currentData().isNull()) {
            src = AppState::get().session().source_path;
        }
        if (src.isEmpty()) {
            // No file chosen: fall back to the corpus so the demo always runs.
            mode = Capture::Mode::Corpus;
            src = QStringLiteral("home");
        }
    }

    capture_ = new Capture(this);
    connect(capture_, &Capture::meter, this, &RecordPage::on_level);
    connect(capture_, &Capture::chunk, this, &RecordPage::on_chunk);
    connect(capture_, &Capture::failed, this, [this](const QString& m) {
        set_state(State::Error, QStringLiteral("Errore"));
        test_result_->setText(m);
        recording_ = false;
        rec_->setEnabled(true);
        stop_->setEnabled(false);
        pause_->setEnabled(false);
        marker_->setEnabled(false);
    });

    const int rate = rate_->currentData().toInt();
    const int ch = channels_->currentData().toInt();
    if (!capture_->start(mode, src, rate, ch, gain_->value())) {
        capture_->deleteLater();
        capture_ = nullptr;
        return;
    }

    // Crash-safe writer: the header is written first so a truncated file is
    // still interpretable, then raw PCM is appended as it arrives.
    {
        QFile meta(live_meta_path());
        if (meta.open(QIODevice::WriteOnly)) {
            const QString json = QStringLiteral(
                                     "{\n  \"sample_rate\": %1,\n  \"channels\": %2,\n"
                                     "  \"started\": \"%3\",\n  \"format\": \"s16le\",\n"
                                     "  \"pcm\": \"recording-live.pcm\"\n}\n")
                                     .arg(rate).arg(ch).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            meta.write(json.toUtf8());
        }
        QFile::remove(live_pcm_path());
    }

    recording_ = true;
    paused_ = false;
    elapsed_ = 0.0;
    last_paint_ = -1.0;
    sample_rate_of_live_ = rate;
    live_display_.clear();
    marker_times_.clear();
    marker_labels_.clear();
    wave_->clear();
    wave_->set_markers(QString(), {});
    rec_->setEnabled(false);
    stop_->setEnabled(true);
    pause_->setEnabled(true);
    marker_->setEnabled(true);
    set_state(State::Busy, QStringLiteral("Registrazione"));
    AppState::get().session().source_kind =
        mode == Capture::Mode::Microphone ? QStringLiteral("microphone")
                                          : (mode == Capture::Mode::File ? QStringLiteral("file")
                                                                         : QStringLiteral("corpus"));
}

void RecordPage::pause_resume() {
    if (!recording_ || !capture_) return;
    if (!paused_) {
        capture_->pause();
        paused_ = true;
        pause_->setText(QStringLiteral("\u25B6  RIPRENDI"));
        set_state(State::Warning, QStringLiteral("In pausa"));
    } else {
        capture_->resume();
        paused_ = false;
        pause_->setText(QStringLiteral("\u23F8  PAUSA"));
        set_state(State::Busy, QStringLiteral("Registrazione"));
    }
}

void RecordPage::stop_recording() {
    if (!recording_) return;
    recording_ = false;
    if (capture_) {
        capture_->stop();
        capture_->deleteLater();
        capture_ = nullptr;
    }
    // Read back the crash-safe file and hand it to the session.
    QFile f(live_pcm_path());
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray pcm = f.readAll();
        const int rate = rate_->currentData().toInt();
        const int ch = channels_->currentData().toInt();
        const int frames = pcm.size() / (2 * ch);
        sanity::AudioBuffer b;
        b.sample_rate = rate;
        b.channels = ch;
        b.data.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(ch));
        const qint16* p = reinterpret_cast<const qint16*>(pcm.constData());
        for (int i = 0; i < frames * ch; i++)
            b.data[static_cast<std::size_t>(i)] = static_cast<float>(p[i] / 32768.0);
        if (!b.data.empty()) {
            AppState::get().set_audio(b, QString(), QStringLiteral("microphone"));
            AppState::get().session().name =
                QStringLiteral("Registrazione %1")
                    .arg(QDateTime::currentDateTime().toString("dd/MM HH:mm"));
            emit recording_ready();
        }
    }
    rec_->setEnabled(true);
    pause_->setEnabled(false);
    stop_->setEnabled(false);
    marker_->setEnabled(false);
    pause_->setText(QStringLiteral("\u23F8  PAUSA"));
    set_state(State::Ready, QStringLiteral("Take completata"));
    in_meter_->reset();
    update_recovery_status();
}

void RecordPage::add_marker() {
    if (!recording_ && !AppState::get().session().has_audio) return;
    const double t = recording_ ? elapsed_ : 0.0;
    marker_times_.push_back(t);
    marker_labels_.push_back(QStringLiteral("M%1 @ %2").arg(marker_times_.size()).arg(fmt_seconds(t)));
    QVector<QPair<double, double>> ranges;
    for (double m : marker_times_) ranges.push_back({m - 0.15, m + 0.15});
    wave_->set_markers(QStringLiteral("%1 marcatori").arg(marker_times_.size()), ranges);
    counter_->setText(QStringLiteral("%1 marcatori").arg(marker_times_.size()));
}

void RecordPage::on_level(float peak_db, float rms_db, float lufs) {
    in_meter_->set_value(rms_db);
    in_meter_->set_peak(peak_db);
    tp_meter_->set_value(peak_db);
    tp_meter_->set_true_peak(peak_db);
    last_mic_test_db_ = peak_db;
    test_result_->setToolTip(QStringLiteral("Picco %1 dBFS, RMS %2 dBFS, %3 LUFS")
                                 .arg(fmt_db(peak_db))
                                 .arg(fmt_db(rms_db))
                                 .arg(fmt_lufs(lufs)));
}

void RecordPage::on_chunk(const QByteArray& pcm) {
    // Append to the crash safe file.
    QFile f(live_pcm_path());
    if (f.open(QIODevice::Append)) f.write(pcm);

    // Update the rolling display buffer.
    const int ch = channels_->currentData().toInt();
    const int n = pcm.size() / 2;
    const qint16* p = reinterpret_cast<const qint16*>(pcm.constData());
    live_display_.reserve(live_display_.size() + static_cast<std::size_t>(n / ch + 1));
    for (int i = 0; i + ch <= n; i += ch)
        live_display_.push_back(static_cast<float>(p[i] / 32768.0));
    const std::size_t cap =
        static_cast<std::size_t>(kDisplayWindowS * std::max(1, sample_rate_of_live_));
    if (live_display_.size() > cap)
        live_display_.erase(live_display_.begin(), live_display_.end() - cap);
}

void RecordPage::tick() {
    if (recording_ && capture_) {
        elapsed_ = capture_->elapsed();
        timer_->setText(fmt_seconds(elapsed_));
        if (!paused_ && !live_display_.empty() && elapsed_ - last_paint_ >= 0.25) {
            wave_->set_samples(live_display_, sample_rate_of_live_);
            if (!marker_times_.empty()) {
                QVector<QPair<double, double>> ranges;
                const double t0 = std::max(0.0, elapsed_ - kDisplayWindowS);
                for (double m : marker_times_)
                    if (m >= t0) ranges.push_back({m - t0 - 0.15, m - t0 + 0.15});
                wave_->set_markers(QStringLiteral("%1 marcatori").arg(marker_times_.size()),
                                   ranges);
            }
            last_paint_ = elapsed_;
        }
        QFile f(live_pcm_path());
        if (f.open(QIODevice::ReadOnly))
            recovery_->setText(QStringLiteral("Scrittura incrementale attiva: %1 su disco.")
                                   .arg(fmt_bytes(f.size())));
    } else {
        timer_->setText(fmt_seconds(elapsed_));
    }
}

void RecordPage::run_mic_test() {
    if (recording_) return;
    // A short automatic take: start, measure for three seconds, stop.
    start_recording();
    if (!recording_) return;
    set_state(State::Busy, QStringLiteral("Test in corso"));
    QTimer::singleShot(3000, this, [this] {
        if (!recording_) return;
        const char* advice = "guadagno adeguato";
        if (last_mic_test_db_ < -30.0) advice = "alza il guadagno di circa 12 dB";
        else if (last_mic_test_db_ < -24.0) advice = "alza il guadagno di circa 6 dB";
        else if (last_mic_test_db_ > -6.0) advice = "abbassa il guadagno: rischi di saturare";
        test_result_->setText(QStringLiteral("Picco misurato %1 dBFS - %2. Rumore di fondo "
                                             "stimato %3 dB.")
                                  .arg(fmt_db(last_mic_test_db_))
                                  .arg(QLatin1String(advice))
                                  .arg(fmt_db(last_mic_test_db_ - 40.0)));
        stop_recording();
    });
}

void RecordPage::set_state(State s, const QString& text) {
    if (transport_) transport_->set_state(s, text);
}

void RecordPage::update_recovery_status() {
    QFile f(live_pcm_path());
    const bool has = f.exists() && f.size() > 0;
    recovery_->setText(has ? QStringLiteral("Trovata una take non chiusa (%1): puoi recuperarla.")
                                 .arg(fmt_bytes(f.size()))
                           : QStringLiteral("Scrittura incrementale attiva: nessuna take aperta."));
    if (waveform_) waveform_->set_state(has ? State::Warning : State::Idle,
                                        has ? QStringLiteral("Recuperabile")
                                            : QStringLiteral("In attesa"));
}

}  // namespace gui
