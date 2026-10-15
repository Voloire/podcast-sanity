// podcast-sanity GUI - main window: navigation rail, pages and status bar
#pragma once

#include <QMainWindow>

class QLabel;
class QStackedWidget;
class QPushButton;
class QVBoxLayout;

namespace gui {

class Chip;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Selects a page by index; public so the automated self test can walk the
    // whole interface without a mouse.
    void select_page(int index);

private:
    void build_sidebar();
    void build_pages();
    void build_statusbar();
    void open_audio();
    void save_session();
    void export_quick();
    void start_mastering();
    void toggle_theme();
    void apply_theme();
    void show_about();

    QStackedWidget* stack_ = nullptr;
    QWidget* sidebar_ = nullptr;
    QVBoxLayout* nav_layout_ = nullptr;
    QVector<QPushButton*> nav_buttons_;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    Chip* state_chip_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* theme_btn_ = nullptr;
};

}  // namespace gui
