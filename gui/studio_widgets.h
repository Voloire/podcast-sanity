// podcast-sanity GUI - studio meters and indicators
#pragma once

#include <QWidget>
#include <vector>
#include "widgets.h"

class QTimer;

namespace gui {

class VuMeter : public QWidget {
    Q_OBJECT
public:
    explicit VuMeter(const QString& caption, QWidget* parent = nullptr);
    void set_scale(double min_units, double max_units, const QString& unit);
    void set_ticks(const std::vector<double>& units);
    void set_reference(double dbfs_at_zero);
    void set_value_dbfs(double dbfs);
    void set_peak_dbfs(double dbfs);
    void set_target(double units);
    void reset();
    QSize sizeHint() const override { return QSize(260, 150); }
    QSize minimumSizeHint() const override { return QSize(180, 110); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void tick();
    QString caption_, unit_ = QStringLiteral("VU");
    double min_ = -20.0, max_ = 3.0, reference_ = -18.0;
    double target_ = -200.0, shown_ = -200.0;
    bool has_target_ = false;
    double marker_ = 0.0;
    bool has_marker_ = false;
    std::vector<double> ticks_ = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    QTimer* timer_ = nullptr;
    qint64 peak_lit_until_ = 0;
};

class OnAirSign : public QWidget {
    Q_OBJECT
public:
    explicit OnAirSign(QWidget* parent = nullptr);
    void set_on(bool on);
    void set_text(const QString& text);
    QSize sizeHint() const override { return QSize(132, 44); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    bool on_ = false;
    QString text_ = QStringLiteral("ON AIR");
};

class LedIndicator : public QWidget {
    Q_OBJECT
public:
    explicit LedIndicator(QWidget* parent = nullptr);
    void set_state(State s);
    QSize sizeHint() const override { return QSize(14, 14); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    State state_ = State::Idle;
    QTimer* pulse_ = nullptr;
    double phase_ = 0.0;
};

}  // namespace gui
