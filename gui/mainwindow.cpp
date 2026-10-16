#include "mainwindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVBoxLayout>

#include "pages/pages.h"
#include "theme.h"
#include "widgets.h"

using namespace sanity;

namespace gui {

namespace {
struct NavEntry {
    const char* glyph;
    const char* label;
};
// Order matches the mockup: capture, mastering, standards, library, export,
// settings, diagnostics.
const NavEntry kNav[] = {
    {"[ * ]", "Registrazione"},
    {"[ ~ ]", "Sanity Master"},
    {"[ = ]", "Standard & Loudness"},
    {"[ # ]", "Libreria / Sessioni"},
    {"[ > ]", "Esporta & Pubblica"},
    {"[ + ]", "Impostazioni"},
    {"[ ? ]", "Diagnostica & Test"},
};
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("podcast-sanity"));
    resize(1240, 840);
    setMinimumSize(1000, 680);

    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    build_sidebar();
    build_pages();
    root->addWidget(sidebar_);
    root->addWidget(stack_, 1);
    setCentralWidget(central);

    build_statusbar();

    // ---------------------------------------------------------------- menu
    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("Apri audio..."), QKeySequence::Open, this,
                    &MainWindow::open_audio);
    file->addAction(QStringLiteral("Salva sessione"), QKeySequence::Save, this,
                    &MainWindow::save_session);
    file->addSeparator();
    file->addAction(QStringLiteral("Esporta master..."), this, &MainWindow::export_quick);
    file->addSeparator();
    file->addAction(QStringLiteral("Esci"), QKeySequence::Quit, this, &QWidget::close);

    auto* master = menuBar()->addMenu(QStringLiteral("&Master"));
    master->addAction(QStringLiteral("Elabora con Sanity Master"), QKeySequence(Qt::Key_F5), this,
                      &MainWindow::start_mastering);

    auto* view = menuBar()->addMenu(QStringLiteral("&Vista"));
    view->addAction(QStringLiteral("Cambia tema"), this, &MainWindow::toggle_theme);

    auto* help = menuBar()->addMenu(QStringLiteral("&Aiuto"));
    help->addAction(QStringLiteral("Diagnostica e test"), this, [this] { select_page(6); });
    help->addAction(QStringLiteral("Informazioni"), this, &MainWindow::show_about);

    connect(&AppState::get(), &AppState::status_message, this,
            [this](const QString& t) { status_->setText(t); });
    connect(&AppState::get(), &AppState::audio_changed, this, [this] {
        const Session& s = AppState::get().session();
        subtitle_->setText(s.has_audio ? QStringLiteral("%1 - %2").arg(s.name, describe_audio(s.audio))
                                       : QStringLiteral("Nessun audio caricato"));
    });
    connect(&AppState::get(), &AppState::mastering_started, this, [this] {
        state_chip_->set_state(State::Busy, QStringLiteral("Elaborazione"));
    });
    connect(&AppState::get(), &AppState::mastering_finished, this,
            [this](bool ok, const QString& err) {
                if (!ok) {
                    state_chip_->set_state(State::Error, QStringLiteral("Errore"));
                    status_->setText(err);
                    return;
                }
                const Report& r = AppState::get().session().report;
                state_chip_->set_state(r.conformant ? State::Ready : State::Warning,
                                       r.conformant ? QStringLiteral("Conforme")
                                                    : QStringLiteral("Da rivedere"));
                select_page(1);
            });
    connect(&AppState::get(), &AppState::library_changed, this,
            [this] { status_->setText(QStringLiteral("Libreria aggiornata")); });
    connect(&AppState::get(), &AppState::theme_changed, this, &MainWindow::apply_theme);

    select_page(0);
    apply_theme();
}

void MainWindow::build_sidebar() {
    auto* side = new QFrame(this);
    sidebar_ = side;
    side->setObjectName("Sidebar");
    side->setFixedWidth(238);
    auto* v = new QVBoxLayout(side);
    v->setContentsMargins(16, 18, 16, 16);
    v->setSpacing(6);

    auto* brand = new QLabel(QStringLiteral("podcast\nsanity"), side);
    brand->setObjectName("BrandTitle");
    v->addWidget(brand);
    auto* tag = new QLabel(QStringLiteral("registra - pulisci - pubblica"), side);
    tag->setObjectName("BrandSub");
    v->addWidget(tag);
    v->addSpacing(14);

    nav_layout_ = v;
    nav_buttons_.clear();
    for (int i = 0; i < static_cast<int>(std::size(kNav)); i++) {
        auto* b = new NavButton(QString::fromUtf8(kNav[i].glyph),
                                QString::fromUtf8(kNav[i].label), side);
        connect(b, &QPushButton::clicked, this, [this, i] { select_page(i); });
        v->addWidget(b);
        nav_buttons_.push_back(b);
    }
    v->addStretch(1);

    theme_btn_ = new QPushButton(QStringLiteral("Tema chiaro/scuro"), side);
    theme_btn_->setObjectName("Ghost");
    connect(theme_btn_, &QPushButton::clicked, this, &MainWindow::toggle_theme);
    v->addWidget(theme_btn_);

    auto* ver = new QLabel(QStringLiteral("v%1 - GPL-3.0 - locale")
                               .arg(QStringLiteral(SANITY_VERSION)), side);
    ver->setObjectName("BrandSub");
    v->addWidget(ver);
}

