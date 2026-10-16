#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <iterator>

#include "pages/pages.h"
#include "theme.h"
#include "widgets.h"

using namespace sanity;

namespace gui {

namespace {

struct Standard {
    const char* name;
    double lufs;
    double tp;
    const char* note;
};

const Standard kStandards[] = {
    {"Apple Podcasts - voce mono", -19.0, -1.0,
     "Apple Authoring Practices: -19 LUFS mono, true peak -1 dBTP."},
    {"Apple Podcasts - stereo", -16.0, -1.0,
     "Apple Authoring Practices: -16 LUFS stereo, true peak -1 dBTP."},
    {"Spotify (target di riproduzione)", -14.0, -1.0,
     "Spotify normalizza a -14 LUFS: pubblicare piu basso non alza il volume."},
    {"EBU R128 / broadcast", -23.0, -1.0, "EBU R128 richiede -23 LUFS e -1 dBTP."},
    {"AES streaming (-16, tetto -1)", -16.0, -1.0, "Target AES per lo streaming."},
    {"Personalizzato", 0.0, -1.0, "Imposta i valori a mano."},
};

constexpr int kStandardCount = static_cast<int>(sizeof(kStandards) / sizeof(kStandards[0]));

}  // namespace

StandardsPage::StandardsPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* page = new Page("Standard & Loudness",
                          "Il master deve rispettare lo standard del tuo distributore: loudness "
                          "integrato e true peak, misurati con BS.1770-4.",
                          "In attesa", this);
    outer->addWidget(page);
    auto* col = page->column();
    const int at = col->count() - 1;

    auto* target_card = new Card(QStringLiteral("Target di pubblicazione"),
                                 QStringLiteral("Scegli lo standard: i valori si compilano da soli "
                                                "e puoi comunque ritoccarli."));
    {
        standard_ = new QComboBox();
        for (const Standard& sd : kStandards) standard_->addItem(QString::fromUtf8(sd.name));
        standard_->setCurrentIndex(0);
        connect(standard_, &QComboBox::currentIndexChanged, this,
                [this] { on_standard_changed(); });
        target_card->add_widget(make_row(QStringLiteral("Standard"), standard_));

        target_ = new QDoubleSpinBox();
        target_->setRange(-40.0, -5.0);
        target_->setSingleStep(0.5);
        target_->setValue(-19.0);
        target_card->add_widget(make_row(QStringLiteral("Loudness integrato"), target_,
                                         QStringLiteral("LUFS: 0 imposta automaticamente -16 "
                                                        "stereo e -19 mono.")));

        true_peak_ = new QDoubleSpinBox();
        true_peak_->setRange(-6.0, 0.0);
        true_peak_->setSingleStep(0.1);
        true_peak_->setValue(-1.0);
        target_card->add_widget(make_row(QStringLiteral("Tetto true peak"), true_peak_,
                                         QStringLiteral("dBTP. Il limite di consegna e -1.0.")));

        max_lra_ = new QDoubleSpinBox();
        max_lra_->setRange(0.0, 30.0);
        max_lra_->setSingleStep(0.5);
        max_lra_->setValue(0.0);
        target_card->add_widget(make_row(QStringLiteral("Loudness range massimo"), max_lra_,
                                         QStringLiteral("LU. 0 = senza vincolo.")));

        max_short_ = new QDoubleSpinBox();
        max_short_->setRange(-40.0, 0.0);
        max_short_->setValue(0.0);
        target_card->add_widget(make_row(QStringLiteral("Short term massimo"), max_short_,
                                         QStringLiteral("LUFS. 0 = senza vincolo.")));

        max_moment_ = new QDoubleSpinBox();
        max_moment_->setRange(-40.0, 0.0);
        max_moment_->setValue(0.0);
        target_card->add_widget(make_row(QStringLiteral("Momentary massimo"), max_moment_,
                                         QStringLiteral("LUFS. 0 = senza vincolo.")));

        auto sync = [this] {
            sanity::Params p = AppState::get().session().params;
            p.target_lufs = (standard_->currentIndex() == static_cast<int>(kStandardCount) - 1)
                                ? target_->value()
                                : kStandards[standard_->currentIndex()].lufs;
            p.target_true_peak_db = true_peak_->value();
            p.max_lra = max_lra_->value();
            p.max_short_term = max_short_->value();
            p.max_momentary = max_moment_->value();
            AppState::get().set_params(p);
            refresh_conformance();
        };
        connect(target_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [sync] { sync(); });
        connect(true_peak_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [sync] { sync(); });
        connect(max_lra_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [sync] { sync(); });
        connect(max_short_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [sync] { sync(); });
        connect(max_moment_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [sync] { sync(); });
    }
    col->insertWidget(at, target_card);

