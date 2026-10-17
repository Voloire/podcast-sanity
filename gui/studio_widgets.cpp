// podcast-sanity GUI - studio furniture: VU meter, on-air sign, LEDs
#include "studio_widgets.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "theme.h"

namespace gui {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSweepDeg = 100.0;
}  // namespace

VuMeter::VuMeter(const QString& caption, QWidget* parent) : QWidget(parent), caption_(caption) {
    setMinimumHeight(110);
    timer_ = new QTimer(this);
    timer_->setInterval(30);
    connect(timer_, &QTimer::timeout, this, &VuMeter::tick);
}

void VuMeter::set_scale(double min_units, double max_units, const QString& unit) {
    min_ = min_units; max_ = max_units; unit_ = unit; update();
}
void VuMeter::set_ticks(const std::vector<double>& units) { ticks_ = units; update(); }
void VuMeter::set_reference(double dbfs_at_zero) { reference_ = dbfs_at_zero; update(); }
void VuMeter::set_value_dbfs(double dbfs) {
    target_ = std::clamp(dbfs - reference_, min_ - 1.0, max_ + 1.0);
    has_target_ = true;
    if (shown_ < -100.0) shown_ = min_;
    if (!timer_->isActive()) timer_->start();
}
void VuMeter::set_peak_dbfs(double dbfs) {
    if (dbfs > -1.0) {
        peak_lit_until_ = QDateTime::currentMSecsSinceEpoch() + 1000;
        if (!timer_->isActive()) timer_->start();
        update();
    }
}
void VuMeter::set_target(double units) { marker_ = units; has_marker_ = true; update(); }
void VuMeter::reset() { has_target_ = false; shown_ = min_; peak_lit_until_ = 0; update(); }

void VuMeter::tick() {
    // VU ballistics: about 300 ms to 99 %, same up and down.
    const double a = 1.0 - std::exp(-30.0 / 100.0);
    shown_ += a * ((has_target_ ? target_ : min_) - shown_);
    if (std::fabs(shown_ - (has_target_ ? target_ : min_)) < 0.02 &&
        QDateTime::currentMSecsSinceEpoch() > peak_lit_until_)
        timer_->stop();
    update();
}

void VuMeter::paintEvent(QPaintEvent*) {
    const Tokens& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect().adjusted(1, 1, -1, -1);
    QPainterPath bezel;
    bezel.addRoundedRect(r, 10, 10);
    QLinearGradient metal(r.topLeft(), r.bottomLeft());
    metal.setColorAt(0.0, t.panel_hi);
    metal.setColorAt(1.0, t.panel_lo);
    p.fillPath(bezel, metal);
    p.setPen(QPen(t.border, 1));
    p.drawPath(bezel);
    const QRectF face = r.adjusted(10, 10, -10, -28);
    QPainterPath fp;
    fp.addRoundedRect(face, 6, 6);
    p.fillPath(fp, t.vu_face);

    const QPointF pivot(face.center().x(), face.bottom() + face.height() * 0.55);
    const double radius = face.height() * 1.25;
    const double a0 = 270.0 - kSweepDeg / 2.0;
    auto angle_of = [&](double units) {
        const double f = std::clamp((units - min_) / (max_ - min_), 0.0, 1.0);
        return a0 + f * kSweepDeg;
    };
    auto at = [&](double deg, double rad) {
        const double ra = deg * kPi / 180.0;
        return QPointF(pivot.x() + rad * std::cos(ra), pivot.y() + rad * std::sin(ra));
    };
    auto arc = [&](double from_units, double to_units, double rad, const QPen& pen) {
        QPolygonF poly;
        const double d0 = angle_of(from_units), d1 = angle_of(to_units);
        for (double d = d0; d <= d1 + 1e-9; d += 1.0) poly << at(d, rad);
        p.setPen(pen);
        p.drawPolyline(poly);
    };
    p.setClipPath(fp);
    arc(min_, std::min(max_, 0.0), radius, QPen(t.vu_needle, 2));
    if (max_ > 0.0) arc(0.0, max_, radius, QPen(t.danger, 4));
    QFont small = font();
    small.setPointSizeF(std::max(6.5, font().pointSizeF() - 2.5));
    p.setFont(small);
    for (double u : ticks_) {
        if (u < min_ || u > max_) continue;
        const double d = angle_of(u);
        const bool major = std::fabs(u - std::round(u)) < 1e-9 && (static_cast<int>(std::round(u)) % 5 == 0 || u >= -3.0);
        p.setPen(QPen(u > 0 ? t.danger : t.vu_needle, major ? 2 : 1));
        p.drawLine(at(d, radius - (major ? 10 : 6)), at(d, radius));
        const bool label_tick = unit_ != QStringLiteral("VU") ||
                                u == -20 || u == -10 || u == -5 || u == 0 || u == 3;
        if (label_tick) {
            const QString label = u > 0 ? QStringLiteral("+%1").arg(u) : QString::number(u);
            const QPointF c = at(d, radius + 14);
            p.drawText(QRectF(c.x() - 16, c.y() - 8, 32, 16), Qt::AlignCenter, label);
        }
    }
    if (has_marker_) {
        p.setPen(QPen(t.accent, 2, Qt::DashLine));
        p.drawLine(at(angle_of(marker_), radius - 14), at(angle_of(marker_), radius + 6));
    }
    const double nd = angle_of(has_target_ ? shown_ : min_);
    p.setPen(QPen(QColor(0, 0, 0, 60), 4));
    p.drawLine(pivot + QPointF(1.5, 1.5), at(nd, radius + 4) + QPointF(1.5, 1.5));
    p.setPen(QPen(t.vu_needle, 2));
    p.drawLine(pivot, at(nd, radius + 4));
    p.setClipping(false);
    p.setBrush(t.vu_needle);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(pivot.x(), face.bottom() - 2), 5, 5);

    p.setPen(t.vu_needle);
    QFont unit = font();
    unit.setBold(true);
    p.setFont(unit);
    p.drawText(QRectF(face.left(), face.bottom() - 32, face.width(), 20), Qt::AlignCenter, unit_);
    p.setFont(font());
    p.setPen(t.text_dim);
    p.drawText(QRectF(r.left() + 12, r.bottom() - 22, r.width() - 24, 18), Qt::AlignLeft | Qt::AlignVCenter, caption_);
    const bool lit = QDateTime::currentMSecsSinceEpoch() < peak_lit_until_;
    const QPointF led(r.right() - 18, r.bottom() - 13);
    if (lit) {
        p.setBrush(t.glow);
        p.drawEllipse(led, 9, 9);
    }
    p.setBrush(lit ? t.danger : t.led_off);
    p.drawEllipse(led, 5, 5);
    p.setPen(t.text_faint);
    p.drawText(QRectF(led.x() - 60, led.y() - 8, 50, 16), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("PEAK"));
}

