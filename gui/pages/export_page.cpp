#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "pages/pages.h"
#include "sanity/encode.hpp"
#include "theme.h"
#include "widgets.h"

using namespace sanity;

namespace gui {

namespace {
struct FormatOption {
    const char* label;
    sanity::ExportFormat format;
    int bitrate;
};
const FormatOption kFormats[] = {
    {"MP3 96 kbps (mono, niente re-encode)", sanity::ExportFormat::Mp3, 96},
    {"MP3 128 kbps", sanity::ExportFormat::Mp3, 128},
    {"MP3 192 kbps (stereo, niente re-encode)", sanity::ExportFormat::Mp3, 192},
    {"MP3 320 kbps", sanity::ExportFormat::Mp3, 320},
    {"WAV 16 bit (master di archivio)", sanity::ExportFormat::Wav16, 0},
    {"WAV 24 bit", sanity::ExportFormat::Wav24, 0},
    {"WAV 32 bit float", sanity::ExportFormat::WavFloat32, 0},
    {"FLAC 24 bit (senza perdita)", sanity::ExportFormat::Flac, 0},
    {"Ogg Vorbis", sanity::ExportFormat::Ogg, 0},
    {"Opus (Ogg)", sanity::ExportFormat::Opus, 0},
};
}  // namespace

ExportPage::ExportPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* page = new Page("Esporta & Pubblica",
                          "Scrivi il file nel formato giusto per il tuo host e prepara la "
                          "pubblicazione su Spotify e sulle altre piattaforme via RSS.",
                          "In attesa", this);
    outer->addWidget(page);
    auto* col = page->column();
    const int at = col->count() - 1;

