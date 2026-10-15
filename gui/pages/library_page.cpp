#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "pages/pages.h"
#include "theme.h"
#include "widgets.h"

using namespace sanity;

namespace gui {

LibraryPage::LibraryPage(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    page_ = new Page("Libreria e sessioni",
                          "Ogni episodio salvato tiene sorgente, impostazioni, master e report di "
                          "qualita: puoi riaprire, riesportare o rifare il mastering.",
                          "0 sessioni", this);
    outer->addWidget(page_);
    auto* col = page_->column();
    const int at = col->count() - 1;

    auto* folder_card = new Card(QStringLiteral("Cartella della libreria"),
                                 QStringLiteral("Tutto resta sul tuo computer."));
    {
        folder_ = new QLabel(AppState::get().library_dir());
        folder_->setObjectName("Mono");
        auto* choose = new QPushButton(QStringLiteral("Cambia cartella..."));
        connect(choose, &QPushButton::clicked, this, &LibraryPage::choose_folder);
        folder_card->add_widget(folder_);
        folder_card->add_widget(choose);
        summary_ = new QLabel(QStringLiteral("Nessuna sessione salvata."));
        summary_->setObjectName("Hint");
        folder_card->add_widget(summary_);
    }
    col->insertWidget(at, folder_card);

    auto* list_card = new Card(QStringLiteral("Sessioni"),
                               QStringLiteral("Ordinate dalla piu recente."));
    {
        auto* tools = new QWidget();
        auto* thl = new QHBoxLayout(tools);
        thl->setContentsMargins(0, 0, 0, 0);
        search_ = new QLineEdit();
        search_->setPlaceholderText(QStringLiteral("Cerca per nome..."));
        connect(search_, &QLineEdit::textChanged, this, [this] { reload(); });
        thl->addWidget(search_, 1);
        auto* open = new QPushButton(QStringLiteral("Apri"));
        connect(open, &QPushButton::clicked, this, &LibraryPage::open_selected);
        thl->addWidget(open);
        auto* exp = new QPushButton(QStringLiteral("Esporta di nuovo"));
        connect(exp, &QPushButton::clicked, this, &LibraryPage::export_selected);
        thl->addWidget(exp);
        auto* rev = new QPushButton(QStringLiteral("Mostra cartella"));
        connect(rev, &QPushButton::clicked, this, &LibraryPage::reveal_selected);
        thl->addWidget(rev);
        auto* del = new QPushButton(QStringLiteral("Elimina"));
        del->setObjectName("Danger");
        connect(del, &QPushButton::clicked, this, &LibraryPage::delete_selected);
        thl->addWidget(del);
        list_card->add_widget(tools);

        table_ = new QTableWidget(0, 7);
        table_->setHorizontalHeaderLabels({QStringLiteral("Nome"), QStringLiteral("Data"),
                                           QStringLiteral("Durata"), QStringLiteral("LUFS"),
                                           QStringLiteral("True peak"),
                                           QStringLiteral("Filler"),
                                           QStringLiteral("Conforme")});
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->verticalHeader()->setVisible(false);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setMinimumHeight(320);
        connect(table_, &QTableWidget::doubleClicked, this, [this](const QModelIndex&) {
            open_selected();
        });
        list_card->add_widget(table_);
    }
    col->insertWidget(at + 1, list_card);

    connect(&AppState::get(), &AppState::library_changed, this, &LibraryPage::reload);
    reload();
}

void LibraryPage::reload() {
    sessions_ = AppState::get().library_sessions();
    const QString q = search_->text().trimmed();
    int shown = 0;
    table_->setRowCount(0);
    for (const Session& s : sessions_) {
        if (!q.isEmpty() && !s.name.contains(q, Qt::CaseInsensitive)) continue;
        const int r = table_->rowCount();
        table_->insertRow(r);
        table_->setItem(r, 0, new QTableWidgetItem(s.name));
        table_->setItem(r, 1, new QTableWidgetItem(s.created));
        table_->setItem(r, 2, new QTableWidgetItem(fmt_seconds(s.duration())));
        table_->setItem(r, 3, new QTableWidgetItem(s.has_report
                                                      ? fmt_lufs(s.report.after.integrated_lufs)
                                                      : QStringLiteral("\u2014")));
        table_->setItem(r, 4, new QTableWidgetItem(s.has_report
                                                      ? fmt_db(s.report.after.true_peak_db)
                                                      : QStringLiteral("\u2014")));
        table_->setItem(r, 5,
                        new QTableWidgetItem(s.has_report ? fmt_seconds(
                                                                s.report.filler_seconds_removed)
                                                          : QStringLiteral("\u2014")));
        auto* conf = new QTableWidgetItem(
            s.has_report ? (s.report.conformant ? QStringLiteral("si") : QStringLiteral("no"))
                         : QStringLiteral("\u2014"));
        table_->setItem(r, 6, conf);
        table_->setRowHeight(r, table_->rowHeight(r) + 4);
        shown++;
    }
    table_->resizeColumnsToContents();
    folder_->setText(AppState::get().library_dir());
    summary_->setText(QStringLiteral("%1 sessioni in libreria%2.")
                          .arg(sessions_.size())
                          .arg(shown != sessions_.size()
                                   ? QStringLiteral(" (%1 visibili)").arg(shown)
                                   : QString()));
    page_->set_badge(QStringLiteral("%1 sessioni").arg(sessions_.size()),
                     sessions_.isEmpty() ? State::Idle : State::Ready);
}

QString LibraryPage::selected_id() const {
    const int r = table_->currentRow();
    if (r < 0) return {};
    const QString name = table_->item(r, 0)->text();
    for (const Session& s : sessions_)
        if (s.name == name) return s.id;
    return {};
}

void LibraryPage::open_selected() {
    const QString id = selected_id();
    if (id.isEmpty()) return;
    QString err;
    if (!AppState::get().load_session(id, &err))
        QMessageBox::warning(this, QStringLiteral("Libreria"), err);
}

void LibraryPage::delete_selected() {
    const QString id = selected_id();
    if (id.isEmpty()) return;
    const auto answer = QMessageBox::question(this, QStringLiteral("Elimina sessione"),
                                              QStringLiteral("Eliminare definitivamente la "
                                                             "sessione \"%1\"?").arg(id));
    if (answer != QMessageBox::Yes) return;
    AppState::get().delete_session(id);
    reload();
}

void LibraryPage::reveal_selected() {
    const QString id = selected_id();
    const QString dir = QDir(AppState::get().library_dir()).filePath(id.isEmpty() ? "." : id);
    QProcess::startDetached(QStringLiteral("xdg-open"), {dir});
}

void LibraryPage::export_selected() {
    const QString id = selected_id();
    if (id.isEmpty()) return;
    QString err;
    AppState::get().load_session(id, &err);
    const Session& s = AppState::get().session();
    if (!s.has_mastered) {
        QMessageBox::information(this, QStringLiteral("Libreria"),
                                 QStringLiteral("Questa sessione non ha un master salvato: "
                                                "apri Sanity Master ed elabora."));
        return;
    }
    sanity::ExportOptions o;
    o.format = sanity::ExportFormat::Mp3;
    o.mp3_bitrate_kbps = sanity::recommended_mp3_bitrate(s.mastered.channels);
    QString error;
    const QString path = AppState::get().export_audio(s.mastered, o, s.name, &error);
    if (path.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Esporta"), error);
    else
        QMessageBox::information(this, QStringLiteral("Esporta"),
                                 QStringLiteral("Scritto:\n%1").arg(path));
}

void LibraryPage::choose_folder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Cartella della libreria"), AppState::get().library_dir());
    if (dir.isEmpty()) return;
    AppState::get().set_library_dir(dir);
    reload();
}

}  // namespace gui
