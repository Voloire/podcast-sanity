#include "theme.h"

#include <QFontDatabase>

namespace gui {

namespace {

ThemeMode g_mode = ThemeMode::Light;

Tokens make_light() {
    Tokens t;
    t.bg = QColor("#F4F1E8");          // cream
    t.bg_alt = QColor("#EDE9DE");
    t.card = QColor("#FFFFFF");
    t.card_alt = QColor("#F7F5EE");
    t.sidebar = QColor("#E7E3D6");
    t.border = QColor("#D8D3C4");
    t.text = QColor("#2B302A");
    t.text_dim = QColor("#5C635A");
    t.text_faint = QColor("#8A9187");
    t.accent = QColor("#7C9082");       // sage
    t.accent_soft = QColor("#DDE6DC");
    t.accent_text = QColor("#FFFFFF");
    t.success = QColor("#5F8A62");
    t.warning = QColor("#B0821F");
    t.danger = QColor("#A8513C");
    t.meter_low = QColor("#7C9082");
    t.meter_mid = QColor("#B0A24F");
    t.meter_high = QColor("#B5643F");
    t.panel_hi = QColor("#F7F5EE");
    t.panel_lo = QColor("#E7E3D6");
    t.vu_face = QColor("#FFFDF6");
    t.vu_needle = QColor("#2B302A");
    t.led_off = QColor("#C9C4B6");
    t.rec = QColor("#C0453A");
    t.glow = QColor(124, 144, 130, 70);
    return t;
}

Tokens make_dark() {
    Tokens t;
    t.bg = QColor("#181C19");
    t.bg_alt = QColor("#1F2420");
    t.card = QColor("#222823");
    t.card_alt = QColor("#1B201C");
    t.sidebar = QColor("#141815");
    t.border = QColor("#333B34");
    t.text = QColor("#E9ECE2");
    t.text_dim = QColor("#A9B2A5");
    t.text_faint = QColor("#76806F");
    t.accent = QColor("#9CB8A2");
    t.accent_soft = QColor("#2C3A2F");
    t.accent_text = QColor("#0F1310");
    t.success = QColor("#8FBE92");
    t.warning = QColor("#D8AE55");
    t.danger = QColor("#D98A73");
    t.meter_low = QColor("#9CB8A2");
    t.meter_mid = QColor("#CFC168");
    t.meter_high = QColor("#D08A5F");
    t.panel_hi = QColor("#262C27");
    t.panel_lo = QColor("#1B201C");
    t.vu_face = QColor("#E9ECE2");
    t.vu_needle = QColor("#181C19");
    t.led_off = QColor("#333B34");
    t.rec = QColor("#D2503F");
    t.glow = QColor(156, 184, 162, 80);
    return t;
}

Tokens make_studio() {
    Tokens t;
    t.bg = QColor("#0E1012");
    t.bg_alt = QColor("#15181B");
    t.card = QColor("#1A1E22");
    t.card_alt = QColor("#14171A");
    t.sidebar = QColor("#0B0D0F");
    t.border = QColor("#2A3036");
    t.text = QColor("#E6E3DA");
    t.text_dim = QColor("#A4A79E");
    t.text_faint = QColor("#6C716A");
    t.accent = QColor("#F2A33C");
    t.accent_soft = QColor("#3A2C14");
    t.accent_text = QColor("#141414");
    t.success = QColor("#46D37A");
    t.warning = QColor("#F2C14E");
    t.danger = QColor("#E0463A");
    t.meter_low = QColor("#46D37A");
    t.meter_mid = QColor("#F2C14E");
    t.meter_high = QColor("#E0463A");
    t.panel_hi = QColor("#262C31");
    t.panel_lo = QColor("#15191C");
    t.vu_face = QColor("#F4EAD2");
    t.vu_needle = QColor("#2B2B2B");
    t.led_off = QColor("#3A3F44");
    t.rec = QColor("#E0463A");
    t.glow = QColor(242, 163, 60, 90);
    return t;
}

}  // namespace

const Tokens& tokens() {
    static const Tokens light = make_light();
    static const Tokens dark = make_dark();
    static const Tokens studio = make_studio();
    if (g_mode == ThemeMode::Studio) return studio;
    return g_mode == ThemeMode::Light ? light : dark;
}

void set_mode(ThemeMode m) { g_mode = m; }
ThemeMode mode() { return g_mode; }

QString mono_family() {
    static const QString family = [] {
        const QStringList wanted = {"Ubuntu Mono", "JetBrains Mono", "Cascadia Mono", "DejaVu Sans Mono",
                                    "Menlo", "Consolas", "monospace"};
        const QStringList have = QFontDatabase::families();
        for (const QString& w : wanted)
            if (have.contains(w)) return w;
        return QString("monospace");
    }();
    return family;
}

QString ui_family() {
    static const QString family = [] {
        const QStringList have = QFontDatabase::families();
        for (const QString& w : {QStringLiteral("Ubuntu"), QStringLiteral("Inter"),
                                 QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans")})
            if (have.contains(w)) return w;
        return QString("sans-serif");
    }();
    return family;
}

QString style_sheet() {
    const Tokens& t = tokens();
    auto c = [](const QColor& x) { return x.name(QColor::HexArgb); };

    QString qss = QString(R"(
* { outline: none; }
QWidget {
    background: %1;
    color: %3;
    font-family: "%18";
    font-size: 13px;
}
QToolTip {
    background: %2; color: %3; border: 1px solid %6; padding: 6px 8px; border-radius: 6px;
}

/* ---------------------------------------------------------------- surfaces */
QFrame#Card {
    background: %2;
    border: 1px solid %6;
    border-radius: 12px;
}
QFrame#CardHeader {
    background: transparent;
    border: none;
    border-bottom: 1px solid %6;
}
QLabel#CardTitle { font-size: 14px; font-weight: 600; background: transparent; }
QLabel#CardSubtitle { color: %4; font-size: 12px; background: transparent; }
QLabel#PageTitle { font-size: 22px; font-weight: 650; background: transparent; }
QLabel#PageSubtitle { color: %4; font-size: 13px; background: transparent; }
QLabel#SectionLabel {
    color: %5; font-size: 11px; font-weight: 600; letter-spacing: 1px;
    text-transform: uppercase; background: transparent;
}
QLabel#Hint { color: %4; font-size: 12px; background: transparent; }
QLabel#Mono { font-family: "%9"; font-size: 12px; background: transparent; }
QLabel#BigNumber { font-size: 30px; font-weight: 650; background: transparent; }
QLabel#Unit { color: %4; font-size: 13px; background: transparent; }

QFrame#Sidebar { background: %7; border: none; border-right: 1px solid %6; }
QLabel#BrandTitle { font-size: 15px; font-weight: 650; background: transparent; }
QLabel#BrandSub { color: %5; font-size: 11px; background: transparent; }

/* --------------------------------------------------------------- navigation */
QPushButton#Nav {
    text-align: left; padding: 9px 12px; border-radius: 9px; border: none;
    background: transparent; color: %4; font-size: 13px;
}
QPushButton#Nav:hover { background: rgba(127,127,127,0.12); color: %3; }
QPushButton#Nav:checked { background: %8; color: %3; font-weight: 600; }

/* ------------------------------------------------------------------ buttons */
QPushButton {
    background: %2; color: %3; border: 1px solid %6; border-radius: 8px;
    padding: 7px 14px; font-size: 13px;
}
QPushButton:hover { border-color: %10; }
QPushButton:pressed { background: %11; }
QPushButton:disabled { color: %5; border-color: %6; }
QPushButton#Primary {
    background: %10; color: %12; border: 1px solid %10; font-weight: 600;
}
QPushButton#Primary:hover { background: %13; border-color: %13; }
QPushButton#Primary:disabled { background: %5; border-color: %5; color: %2; }
QPushButton#Danger { color: %14; border-color: %14; }
QPushButton#Ghost { background: transparent; border-color: transparent; color: %4; }
QPushButton#Ghost:hover { background: rgba(127,127,127,0.12); }
QPushButton#Rec {
    background: #C0453A; color: white; border: 1px solid #C0453A;
    font-weight: 700; font-size: 15px; padding: 14px 24px; border-radius: 12px;
}
QPushButton#Rec:hover { background: #D2503F; }
QPushButton#Transport {
    min-width: 44px; padding: 9px 14px; font-family: "%9"; font-size: 14px;
}
QPushButton#Rec, QPushButton#Transport { text-align: left; padding-left: 40px; }