OnAirSign::OnAirSign(QWidget* parent) : QWidget(parent) { setMinimumSize(110, 36); }
void OnAirSign::set_on(bool on) { on_ = on; update(); }
void OnAirSign::set_text(const QString& text) { text_ = text; update(); }
void OnAirSign::paintEvent(QPaintEvent*) {
    const Tokens& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect().adjusted(4, 4, -4, -4);
    if (on_) {
        QPainterPath glow;
        glow.addRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
        p.fillPath(glow, QColor(t.rec.red(), t.rec.green(), t.rec.blue(), 70));
    }
    QPainterPath body;
    body.addRoundedRect(r, 7, 7);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (on_) {
        g.setColorAt(0.0, t.rec.lighter(115));
        g.setColorAt(1.0, t.rec.darker(120));
    } else {
        g.setColorAt(0.0, t.panel_hi);
        g.setColorAt(1.0, t.panel_lo);
    }
    p.fillPath(body, g);
    p.setPen(QPen(on_ ? t.rec.darker(140) : t.border, 1));
    p.drawPath(body);
    QFont f = font();
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    f.setPointSizeF(std::max(8.0, font().pointSizeF()) + 2.0);
    p.setFont(f);
    p.setPen(on_ ? QColor("#FFF6F2") : t.text_faint);
    p.drawText(r, Qt::AlignCenter, text_);
}

LedIndicator::LedIndicator(QWidget* parent) : QWidget(parent) {
    setFixedSize(14, 14);
    pulse_ = new QTimer(this);
    pulse_->setInterval(40);
    connect(pulse_, &QTimer::timeout, this, [this] {
        phase_ = std::fmod(phase_ + 0.08, 2.0 * kPi);
        update();
    });
}
void LedIndicator::set_state(State s) {
    state_ = s;
    if (s == State::Busy) pulse_->start();
    else pulse_->stop();
    update();
}
void LedIndicator::paintEvent(QPaintEvent*) {
    const Tokens& t = tokens();
    QColor c = t.led_off;
    switch (state_) {
        case State::Idle: c = t.led_off; break;
        case State::Busy: c = t.accent; break;
        case State::Ready: c = t.success; break;
        case State::Warning: c = t.warning; break;
        case State::Error: c = t.danger; break;
    }
    if (state_ == State::Busy) c.setAlphaF(0.55 + 0.45 * (0.5 + 0.5 * std::sin(phase_)));
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    if (state_ != State::Idle) {
        QColor glow = c;
        glow.setAlpha(70);
        p.setBrush(glow);
        p.drawEllipse(rect().center(), 7, 7);
    }
    p.setBrush(c);
    p.drawEllipse(rect().center(), 4, 4);
    p.setBrush(QColor(255, 255, 255, state_ == State::Idle ? 40 : 110));
    p.drawEllipse(QPointF(rect().center().x() - 1.3, rect().center().y() - 1.3), 1.2, 1.2);
}

}  // namespace gui
