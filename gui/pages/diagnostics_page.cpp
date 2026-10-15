#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <thread>

#include "pages/pages.h"
#include "sanity/corpus.hpp"
#include "sanity/metrics.hpp"
#include "theme.h"
#include "widgets.h"

using namespace sanity;

namespace gui {

namespace {

sanity::Degradations degradations_for(const QString& profile) {
    if (profile == QLatin1String("studio")) return sanity::degradation_studio();
    if (profile == QLatin1String("rough")) return sanity::degradation_rough();
    return sanity::degradation_home();
}

}  // namespace

DiagnosticsPage::DiagnosticsPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* page = new Page("Diagnostica & Test",
                          "Lo stesso harness end-to-end della suite di test, eseguito "
                          "nell'applicazione: episodi sintetici con ground truth, metriche "
                          "oggettive, report JSON.",
                          "Pronto", this);
    outer->addWidget(page);
    auto* col = page->column();
    const int at = col->count() - 1;

    harness_card_ = new Card(QStringLiteral("Harness end-to-end"),
                             QStringLiteral("Genera episodi noti (studio, casa, strada), li "
                                            "elabora e misura precisione/recall dei filler, "
                                            "loudness, true peak, SNR e SI-SDR."));
    {
        auto* tools = new QWidget();
        auto* hl = new QHBoxLayout(tools);
        hl->setContentsMargins(0, 0, 0, 0);
        seconds_ = new QComboBox();
        seconds_->addItem(QStringLiteral("12 secondi"), 12.0);
        seconds_->addItem(QStringLiteral("20 secondi"), 20.0);
        seconds_->addItem(QStringLiteral("30 secondi"), 30.0);
        seconds_->setCurrentIndex(1);
        run_ = new QPushButton(QStringLiteral("Esegui harness"));
        run_->setObjectName("Primary");
        connect(run_, &QPushButton::clicked, this, &DiagnosticsPage::run_harness);
        analyse_ = new QPushButton(QStringLiteral("Misura il master corrente"));
        connect(analyse_, &QPushButton::clicked, this, &DiagnosticsPage::run_analysis);
        hl->addWidget(new QLabel(QStringLiteral("Durata")));
        hl->addWidget(seconds_);
        hl->addWidget(run_);
        hl->addWidget(analyse_);
        hl->addStretch(1);
        harness_card_->add_widget(tools);

        status_ = new QLabel(QStringLiteral("Non eseguito in questa sessione."));
        status_->setObjectName("Hint");
        status_->setWordWrap(true);
        harness_card_->add_widget(status_);

        table_ = new QTableWidget(0, 10);
        table_->setHorizontalHeaderLabels(
            {QStringLiteral("Profilo"), QStringLiteral("LUFS"), QStringLiteral("dBTP"),
             QStringLiteral("Precisione"), QStringLiteral("Recall"), QStringLiteral("SNR +dB"),
             QStringLiteral("SI-SDR +dB"), QStringLiteral("RTF"),
             QStringLiteral("Conforme"), QStringLiteral("Deterministico")});
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->verticalHeader()->setVisible(false);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setMinimumHeight(200);
        harness_card_->add_widget(table_);
    }
    col->insertWidget(at, harness_card_);

    report_card_ = new Card(QStringLiteral("Report di qualita (JSON)"),
                            QStringLiteral("Il contratto con l'automazione: ogni stadio con "
                                           "tempo, RTF e metriche."));
    {
        report_ = new QPlainTextEdit();
        report_->setReadOnly(true);
        report_->setPlaceholderText(QStringLiteral("Esegui un mastering o l'harness."));
        report_->setMinimumHeight(260);
        report_card_->add_widget(report_);
        auto* host = new QWidget();
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        auto* last = new QPushButton(QStringLiteral("Report dell'ultimo master"));
        connect(last, &QPushButton::clicked, this, &DiagnosticsPage::show_last_report);
        copy_ = new QPushButton(QStringLiteral("Copia"));
        connect(copy_, &QPushButton::clicked, this, &DiagnosticsPage::copy_report);
        save_ = new QPushButton(QStringLiteral("Salva..."));
        connect(save_, &QPushButton::clicked, this, &DiagnosticsPage::save_report);
        hl->addWidget(last);
        hl->addWidget(copy_);
        hl->addWidget(save_);
        hl->addStretch(1);
        report_card_->add_widget(host);
    }
    col->insertWidget(at + 1, report_card_);