/* ------------------------------------------------------------------ inputs */
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background: %2; border: 1px solid %6; border-radius: 8px; padding: 6px 9px;
    min-height: 18px; selection-background-color: %10;
}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover { border-color: %10; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: %2; border: 1px solid %6; selection-background-color: %8;
    selection-color: %3; padding: 4px; outline: none;
}
QCheckBox, QRadioButton { background: transparent; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }
QCheckBox::indicator {
    border: 1px solid %6; border-radius: 4px; background: %2;
}
QCheckBox::indicator:checked { background: %10; border-color: %10; }
QCheckBox::indicator:disabled { background: %11; }
QSlider::groove:horizontal { height: 4px; background: %11; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %10; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 14px; height: 14px; margin: -6px 0; border-radius: 7px;
    background: %10; border: 2px solid %2;
}
QProgressBar {
    background: %11; border: none; border-radius: 4px; height: 8px; text-align: center;
    color: %3; font-size: 11px;
}
QProgressBar::chunk { background: %10; border-radius: 4px; }

/* ------------------------------------------------------------------- tables */
QTableWidget, QTreeWidget, QListWidget {
    background: %2; border: 1px solid %6; border-radius: 10px;
    gridline-color: %6; alternate-background-color: %15;
    selection-background-color: %8; selection-color: %3;
}
QTableWidget::item, QTreeWidget::item, QListWidget::item { padding: 5px 6px; }
QHeaderView::section {
    background: %11; color: %4; border: none; border-bottom: 1px solid %6;
    padding: 6px 8px; font-size: 11px; font-weight: 650; letter-spacing: 0.5px;
}
QTableCornerButton::section { background: %11; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %6; border-radius: 5px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: %5; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %6; border-radius: 5px; min-width: 28px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollArea { border: none; background: transparent; }
QPlainTextEdit, QTextEdit {
    background: %16; border: 1px solid %6; border-radius: 10px;
    font-family: "%9"; font-size: 12px; padding: 8px;
}
QTabWidget::pane { border: 1px solid %6; border-radius: 10px; top: -1px; }
QTabBar::tab {
    background: transparent; color: %4; padding: 7px 14px; border: none;
    border-top-left-radius: 8px; border-top-right-radius: 8px;
}
QTabBar::tab:selected { background: %2; color: %3; font-weight: 600; }
QTabBar::tab:hover { color: %3; }

/* ------------------------------------------------------------------ chips */
QLabel#Chip {
    border-radius: 9px; padding: 2px 9px; font-size: 11px; font-weight: 650;
}
QLabel#ChipIdle { background: %11; color: %4; }
QLabel#ChipBusy { background: %8; color: %10; }
QLabel#ChipReady { background: %8; color: %10; }
QLabel#ChipWarn { background: rgba(176,130,31,0.18); color: %17; }
QLabel#ChipError { background: rgba(168,81,60,0.18); color: %14; }