    conform_card_ = new Card(QStringLiteral("Verifica di conformita"),
                             QStringLiteral("Ultimo master elaborato."));
    {
        auto* tiles = new QGridLayout();
        tiles->setSpacing(10);
        t_target_ = new StatTile(QStringLiteral("Target"), QStringLiteral("LUFS"));
        t_achieved_ = new StatTile(QStringLiteral("Ottenuto"), QStringLiteral("LUFS"));
        t_error_ = new StatTile(QStringLiteral("Scostamento"), QStringLiteral("LU"));
        t_tp_ = new StatTile(QStringLiteral("True peak"), QStringLiteral("dBTP"));
        tiles->addWidget(t_target_, 0, 0);
        tiles->addWidget(t_achieved_, 0, 1);
        tiles->addWidget(t_error_, 0, 2);
        tiles->addWidget(t_tp_, 0, 3);
        auto* host = new QWidget();
        host->setLayout(tiles);
        conform_card_->add_widget(host);

        auto* chips = new QHBoxLayout();
        loudness_chip_ = new Chip();
        peak_chip_ = new Chip();
        lra_chip_ = new Chip();
        chips->addWidget(loudness_chip_);
        chips->addWidget(peak_chip_);
        chips->addWidget(lra_chip_);
        chips->addStretch(1);
        auto* chip_host = new QWidget();
        chip_host->setLayout(chips);
        conform_card_->add_widget(chip_host);
    }
    col->insertWidget(at + 1, conform_card_);

    measure_card_ = new Card(QStringLiteral("Misura un file"),
                             QStringLiteral("Analisi BS.1770-4 completa, senza elaborare."));
    {
        measure_btn_ = new QPushButton(QStringLiteral("Scegli un file e misura..."));
        connect(measure_btn_, &QPushButton::clicked, this, &StandardsPage::measure_file);
        measured_ = new QLabel(QStringLiteral("Nessuna misura eseguita."));
        measured_->setObjectName("Mono");
        measured_->setWordWrap(true);
        measure_card_->add_widget(measure_btn_);
        measure_card_->add_widget(measured_);
    }
    col->insertWidget(at + 2, measure_card_);

