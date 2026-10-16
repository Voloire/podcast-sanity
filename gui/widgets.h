// podcast-sanity GUI - reusable presentation widgets
#pragma once

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVector>
#include <QWidget>

class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;

namespace gui {

// --------------------------------------------------------------------- chip
// A small status pill. Every panel and every long running operation in the app
// reports through one of these, so the state of the tool is always visible.
enum class State { Idle, Busy, Ready, Warning, Error };

QString state_name(State s);

class Chip : public QLabel {
    Q_OBJECT
public:
    explicit Chip(QWidget* parent = nullptr);
    void set_state(State s, const QString& text);
    State state() const { return state_; }

private:
    State state_ = State::Idle;
};

// --------------------------------------------------------------------- card
// A titled surface with an optional collapsible body. The header always shows a
// chip, so a card can be read at a glance without opening it.
class Card : public QFrame {
    Q_OBJECT
public:
    explicit Card(const QString& title, const QString& subtitle = QString(),
                  QWidget* parent = nullptr);

    // Content goes into this layout.
    QVBoxLayout* body() const { return body_; }
    QHBoxLayout* header_trailing() const { return header_trailing_; }
    void add_widget(QWidget* w);
    void add_row(QWidget* left, QWidget* right);
    void set_state(State s, const QString& text);
    void set_collapsible(bool on);
    bool is_collapsed() const { return collapsed_; }
    void set_collapsed(bool on);

private:
    void toggle();
    QVBoxLayout* outer_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    QWidget* body_host_ = nullptr;
    QHBoxLayout* header_trailing_ = nullptr;
    Chip* chip_ = nullptr;
    QPushButton* collapse_ = nullptr;
    bool collapsible_ = false;
    bool collapsed_ = false;
};

// ------------------------------------------------------------------- tiles
// A labelled number, the unit of information on the standards and master pages.
class StatTile : public QFrame {
    Q_OBJECT
public:
    explicit StatTile(const QString& caption, const QString& unit = QString(),
                      QWidget* parent = nullptr);
    void set_value(const QString& text);
    void set_caption(const QString& text);
    void set_accent(bool on);
    void set_state(State s);
    QString value() const;

private:
    QLabel* value_ = nullptr;
    QLabel* unit_ = nullptr;
    QLabel* caption_ = nullptr;
};

// ------------------------------------------------------------- level meter
// Horizontal bar meter on a dBFS scale, with a peak hold tick. Used for the
// input and output level in the recorder and for the loudness history.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);
    void set_range(double min_db, double max_db);
    void set_value(double db);
    void set_peak(double db);
    void set_target(double db);  // draws a marker, e.g. the true peak ceiling
    void reset();
    // True peak marker, drawn slightly brighter than the bar peak.
    void set_true_peak(double db);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    double min_db_ = -60.0;
    double max_db_ = 0.0;
    double value_ = -60.0;
    double peak_ = -60.0;
    double true_peak_ = -60.0;
    bool has_target_ = false;
    double target_ = 0.0;
};

// ---------------------------------------------------------------- waveform
// Min/max envelope of a mono signal, with optional marker overlays (filler hits,
// silence, clip). Cheap enough to repaint while recording.
class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);

    void set_samples(const std::vector<float>& mono, int sample_rate);
    void append_samples(const float* samples, std::size_t n, int sample_rate);
    void clear();
    void set_markers(const QString& label, const QVector<QPair<double, double>>& ranges);
    void set_playhead(double seconds);
    void set_show_playhead(bool on);
    double duration() const { return duration_s_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void rebuild();
    std::vector<float> mono_;
    int sample_rate_ = 48000;
    std::vector<float> env_min_;
    std::vector<float> env_max_;
    int buckets_ = 0;
    double duration_s_ = 0.0;
    double playhead_ = -1.0;
    bool show_playhead_ = false;
    QString marker_label_;
    QVector<QPair<double, double>> markers_;
};

// --------------------------------------------------------------- nav button
class NavButton : public QPushButton {
    Q_OBJECT
public:
    NavButton(const QString& glyph, const QString& text, QWidget* parent = nullptr);
};

// ------------------------------------------------------------- misc helpers
QString fmt_db(double db, int decimals = 1);
QString fmt_lufs(double lufs);
QString fmt_seconds(double seconds);
QString fmt_ms(double ms);
QString fmt_bytes(qint64 bytes);

// A row of "label / control / hint" used on the settings and master pages.
QWidget* make_row(const QString& label, QWidget* control, const QString& hint = QString());
QWidget* make_section(const QString& text);

}  // namespace gui