void MainWindow::build_pages() {
    stack_ = new QStackedWidget(this);
    stack_->addWidget(new RecordPage(this));
    stack_->addWidget(new MasterPage(this));
    stack_->addWidget(new StandardsPage(this));
    stack_->addWidget(new LibraryPage(this));
    stack_->addWidget(new ExportPage(this));
    stack_->addWidget(new SettingsPage(this));
    stack_->addWidget(new DiagnosticsPage(this));
}

void MainWindow::build_statusbar() {
    status_ = new QLabel(QStringLiteral("Pronto. Tutto in locale: nessun upload."));
    subtitle_ = new QLabel(QStringLiteral("Nessun audio caricato"));
    subtitle_->setObjectName("Hint");
    state_chip_ = new Chip();
    state_chip_->set_state(State::Idle, QStringLiteral("In attesa"));
    auto* host = new QWidget();
    auto* hl = new QHBoxLayout(host);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(12);
    hl->addWidget(subtitle_);
    hl->addWidget(status_, 1);
    hl->addWidget(state_chip_);
    statusBar()->addWidget(host, 1);

    title_ = new QLabel(QStringLiteral("Sanity Master"));
    title_->hide();
}

void MainWindow::select_page(int index) {
    if (index < 0 || index >= stack_->count()) return;
    stack_->setCurrentIndex(index);
    for (int i = 0; i < nav_buttons_.size(); i++) nav_buttons_[i]->setChecked(i == index);
}

void MainWindow::open_audio() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Apri un audio"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        QString::fromStdString(sanity::audio_read_filter()));
    if (path.isEmpty()) return;
    sanity::AudioBuffer b;
    std::string err;
    if (!sanity::audio_read(path.toStdString(), b, err)) {
        QMessageBox::warning(this, QStringLiteral("Apri"),
                             QStringLiteral("Impossibile leggere il file:\n%1")
                                 .arg(QString::fromStdString(err)));
        return;
    }
    AppState::get().set_audio(b, path, QStringLiteral("file"));
    AppState::get().session().name = QFileInfo(path).completeBaseName();
    subtitle_->setText(QStringLiteral("%1 - %2")
                           .arg(AppState::get().session().name, describe_audio(b)));
    select_page(1);
}

void MainWindow::save_session() {
    if (!AppState::get().session().has_audio) {
        QMessageBox::information(this, QStringLiteral("Salva"),
                                 QStringLiteral("Non c'e nulla da salvare: registra o apri un "
                                                "audio."));
        return;
    }
    const QString dir = AppState::get().save_session(true);
    status_->setText(QStringLiteral("Sessione salvata in %1").arg(dir));
}

void MainWindow::export_quick() {
    select_page(4);
    status_->setText(QStringLiteral("Esporta & Pubblica: scegli il formato e premi Esporta."));
}

void MainWindow::start_mastering() {
    if (!AppState::get().session().has_audio) {
        QMessageBox::information(this, QStringLiteral("Master"),
                                 QStringLiteral("Carica o registra prima un audio."));
        select_page(0);
        return;
    }
    if (AppState::get().busy()) return;
    AppState::get().start_mastering(AppState::get().session().params);
    select_page(1);
}

void MainWindow::toggle_theme() {
    set_mode(mode() == ThemeMode::Light ? ThemeMode::Dark :
             mode() == ThemeMode::Dark ? ThemeMode::Studio : ThemeMode::Light);
    QSettings st;
    st.setValue("ui/theme", static_cast<int>(mode()));
    apply_theme();
}

void MainWindow::apply_theme() {
    qApp->setStyleSheet(style_sheet());
    if (theme_btn_)
        theme_btn_->setText(mode() == ThemeMode::Light ? QStringLiteral("Tema: chiaro") :
                            mode() == ThemeMode::Dark ? QStringLiteral("Tema: scuro")
                                                      : QStringLiteral("Tema: studio"));
}

void MainWindow::show_about() {
    QMessageBox::about(
        this, QStringLiteral("Informazioni su podcast-sanity"),
        QStringLiteral("<b>podcast-sanity %1</b><br><br>"
                       "Registrazione e mastering automatico per podcast, interamente locale.<br>"
                       "Loudness BS.1770-4, true peak, Filler Killer acustico (senza "
                       "trascrizione), Power Clean, Smart Adaptive Leveling.<br><br>"
                       "Licenza GPL-3.0. Nessun dato lascia il computer.")
            .arg(QStringLiteral(SANITY_VERSION)));
}

}  // namespace gui