    auto* ref = new Card(QStringLiteral("Riferimenti"),
                         QStringLiteral("Valori di consegna dei principali standard."));
    {
        auto* table = new QTableWidget(static_cast<int>(kStandardCount), 3);
        table->setHorizontalHeaderLabels(
            {QStringLiteral("Standard"), QStringLiteral("Loudness"), QStringLiteral("Note")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for (int i = 0; i < static_cast<int>(kStandardCount); i++) {
            table->setItem(i, 0, new QTableWidgetItem(QString::fromUtf8(kStandards[i].name)));
            table->setItem(i, 1, new QTableWidgetItem(
                                     QStringLiteral("%1 LUFS / %2 dBTP")
                                         .arg(kStandards[i].lufs, 0, 'f', 1)
                                         .arg(kStandards[i].tp, 0, 'f', 1)));
            table->setItem(i, 2, new QTableWidgetItem(QString::fromUtf8(kStandards[i].note)));
        }
        table->resizeColumnsToContents();
        table->setMinimumHeight(210);
        ref->add_widget(table);
    }
    col->insertWidget(at + 3, ref);

    connect(&AppState::get(), &AppState::mastered_changed, this,
            [this] { refresh_conformance(); });
    connect(&AppState::get(), &AppState::params_changed, this,
            [this] { refresh_conformance(); });
    on_standard_changed();
}

void StandardsPage::on_standard_changed() {
    const int i = standard_->currentIndex();
    const bool custom = i == static_cast<int>(kStandardCount) - 1;
    target_->setEnabled(custom);
    if (!custom) target_->setValue(kStandards[i].lufs);
    true_peak_->setValue(kStandards[i].tp);
    sanity::Params p = AppState::get().session().params;
    p.target_lufs = custom ? target_->value() : kStandards[i].lufs;
    p.target_true_peak_db = true_peak_->value();
    AppState::get().set_params(p);
    refresh_conformance();
}

void StandardsPage::refresh_conformance() {
    const Session& s = AppState::get().session();
    const sanity::Params p = s.params;
    const bool stereo = s.has_audio && s.audio.channels > 1;
    const double target = p.target_lufs != 0.0 ? p.target_lufs : (stereo ? -16.0 : -19.0);
    t_target_->set_value(fmt_lufs(target));
    if (!s.has_report) {
        t_achieved_->set_value(QStringLiteral("\u2014"));
        t_error_->set_value(QStringLiteral("\u2014"));
        t_tp_->set_value(QStringLiteral("\u2014"));
        loudness_chip_->set_state(State::Idle, QStringLiteral("Nessun master"));
        peak_chip_->set_state(State::Idle, QStringLiteral("Nessun master"));
        lra_chip_->set_state(State::Idle, QStringLiteral("Nessun master"));
        conform_card_->set_state(State::Idle, QStringLiteral("In attesa"));
        return;
    }
    const Report& r = s.report;
    const double err = r.after.integrated_lufs - target;
    t_achieved_->set_value(fmt_lufs(r.after.integrated_lufs));
    t_error_->set_value(fmt_db(err));
    t_tp_->set_value(fmt_db(r.after.true_peak_db));
    t_error_->set_state(std::fabs(err) <= 1.0 ? State::Ready : State::Warning);
    t_tp_->set_state(r.after.true_peak_db <= p.target_true_peak_db + 0.05 ? State::Ready
                                                                         : State::Error);
    loudness_chip_->set_state(std::fabs(err) <= 1.0 ? State::Ready : State::Warning,
                              std::fabs(err) <= 1.0 ? QStringLiteral("Loudness OK")
                                                    : QStringLiteral("Fuori tolleranza"));
    peak_chip_->set_state(r.conformant_true_peak ? State::Ready : State::Error,
                          r.conformant_true_peak ? QStringLiteral("True peak OK")
                                                 : QStringLiteral("True peak alto"));
    lra_chip_->set_state(r.conformant_lra ? State::Ready : State::Warning,
                         QStringLiteral("LRA %1 LU").arg(fmt_db(r.after.lra)));
    conform_card_->set_state(r.conformant ? State::Ready : State::Warning,
                             r.conformant ? QStringLiteral("Conforme")
                                          : QStringLiteral("Da rivedere"));
}

sanity::Params StandardsPage::current_params() const { return AppState::get().session().params; }

void StandardsPage::apply_preset_target() { on_standard_changed(); }

void StandardsPage::measure_file() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Scegli un audio"), QStandardPaths::writableLocation(
                                                    QStandardPaths::MusicLocation),
        QString::fromStdString(sanity::audio_read_filter()));
    if (path.isEmpty()) return;
    sanity::AudioBuffer b;
    std::string err;
    if (!sanity::audio_read(path.toStdString(), b, err)) {
        measured_->setText(QStringLiteral("Impossibile leggere: %1")
                               .arg(QString::fromStdString(err)));
        measure_card_->set_state(State::Error, QStringLiteral("Errore"));
        return;
    }
    const sanity::Analysis a = sanity::analyze(sanity::to_mono(b), b.sample_rate);
    measured_->setText(
        QStringLiteral("File: %1\n"
                       "Loudness integrato: %2 LUFS\n"
                       "Loudness range: %3 LU\n"
                       "Max momentary: %4 LUFS   Max short term: %5 LUFS\n"
                       "True peak: %6 dBTP   Sample peak: %7 dBFS\n"
                       "Rumore di fondo: %8 dB (%9 regioni)\n"
                       "Ronio: %10 Hz a %11 dB (%12)\n"
                       "DC offset: %13   Campioni saturati: %14\n"
                       "RT60 stimato: %15 s\n"
                       "Rapporto parlato/musica/silenzio: %16 / %17 / %18\n"
                       "Sibilanti: %19 dB   Livello attivo: %20 dB")
            .arg(QFileInfo(path).fileName())
            .arg(fmt_lufs(a.integrated_lufs))
            .arg(fmt_db(a.lra))
            .arg(fmt_lufs(a.max_momentary))
            .arg(fmt_lufs(a.max_short_term))
            .arg(fmt_db(a.true_peak_db))
            .arg(fmt_db(a.sample_peak_db))
            .arg(fmt_db(a.noise_floor_db))
            .arg(a.noise_regions)
            .arg(fmt_db(a.hum_freq, 0))
            .arg(fmt_db(a.hum_db))
            .arg(a.hum_present ? QStringLiteral("presente") : QStringLiteral("assente"))
            .arg(a.dc_offset, 0, 'f', 5)
            .arg(static_cast<qlonglong>(a.clipped_samples))
            .arg(a.rt60, 0, 'f', 2)
            .arg(a.speech_ratio, 0, 'f', 2)
            .arg(a.music_ratio, 0, 'f', 2)
            .arg(a.silence_ratio, 0, 'f', 2)
            .arg(fmt_db(a.sibilance_db))
            .arg(fmt_db(a.speech_activity_db)));
    measure_card_->set_state(State::Ready, QStringLiteral("Misurato"));
}

}  // namespace gui
