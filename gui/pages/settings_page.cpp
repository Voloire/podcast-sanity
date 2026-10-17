#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include "pages/pages.h"
#include "sanity/distribution.hpp"
#include "theme.h"
#include "widgets.h"

using namespace sanity;

namespace gui {

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* page = new Page("Impostazioni",
                          "Aspetto, cartelle e valori predefiniti del motore. Le preferenze "
                          "restano sul computer.",
                          "Locale", this);
    outer->addWidget(page);
    auto* col = page->column();
    const int at = col->count() - 1;

    auto* look = new Card(QStringLiteral("Aspetto"),
                          QStringLiteral("Tema chiaro, scuro o studio con accenti ambra."));
    {
        theme_ = new QComboBox();
        theme_->addItem(QStringLiteral("Chiaro (panna e salvia)"), 0);
        theme_->addItem(QStringLiteral("Scuro"), 1);
        theme_->addItem(QStringLiteral("Studio (scuro, ambra)"), 2);
        QSettings st;
        theme_->setCurrentIndex(st.value("ui/theme", 2).toInt());
        connect(theme_, &QComboBox::currentIndexChanged, this, [this] { apply_theme(); });
        look->add_widget(make_row(QStringLiteral("Tema"), theme_));
    }
    col->insertWidget(at, look);

    auto* defaults = new Card(QStringLiteral("Valori predefiniti di mastering"),
                              QStringLiteral("Applicati a ogni nuova sessione."));
    {
        preset_ = new QComboBox();
        preset_->addItem(QStringLiteral("Voce (parlato)"), 0);
        preset_->addItem(QStringLiteral("Musica"), 1);
        defaults->add_widget(make_row(QStringLiteral("Preset"), preset_));

        filler_ = new QComboBox();
        filler_->addItem(QStringLiteral("Disattivato"), 0);
        filler_->addItem(QStringLiteral("Prudente"), 1);
        filler_->addItem(QStringLiteral("Bilanciato"), 2);
        filler_->addItem(QStringLiteral("Aggressivo"), 3);
        defaults->add_widget(make_row(QStringLiteral("Filler Killer"), filler_));

        power_clean_ = new QCheckBox(QStringLiteral("Attivo"));
        defaults->add_widget(make_row(QStringLiteral("Power Clean di default"), power_clean_));

        default_format_ = new QComboBox();
        for (const sanity::DistributionProfile& d : sanity::distribution_profiles()) {
            QString label = QString::fromUtf8(d.name);
            if (d.recommended) label += QStringLiteral(" (recommended)");
            default_format_->addItem(label, QString::fromUtf8(d.id));
        }
        defaults->add_widget(make_row(QStringLiteral("Profilo di distribuzione predefinito"),
                                      default_format_));

        QSettings st;
        preset_->setCurrentIndex(st.value("master/preset", 0).toInt());
        filler_->setCurrentIndex(st.value("master/filler", 1).toInt());
        power_clean_->setChecked(st.value("master/power_clean", true).toBool());
        st.remove("export/format");
        const QString id = st.value("export/profile",
            QString::fromUtf8(sanity::recommended_distribution_profile().id)).toString();
        const int index = default_format_->findData(id);
        default_format_->setCurrentIndex(index >= 0 ? index : 0);

        auto* save = new QPushButton(QStringLiteral("Salva i predefiniti"));
        save->setObjectName("Primary");
        connect(save, &QPushButton::clicked, this, &SettingsPage::save_defaults);
        auto* reset = new QPushButton(QStringLiteral("Ripristina"));
        connect(reset, &QPushButton::clicked, this, &SettingsPage::reset_defaults);
        auto* host = new QWidget();
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(save);
        hl->addWidget(reset);
        hl->addStretch(1);
        defaults->add_widget(host);
    }
    col->insertWidget(at + 1, defaults);

    auto* paths = new Card(QStringLiteral("Cartelle"));
    {
        library_ = new QLineEdit(AppState::get().library_dir());
        auto* choose = new QPushButton(QStringLiteral("Sfoglia..."));
        connect(choose, &QPushButton::clicked, this, &SettingsPage::choose_library);
        auto* host = new QWidget();
        auto* hl = new QHBoxLayout(host);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(library_, 1);
        hl->addWidget(choose);
        paths->add_widget(make_row(QStringLiteral("Libreria (sessioni ed esportazioni)"), host));
        paths->add_widget(make_row(
            QStringLiteral("Cache (registrazioni in corso)"),
            new QLabel(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))));
    }
    col->insertWidget(at + 2, paths);

    auto* privacy = new Card(QStringLiteral("Privacy e risorse"),
                             QStringLiteral("Il mastering avviene interamente sul dispositivo."));
    {
        auto* t = new QLabel(
            QStringLiteral("Nessun upload, nessun modello cloud, nessuna telemetria. La pipeline "
                           "usa solo DSP scritto in casa (loudness BS.1770-4, biquad, FFT, STFT). "
                           "Thread disponibili su questa macchina: %1.")
                .arg(QThread::idealThreadCount()));
        t->setWordWrap(true);
        privacy->add_widget(t);
        version_ = new QLabel(QStringLiteral("Versione %1 - GPL-3.0 - motore open source")
                                  .arg(QStringLiteral(SANITY_VERSION)));
        version_->setObjectName("Mono");
        privacy->add_widget(version_);
        auto* lic = new QPushButton(QStringLiteral("Licenza e sorgenti"));
        connect(lic, &QPushButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.gnu.org/licenses/gpl-3.0.html")));
        });
        privacy->add_widget(lic);
    }
    col->insertWidget(at + 3, privacy);
}

void SettingsPage::apply_theme() {
    QSettings st;
    st.setValue("ui/theme", theme_->currentIndex());
    set_mode(theme_->currentIndex() == 0 ? ThemeMode::Light :
             theme_->currentIndex() == 1 ? ThemeMode::Dark : ThemeMode::Studio);
    emit AppState::get().theme_changed();
    AppState::get().status_message(QStringLiteral("Tema aggiornato"));
}

void SettingsPage::choose_library() {
    const QString dir =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Cartella della libreria"),
                                          library_->text());
    if (dir.isEmpty()) return;
    library_->setText(dir);
    AppState::get().set_library_dir(dir);
}

void SettingsPage::save_defaults() {
    QSettings st;
    st.setValue("master/preset", preset_->currentIndex());
    st.setValue("master/filler", filler_->currentIndex());
    st.setValue("master/power_clean", power_clean_->isChecked());
    st.setValue("export/profile", default_format_->currentData().toString());
    st.sync();
    AppState::get().set_params(AppState::get().default_params());
    AppState::get().status_message(QStringLiteral("Predefiniti salvati"));
    QMessageBox::information(this, QStringLiteral("Impostazioni"),
                             QStringLiteral("Predefiniti salvati."));
}

void SettingsPage::reset_defaults() {
    QSettings st;
    st.clear();
    st.sync();
    preset_->setCurrentIndex(0);
    filler_->setCurrentIndex(1);
    power_clean_->setChecked(true);
    default_format_->setCurrentIndex(0);
    theme_->setCurrentIndex(2);
    apply_theme();
    AppState::get().set_library_dir(QDir::homePath() + QStringLiteral("/PodcastSanity"));
    library_->setText(AppState::get().library_dir());
    AppState::get().status_message(QStringLiteral("Impostazioni ripristinate"));
}

}  // namespace gui
