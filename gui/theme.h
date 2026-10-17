// podcast-sanity GUI - colour tokens and global style sheet
#pragma once

#include <QColor>
#include <QString>

namespace gui {

enum class ThemeMode { Light, Dark, Studio };

struct Tokens {
    QColor bg;          // window background (cream / charcoal)
    QColor bg_alt;      // slightly off background, used for zebra rows
    QColor card;        // card surface
    QColor card_alt;    // nested surface (headers, code blocks)
    QColor sidebar;     // navigation rail
    QColor border;      // hairlines
    QColor text;
    QColor text_dim;
    QColor text_faint;
    QColor accent;      // sage
    QColor accent_soft; // sage at low alpha, for selected rows and chips
    QColor accent_text; // text on top of the accent
    QColor success;
    QColor warning;
    QColor danger;
    QColor meter_low;
    QColor meter_mid;
    QColor meter_high;
    QColor panel_hi, panel_lo, vu_face, vu_needle, led_off, rec, glow;
};

const Tokens& tokens();
void set_mode(ThemeMode m);
ThemeMode mode();
// The application style sheet, rebuilt from the current tokens.
QString style_sheet();
// Monospaced family used for numbers, logs and the quality report.
QString mono_family();
QString ui_family();

}  // namespace gui