    format_card_ = new Card(QStringLiteral("Formato di esportazione"),
                            QStringLiteral("Gli host non ricodificano quando il file e gia "
                                           "dentro l'involucro accettato: mono <= 96 kbps, "
                                           "stereo <= 192 kbps."));
    {
        format_ = new QComboBox();
        for (const FormatOption& f : kFormats) {
            if (!sanity::encoder_available(f.format)) continue;
            format_->addItem(QString::fromUtf8(f.label),
                             QVariantList{f.bitrate, static_cast<int>(f.format)});
        }
        connect(format_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        format_card_->add_widget(make_row(QStringLiteral("Formato"), format_));

        channels_ = new QComboBox();
        channels_->addItem(QStringLiteral("Mono (podcast di sola voce)"), 1);
        channels_->addItem(QStringLiteral("Stereo"), 2);
        connect(channels_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        format_card_->add_widget(make_row(QStringLiteral("Canali del master"), channels_,
                                          QStringLiteral("Il mastering processa la somma mono e "
                                                         "duplica sui canali scelti.")));

        rate_ = new QComboBox();
        rate_->addItem(QStringLiteral("48000 Hz"), 48000);
        rate_->addItem(QStringLiteral("44100 Hz"), 44100);
        connect(rate_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        format_card_->add_widget(make_row(QStringLiteral("Frequenza di uscita"), rate_));

        filename_ = new QLineEdit();
        filename_->setPlaceholderText(QStringLiteral("nome-episodio"));
        connect(filename_, &QLineEdit::textChanged, this, [this] { refresh(); });
        format_card_->add_widget(make_row(QStringLiteral("Nome file"), filename_));

        hint_ = new QLabel();
        hint_->setObjectName("Hint");
        hint_->setWordWrap(true);
        format_card_->add_widget(hint_);
        target_path_ = new QLabel();
        target_path_->setObjectName("Mono");
        target_path_->setWordWrap(true);
        format_card_->add_widget(target_path_);
    }
    col->insertWidget(at, format_card_);

    conform_card_ = new Card(QStringLiteral("Controllo prima di esportare"),
                             QStringLiteral("Non si pubblica un file non conforme per "
                                            "distrazione."));
    {
        conform_label_ = new QLabel(QStringLiteral("Nessun master da esportare."));
        conform_label_->setWordWrap(true);
        conform_chip_ = new Chip();
        auto* host = new QWidget();
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(conform_chip_);
        hl->addStretch(1);
        conform_card_->add_widget(host);
        conform_card_->add_widget(conform_label_);
        also_input_ = new QCheckBox(QStringLiteral("Esporta anche la traccia non elaborata"));
        conform_card_->add_widget(also_input_);
        export_ = new QPushButton(QStringLiteral("Elabora ed esporta"));
        export_->setObjectName("Primary");
        connect(export_, &QPushButton::clicked, this, &ExportPage::do_export);
        conform_card_->add_widget(export_);
    }
    col->insertWidget(at + 1, conform_card_);

    publish_card_ = new Card(QStringLiteral("Pubblicazione"),
                             QStringLiteral("Spotify non ha un'API di caricamento: si pubblica "
                                            "con un feed RSS ospitato (Buzzsprout, Spotify for "
                                            "Creators, il tuo hosting)."));
    {
        auto* steps = new QLabel(
            QStringLiteral(
                "1. Esporta l'MP3 mono a 96 kbps (o stereo a 192) con loudness conforme.\n"
                "2. Carica il file sull'hosting del tuo feed RSS (Buzzsprout, Transistor, "
                "Spotify for Creators...).\n"
                "3. L'host pubblica il feed: Spotify, Apple Podcasts e le altre piattaforme lo "
                "leggono da li.\n"
                "4. Se l'host offre la consegna diretto-a-Spotify, collegalo una volta sola: "
                "ogni nuovo episodio arriva in automatico."));
        steps->setWordWrap(true);
        steps->setObjectName("Hint");
        publish_card_->add_widget(steps);

        copy_ = new QPushButton(QStringLiteral("Copia lo snippet RSS"));
        connect(copy_, &QPushButton::clicked, this, &ExportPage::copy_rss);
        spotify_ = new QPushButton(QStringLiteral("Apri Spotify for Creators"));
        connect(spotify_, &QPushButton::clicked, this, &ExportPage::open_spotify);
        auto* host = new QWidget();
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(copy_);
        hl->addWidget(spotify_);
        hl->addStretch(1);
        publish_card_->add_widget(host);

        auto* note = new QLabel(QStringLiteral(
            "Nota tecnica: qui non si carica nulla in rete. Il file resta sul tuo disco finche "
            "non lo carichi tu sull'hosting. Nessun dato lascia il computer."));
        note->setObjectName("Hint");
        note->setWordWrap(true);
        publish_card_->add_widget(note);
    }
    col->insertWidget(at + 2, publish_card_);

    connect(&AppState::get(), &AppState::mastered_changed, this, [this] { refresh(); });
    connect(&AppState::get(), &AppState::audio_changed, this, [this] { refresh(); });
    connect(&AppState::get(), &AppState::mastering_finished, this, [this](bool ok, const QString& err) {
        if (!export_after_) return;
        export_after_ = false;
        export_->setEnabled(true);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("Esporta"), err);
            return;
        }
        do_export();
    });
    refresh();
}

sanity::ExportOptions ExportPage::options() const {
    sanity::ExportOptions o;
    const QVariantList v = format_->currentData().toList();
    if (v.size() == 2) {
        o.mp3_bitrate_kbps = v.at(0).toInt();
        o.format = static_cast<sanity::ExportFormat>(v.at(1).toInt());
    }
    return o;
}

void ExportPage::refresh() {
    const Session& s = AppState::get().session();
    const sanity::ExportOptions o = options();
    const int ch = channels_->currentData().toInt();

    if (s.has_report) {
        const Report& r = s.report;
        conform_label_->setText(
            QStringLiteral("Loudness %1 LUFS, true peak %2 dBTP, filler rimossi %3 s. %4")
                .arg(fmt_lufs(r.after.integrated_lufs))
                .arg(fmt_db(r.after.true_peak_db))
                .arg(fmt_db(r.filler_seconds_removed, 2))
                .arg(r.conformant ? QStringLiteral("Conforme: puoi pubblicare.")
                                  : QStringLiteral("Non conforme: torna su Standard & Loudness.")));
        conform_chip_->set_state(r.conformant ? State::Ready : State::Warning,
                                 r.conformant ? QStringLiteral("Pronto")
                                              : QStringLiteral("Da correggere"));
        conform_card_->set_state(r.conformant ? State::Ready : State::Warning,
                                 r.conformant ? QStringLiteral("Conforme")
                                              : QStringLiteral("Attenzione"));
    } else {
        conform_label_->setText(
            QStringLiteral("Nessun master in memoria. Premi \"Elabora ed esporta\": la pipeline "
                           "gira con le impostazioni correnti e poi scrive il file."));
        conform_chip_->set_state(State::Idle, QStringLiteral("Da elaborare"));
        conform_card_->set_state(State::Idle, QStringLiteral("In attesa"));
    }

    export_->setText(s.has_report ? QStringLiteral("Esporta il master")
                                  : QStringLiteral("Elabora ed esporta"));

    // Encode hint + preview of where the file will land.
    QString hint;
    if (o.format == sanity::ExportFormat::Mp3) {
        const int recommended = sanity::recommended_mp3_bitrate(ch);
        hint = QStringLiteral("Consigliato %1 kbps per %2 canale/i (limite oltre il quale l'host "
                              "ricodifica).")
                   .arg(recommended)
                   .arg(ch);
        if (o.mp3_bitrate_kbps > recommended && o.mp3_bitrate_kbps != 320)
            hint += QStringLiteral(" Con questa scelta l'host potrebbe ricodificare.");
    } else {
        hint = QStringLiteral("Formato senza perdita: utile come archivio, piu pesante da "
                              "caricare.");
    }
    if (!sanity::encoder_available(o.format))
        hint = QStringLiteral("Codificatore non disponibile in questa build.");
    hint_->setText(hint);

    const QString name = filename_->text().trimmed().isEmpty()
                             ? (s.name.isEmpty() ? QStringLiteral("episodio")
                                                 : s.name.toLower().replace(' ', '-'))
                             : filename_->text().trimmed();
    const QString ext = QString::fromStdString(sanity::export_extension(o.format));
    const QString path = AppState::get().library_dir() + "/" + name + ext;
    target_path_->setText(QStringLiteral("File di destinazione: %1").arg(path));
    publish_card_->set_state(s.has_report ? State::Ready : State::Idle,
                            s.has_report ? QStringLiteral("Pronto") : QStringLiteral("In attesa"));
    export_->setEnabled(sanity::encoder_available(o.format) &&
                        (s.has_audio || s.has_report));
}

void ExportPage::do_export() {
    Session& s = AppState::get().session();
    if (!s.has_report) {
        if (!s.has_audio) {
            QMessageBox::information(this, QStringLiteral("Esporta"),
                                     QStringLiteral("Carica o registra prima un audio."));
            return;
        }
        sanity::Params p = s.params;
        p.out_channels = channels_->currentData().toInt();
        p.out_sample_rate = rate_->currentData().toInt();
        s.params = p;
        export_after_ = true;
        export_->setEnabled(false);
        conform_chip_->set_state(State::Busy, QStringLiteral("Elaborazione"));
        AppState::get().start_mastering(p);
        return;
    }

    sanity::ExportOptions o = options();
    QString suggested = QDir(AppState::get().library_dir())
                            .filePath(filename_->text().trimmed().isEmpty()
                                          ? s.name.toLower().replace(' ', '-')
                                          : filename_->text().trimmed());
    suggested += QString::fromStdString(sanity::export_extension(o.format));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salva il master"), suggested,
        QStringLiteral("Audio (*%1)").arg(QString::fromStdString(
            sanity::export_extension(o.format))));
    if (path.isEmpty()) return;