    contract_card_ = new Card(QStringLiteral("Contratto di osservabilita"),
                              QStringLiteral("Cosa la suite verifica a ogni esecuzione."));
    {
        auto* t = new QLabel(QStringLiteral(
            "1. Loudness entro +/-1 LU dal target (-19 mono, -16 stereo) su tutti i profili.\n"
            "2. True peak <= -1.0 dBTP misurato sul file scritto.\n"
            "3. Filler Killer: recall >= 0.75 e precisione >= 0.75; le parole restano.\n"
            "4. Nessun falso positivo su parlato pulito senza esitazioni.\n"
            "5. Il denoiser non peggiora l'SI-SDR e alza il rapporto segnale/rumore.\n"
            "6. Stesso input e stessi parametri danno byte identici (deterministico).\n"
            "7. Ogni stadio abilitato compare nel report con tempo, RTF e metriche."));
        t->setWordWrap(true);
        t->setObjectName("Hint");
        contract_card_->add_widget(t);
    }
    col->insertWidget(at + 2, contract_card_);

    system_card_ = new Card(QStringLiteral("Sistema"));
    {
        const int threads = QThread::idealThreadCount();
        auto* t = new QLabel(QStringLiteral(
            "Qt %1\n"
            "Thread: %2\n"
            "Versione podcast-sanity: %3\n"
            "Pipeline: DSP proprietario (BS.1770-4, RBJ biquad, radix-2 FFT, WOLA STFT, "
            "resampler polyphase) - nessuna libreria DSP esterna.")
                                 .arg(QString::fromLatin1(qVersion()))
                                 .arg(threads)
                                 .arg(QStringLiteral(SANITY_VERSION)));
        t->setWordWrap(true);
        t->setObjectName("Mono");
        system_card_->add_widget(t);
    }
    col->insertWidget(at + 3, system_card_);

    connect(&AppState::get(), &AppState::mastered_changed, this, [this] {
        show_last_report();
    });
}

ScenarioRow DiagnosticsPage::run_scenario(const QString& profile, double seconds,
                                          QString* json) {
    QElapsedTimer timer;
    timer.start();
    const int rate = 48000;
    sanity::CorpusItem item =
        sanity::make_corpus_talk(rate, seconds, degradations_for(profile), 7,
                                 profile.toStdString(), false);
    sanity::AudioBuffer in;
    in.sample_rate = rate;
    in.channels = 1;
    in.data = item.degraded;

    sanity::Params p;
    p.preset = sanity::Preset::Talk;
    p.power_clean = true;
    p.filler = sanity::FillerMode::Cautious;

    sanity::AudioBuffer out;
    const sanity::Report rep = sanity::master(in, p, out);

    ScenarioRow row;
    row.profile = profile;
    row.lufs = rep.after.integrated_lufs;
    row.tp = rep.after.true_peak_db;
    row.conformant = rep.conformant;
    row.rtf = rep.rtf;
    row.fillers_true = static_cast<int>(item.fillers.size());

    // Precision / recall against ground truth, half overlap counts as a hit.
    int tp_count = 0;
    for (const sanity::Span& t : item.fillers) {
        double best = 0.0;
        for (const sanity::FillerHit& h : rep.fillers) {
            const std::size_t a = std::max(t.start, h.start);
            const std::size_t b = std::min(t.end, h.end);
            if (b > a) best = std::max(best, static_cast<double>(b - a));
        }
        const double len = static_cast<double>(t.end - t.start);
        if (len > 0 && best >= 0.5 * len) tp_count++;
    }
    row.fillers_found = static_cast<int>(rep.fillers.size());
    row.precision = row.fillers_found > 0
                        ? static_cast<double>(tp_count) / row.fillers_found
                        : 1.0;
    row.recall = row.fillers_true > 0
                     ? static_cast<double>(tp_count) / row.fillers_true
                     : 1.0;
    row.snr_gain = (rep.after.speech_activity_db - rep.after.noise_floor_db) -
                   (rep.before.speech_activity_db - rep.before.noise_floor_db);

    // Determinism: a second identical run must give identical samples.
    sanity::AudioBuffer out2;
    const sanity::Report rep2 = sanity::master(in, p, out2);
    row.deterministic = out2.data.size() == out.data.size() &&
                        rep2.params_hash == rep.params_hash;
    for (std::size_t i = 0; row.deterministic && i < out.data.size(); i++)
        if (out.data[i] != out2.data[i]) row.deterministic = false;

    // Cleaning only, aligned against the clean reference.
    {
        sanity::Params cp = p;
        cp.filler = sanity::FillerMode::Off;
        cp.normalize = false;
        cp.adaptive_leveler = false;
        cp.compress = false;
        cp.adaptive_highpass = false;
        sanity::AudioBuffer cleaned;
        sanity::master(in, cp, cleaned);
        const int max_lag = rate / 20;
        const double before = sanity::aligned_si_sdr_db(item.degraded, item.clean, max_lag);
        const double after = sanity::aligned_si_sdr_db(sanity::to_mono(cleaned), item.clean,
                                                       max_lag);
        row.si_sdr = after - before;
    }

    *json = QString::fromStdString(rep.to_json());
    return row;
}