QStatusBar { background: %7; border-top: 1px solid %6; color: %4; }
QStatusBar::item { border: none; }
QMenuBar { background: %7; color: %3; border-bottom: 1px solid %6; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: %8; color: %3; }
QMenu { background: %2; color: %3; border: 1px solid %6; padding: 4px; }
QMenu::item { padding: 5px 24px; border-radius: 6px; }
QMenu::item:selected { background: %8; color: %3; }
QMenu::separator { height: 1px; background: %6; margin: 4px 8px; }
QSplitter::handle { background: transparent; }
QGroupBox {
    border: 1px solid %6; border-radius: 10px; margin-top: 10px; padding-top: 10px;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; color: %4; }
QFrame#Sidebar {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %19, stop:1 %7);
    border: none; border-right: 1px solid %6;
}
QFrame#CardHeader {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %19, stop:1 %20);
    border: none; border-bottom: 1px solid %6;
    border-top-left-radius: 12px; border-top-right-radius: 12px;
}
QPushButton#Nav:checked { background: %8; color: %3; font-weight: 600; border-left: 3px solid %10; padding-left: 9px; }
QLabel#PageTitle { font-size: 24px; font-weight: 700; letter-spacing: 0.5px; background: transparent; }
QLabel#BrandTitle { font-size: 17px; font-weight: 700; letter-spacing: 1px; text-transform: uppercase; background: transparent; }
QLabel#Mono, QPlainTextEdit, QTextEdit { font-family: "%9"; }
)")
                        .arg(c(t.bg), c(t.card), c(t.text), c(t.text_dim), c(t.text_faint),
                             c(t.border), c(t.sidebar), c(t.accent_soft), mono_family(),
                             c(t.accent), c(t.bg_alt), c(t.accent_text), c(t.accent), c(t.danger),
                              c(t.card_alt), c(t.card_alt), c(t.warning), ui_family(),
                              c(t.panel_hi), c(t.panel_lo));
    return qss;
}

}  // namespace gui