    std::string err;
    if (!sanity::encode_audio(path.toStdString(), s.mastered, o, err)) {
        QMessageBox::warning(this, QStringLiteral("Esporta"),
                             QStringLiteral("Scrittura non riuscita: %1")
                                 .arg(QString::fromStdString(err)));
        return;
    }
    QString extra;
    if (also_input_->isChecked() && s.has_audio) {
        sanity::ExportOptions w;
        w.format = sanity::ExportFormat::WavFloat32;
        const QString in_path = QFileInfo(path).absolutePath() + "/" +
                                QFileInfo(path).completeBaseName() + "-originale.wav";
        std::string e2;
        if (sanity::encode_audio(in_path.toStdString(), s.audio, w, e2))
            extra = QStringLiteral("\nOriginale: %1").arg(in_path);
    }
    conform_chip_->set_state(State::Ready, QStringLiteral("Scritto"));
    QMessageBox::information(
        this, QStringLiteral("Esporta"),
        QStringLiteral("File scritto:\n%1\n\nFormato: %2\n%3%4")
            .arg(path)
            .arg(QString::fromStdString(sanity::export_format_name(o)))
            .arg(conform_label_->text())
            .arg(extra));
    AppState::get().status_message(QStringLiteral("Esportato: %1").arg(path));
}

void ExportPage::copy_rss() {
    const QString snippet =
        QStringLiteral("<item>\n"
                       "  <title>Nome episodio</title>\n"
                       "  <enclosure url=\"https://tuo-hosting.example/audio/%1.mp3\"\n"
                       "             length=\"12345678\" type=\"audio/mpeg\"/>\n"
                       "  <itunes:duration>00:42:00</itunes:duration>\n"
                       "</item>")
            .arg(QFileInfo(AppState::get().library_dir()).fileName());
    QApplication::clipboard()->setText(snippet);
    AppState::get().status_message(QStringLiteral("Snippet RSS copiato negli appunti"));
}

void ExportPage::open_spotify() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://creators.spotify.com/")));
}

}  // namespace gui