void DiagnosticsPage::add_row(const ScenarioRow& row) {
    const int r = table_->rowCount();
    table_->insertRow(r);
    auto set = [&](int c, const QString& text) { table_->setItem(r, c, new QTableWidgetItem(text)); };
    set(0, row.profile);
    set(1, fmt_lufs(row.lufs));
    set(2, fmt_db(row.tp));
    set(3, QString::number(row.precision, 'f', 2));
    set(4, QString::number(row.recall, 'f', 2));
    set(5, fmt_db(row.snr_gain));
    set(6, fmt_db(row.si_sdr));
    set(7, QString::number(row.rtf, 'f', 3));
    set(8, row.conformant ? QStringLiteral("si") : QStringLiteral("no"));
    set(9, row.deterministic ? QStringLiteral("si") : QStringLiteral("no"));
    table_->resizeColumnsToContents();
}

void DiagnosticsPage::set_running(bool on) {
    running_ = on;
    run_->setEnabled(!on);
    harness_card_->set_state(on ? State::Busy : State::Ready,
                            on ? QStringLiteral("In esecuzione") : QStringLiteral("Completato"));
}

void DiagnosticsPage::run_harness() {
    if (running_) return;
    running_ = true;
    table_->setRowCount(0);
    run_->setEnabled(false);
    harness_card_->set_state(State::Busy, QStringLiteral("In esecuzione"));
    status_->setText(QStringLiteral("Generazione ed elaborazione degli episodi sintetici..."));
    const double seconds = seconds_->currentData().toDouble();

    // The harness is heavy: keep the interface responsive on a worker thread and
    // post each finished scenario back to the GUI thread.
    std::thread([this, seconds] {
        int done = 0;
        for (const QString& profile : {QStringLiteral("studio"), QStringLiteral("home"),
                                       QStringLiteral("rough")}) {
            QString json;
            const ScenarioRow row = run_scenario(profile, seconds, &json);
            done++;
            const int n = done;
            QMetaObject::invokeMethod(
                this,
                [this, row, json, n] {
                    add_row(row);
                    last_json_ = json;
                    report_->setPlainText(json);
                    report_card_->set_state(State::Ready, QStringLiteral("Aggiornato"));
                    status_->setText(QStringLiteral("Completati %1 di 3 profili (ultimo: %2, "
                                                    "%3 LUFS, %4 dBTP).")
                                         .arg(n)
                                         .arg(row.profile)
                                         .arg(fmt_lufs(row.lufs))
                                         .arg(fmt_db(row.tp)));
                },
                Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(
            this,
            [this] {
                set_running(false);
                status_->setText(QStringLiteral("Harness completato: 3 profili. Il report JSON "
                                                "dell'ultimo profilo e qui sotto; usa "
                                                "\"Misura il master corrente\" per il tuo "
                                                "master."));
            },
            Qt::QueuedConnection);
    }).detach();
}

void DiagnosticsPage::run_analysis() {
    const Session& s = AppState::get().session();
    if (!s.has_report) {
        status_->setText(QStringLiteral("Nessun master disponibile: elabora prima un audio."));
        return;
    }
    last_json_ = QString::fromStdString(s.report.to_json());
    report_->setPlainText(last_json_);
    report_card_->set_state(State::Ready, QStringLiteral("Aggiornato"));
    status_->setText(QStringLiteral("Report dell'ultimo master caricato."));
}

void DiagnosticsPage::show_last_report() {
    const Session& s = AppState::get().session();
    if (!s.has_report) return;
    last_json_ = QString::fromStdString(s.report.to_json());
    report_->setPlainText(last_json_);
    report_card_->set_state(State::Ready, QStringLiteral("Aggiornato"));
}

void DiagnosticsPage::copy_report() {
    QApplication::clipboard()->setText(report_->toPlainText());
    status_->setText(QStringLiteral("Report copiato negli appunti."));
}

void DiagnosticsPage::save_report() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salva report"),
        QDir(AppState::get().library_dir()).filePath(QStringLiteral("report.json")),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(report_->toPlainText().toUtf8());
        status_->setText(QStringLiteral("Report salvato in %1").arg(path));
    }
}

}  // namespace gui
