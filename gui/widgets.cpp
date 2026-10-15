#include "widgets.h"

#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "theme.h"

namespace gui {

namespace {
constexpr int kEnvelopeBuckets = 2400;  // fixed so painting stays cheap
}

// --------------------------------------------------------------------- chip
QString state_name(State s) {
    switch (s) {
        case State::Idle: return QStringLiteral("In attesa");
        case State::Busy: return QStringLiteral("In corso");
        case State::Ready: return QStringLiteral("Pronto");
        case State::Warning: return QStringLiteral("Attenzione");
        case State::Error: return QStringLiteral("Errore");
    }
    return QString();
}

Chip::Chip(QWidget* parent) : QLabel(parent) {
    setObjectName("Chip");
    setAlignment(Qt::AlignCenter);
    set_state(State::Idle, state_name(State::Idle));
}

void Chip::set_state(State s, const QString& text) {
    state_ = s;
    const char* obj = "ChipIdle";
    switch (s) {
        case State::Idle: obj = "ChipIdle"; break;
        case State::Busy: obj = "ChipBusy"; break;
        case State::Ready: obj = "ChipReady"; break;
        case State::Warning: obj = "ChipWarn"; break;
        case State::Error: obj = "ChipError"; break;
    }
    setObjectName(obj);
    setText(text.isEmpty() ? state_name(s) : text);
    style()->unpolish(this);
    style()->polish(this);
}

// --------------------------------------------------------------------- card
Card::Card(const QString& title, const QString& subtitle, QWidget* parent)
    : QFrame(parent) {
    setObjectName("Card");
    outer_ = new QVBoxLayout(this);
    outer_->setContentsMargins(0, 0, 0, 0);
    outer_->setSpacing(0);

    auto* header = new QFrame(this);
    header->setObjectName("CardHeader");
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(16, 12, 12, 12);
    hl->setSpacing(10);

    auto* titles = new QVBoxLayout();
    titles->setSpacing(1);
    auto* t = new QLabel(title, header);
    t->setObjectName("CardTitle");
    titles->addWidget(t);
    if (!subtitle.isEmpty()) {
        auto* s = new QLabel(subtitle, header);
        s->setObjectName("CardSubtitle");
        s->setWordWrap(true);
        titles->addWidget(s);
    }
    hl->addLayout(titles, 1);

    header_trailing_ = new QHBoxLayout();
    header_trailing_->setSpacing(8);
    hl->addLayout(header_trailing_);

    chip_ = new Chip(header);
    hl->addWidget(chip_);

    collapse_ = new QPushButton(QStringLiteral("\u25BE"), header);
    collapse_->setObjectName("Ghost");
    collapse_->setFixedWidth(26);
    collapse_->setToolTip(QStringLiteral("Comprimi / espandi il pannello"));
    collapse_->hide();
    connect(collapse_, &QPushButton::clicked, this, [this] { toggle(); });
    hl->addWidget(collapse_);

    outer_->addWidget(header);

    body_host_ = new QWidget(this);
    body_ = new QVBoxLayout(body_host_);
    body_->setContentsMargins(16, 14, 16, 16);
    body_->setSpacing(10);
    outer_->addWidget(body_host_);
}

void Card::add_widget(QWidget* w) { body_->addWidget(w); }

void Card::add_row(QWidget* left, QWidget* right) {
    auto* row = new QWidget(body_host_);
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(10);
    hl->addWidget(left, 1);
    hl->addWidget(right);
    body_->addWidget(row);
}

void Card::set_state(State s, const QString& text) { chip_->set_state(s, text); }

void Card::set_collapsible(bool on) {
    collapsible_ = on;
    collapse_->setVisible(on);
    collapse_->setText(collapsed_ ? QStringLiteral("\u25B8") : QStringLiteral("\u25BE"));
}

void Card::set_collapsed(bool on) {
    if (!collapsible_) return;
    collapsed_ = on;
    body_host_->setVisible(!on);
    collapse_->setText(on ? QStringLiteral("\u25B8") : QStringLiteral("\u25BE"));
}

void Card::toggle() { set_collapsed(!collapsed_); }

// -------------------------------------------------------------------- tiles
StatTile::StatTile(const QString& caption, const QString& unit, QWidget* parent)
    : QFrame(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(2);

    auto* top = new QHBoxLayout();
    top->setSpacing(4);
    value_ = new QLabel(QStringLiteral("\u2014"), this);
    value_->setObjectName("BigNumber");
    top->addWidget(value_);
    unit_ = new QLabel(unit, this);
    unit_->setObjectName("Unit");
    unit_->setAlignment(Qt::AlignBottom);
    if (unit.isEmpty()) unit_->hide();
    top->addWidget(unit_);
    top->addStretch(1);
    v->addLayout(top);

    caption_ = new QLabel(caption, this);
    caption_->setObjectName("CardSubtitle");
    v->addWidget(caption_);
}

void StatTile::set_value(const QString& text) { value_->setText(text); }
void StatTile::set_caption(const QString& text) { caption_->setText(text); }
QString StatTile::value() const { return value_->text(); }

void StatTile::set_accent(bool on) {
    const Tokens& t = tokens();
    value_->setStyleSheet(
        QString("color: %1; background: transparent;").arg((on ? t.accent : t.text).name()));
}

void StatTile::set_state(State s) {
    const Tokens& t = tokens();
    QColor c = t.text;
    if (s == State::Ready) c = t.success;
    if (s == State::Warning) c = t.warning;
    if (s == State::Error) c = t.danger;
    value_->setStyleSheet(QString("color: %1; background: transparent;").arg(c.name()));
}

// -------------------------------------------------------------- level meter
LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent) { setMinimumHeight(14); }

void LevelMeter::set_range(double min_db, double max_db) {
    min_db_ = min_db;
    max_db_ = max_db;
    update();
}
void LevelMeter::set_value(double db) {
    value_ = std::clamp(db, min_db_, max_db_);
    update();
}
void LevelMeter::set_peak(double db) {
    peak_ = std::clamp(db, min_db_, max_db_);
    update();
}
void LevelMeter::set_true_peak(double db) {
    true_peak_ = std::clamp(db, min_db_, max_db_);
    update();
}
void LevelMeter::set_target(double db) {
    has_target_ = true;
    target_ = db;
    update();
}
void LevelMeter::reset() {
    value_ = peak_ = true_peak_ = min_db_;
    update();
}

QSize LevelMeter::sizeHint() const { return QSize(180, 16); }
QSize LevelMeter::minimumSizeHint() const { return QSize(80, 12); }

void LevelMeter::paintEvent(QPaintEvent*) {
    const Tokens& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect().adjusted(0, 1, -1, -1);
    const double radius = r.height() / 2.0;

    QPainterPath track;
    track.addRoundedRect(r, radius, radius);
    p.fillPath(track, t.bg_alt);

    auto to_x = [&](double db) {
        const double f = (db - min_db_) / (max_db_ - min_db_);
        return r.left() + std::clamp(f, 0.0, 1.0) * r.width();
    };

    // Filled portion, coloured by zone so clipping is obvious.
    const double x = to_x(value_);
    if (x > r.left() + 1) {
        QPainterPath fill;
        fill.addRoundedRect(QRectF(r.left(), r.top(), x - r.left(), r.height()), radius, radius);
        QLinearGradient g(r.left(), 0, r.right(), 0);
        g.setColorAt(0.0, t.meter_low);
        g.setColorAt(0.86, t.meter_mid);
        g.setColorAt(1.0, t.meter_high);
        p.fillPath(fill, g);
    }

    if (has_target_) {
        const double tx = to_x(target_);
        p.setPen(QPen(t.text_faint, 1, Qt::DashLine));
        p.drawLine(QPointF(tx, r.top()), QPointF(tx, r.bottom()));
    }

    auto tick = [&](double db, const QColor& c, double w) {
        const double px = to_x(db);
        p.setPen(QPen(c, w));
        p.drawLine(QPointF(px, r.top() - 1), QPointF(px, r.bottom() + 1));
    };
    if (peak_ > min_db_ + 0.01) tick(peak_, t.text_dim, 2.0);
    if (true_peak_ > min_db_ + 0.01) tick(true_peak_, t.text, 2.0);
}

// ----------------------------------------------------------------- waveform
WaveformView::WaveformView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize WaveformView::sizeHint() const { return QSize(640, 160); }
QSize WaveformView::minimumSizeHint() const { return QSize(240, 90); }

void WaveformView::clear() {
    mono_.clear();
    env_min_.clear();
    env_max_.clear();
    buckets_ = 0;
    duration_s_ = 0.0;
    playhead_ = -1.0;
    markers_.clear();
    update();
}

void WaveformView::set_samples(const std::vector<float>& mono, int sample_rate) {
    mono_ = mono;
    sample_rate_ = sample_rate > 0 ? sample_rate : 48000;
    duration_s_ = static_cast<double>(mono_.size()) / sample_rate_;
    rebuild();
    update();
}

void WaveformView::append_samples(const float* data, std::size_t n, int sample_rate) {
    if (sample_rate > 0) sample_rate_ = sample_rate;
    mono_.insert(mono_.end(), data, data + n);
    duration_s_ = static_cast<double>(mono_.size()) / sample_rate_;
    rebuild();
    update();
}

void WaveformView::rebuild() {
    env_min_.assign(kEnvelopeBuckets, 0.0f);
    env_max_.assign(kEnvelopeBuckets, 0.0f);
    buckets_ = kEnvelopeBuckets;
    if (mono_.empty()) return;
    const std::size_t n = mono_.size();
    for (int b = 0; b < kEnvelopeBuckets; b++) {
        const std::size_t a = n * static_cast<std::size_t>(b) / kEnvelopeBuckets;
        std::size_t z = n * static_cast<std::size_t>(b + 1) / kEnvelopeBuckets;
        if (z <= a) z = a + 1;
        if (z > n) z = n;
        float lo = 0.0f, hi = 0.0f;
        for (std::size_t i = a; i < z; i++) {
            lo = std::min(lo, mono_[i]);
            hi = std::max(hi, mono_[i]);
        }
        env_min_[b] = lo;
        env_max_[b] = hi;
    }
}

void WaveformView::set_markers(const QString& label, const QVector<QPair<double, double>>& ranges) {
    marker_label_ = label;
    markers_ = ranges;
    update();
}

void WaveformView::set_playhead(double seconds) {
    playhead_ = seconds;
    update();
}

void WaveformView::set_show_playhead(bool on) {
    show_playhead_ = on;
    update();
}

void WaveformView::resizeEvent(QResizeEvent*) { update(); }

void WaveformView::paintEvent(QPaintEvent*) {
    const Tokens& t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect().adjusted(1, 1, -1, -1);

    QPainterPath bg;
    bg.addRoundedRect(r, 10, 10);
    p.fillPath(bg, t.card_alt);
    p.setPen(QPen(t.border, 1));
    p.drawPath(bg);

    // Filler / marker overlays first, so the waveform sits on top.
    if (!markers_.isEmpty() && duration_s_ > 0.0) {
        p.setPen(Qt::NoPen);
        QColor overlay = t.accent;
        overlay.setAlpha(70);
        for (const auto& m : markers_) {
            const double x0 = r.left() + (m.first / duration_s_) * r.width();
            const double x1 = r.left() + (m.second / duration_s_) * r.width();
            p.fillRect(QRectF(x0, r.top(), std::max(1.0, x1 - x0), r.height()), overlay);
        }
    }

    if (buckets_ > 0 && !mono_.empty()) {
        const double mid = r.center().y();
        const double scale = r.height() * 0.46;
        p.setPen(QPen(t.border, 1));
        p.drawLine(QPointF(r.left(), mid), QPointF(r.right(), mid));
        p.setPen(QPen(t.accent, 1));
        for (int b = 0; b < buckets_; b++) {
            const double x = r.left() + (static_cast<double>(b) + 0.5) / buckets_ * r.width();
            const double y0 = mid - env_max_[b] * scale;
            const double y1 = mid - env_min_[b] * scale;
            p.drawLine(QPointF(x, y0), QPointF(x, std::max(y1, y0 + 0.6)));
        }
    } else {
        p.setPen(t.text_faint);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("Nessun audio caricato"));
    }

    if (show_playhead_ && playhead_ >= 0.0 && duration_s_ > 0.0) {
        const double x = r.left() + (playhead_ / duration_s_) * r.width();
        p.setPen(QPen(t.text, 1));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }

    if (!marker_label_.isEmpty() && !markers_.isEmpty()) {
        p.setPen(t.text_dim);
        p.drawText(r.adjusted(8, 6, -8, 0), Qt::AlignTop | Qt::AlignLeft, marker_label_);
    }
}

// -------------------------------------------------------------- nav button
NavButton::NavButton(const QString& glyph, const QString& text, QWidget* parent)
    : QPushButton(parent) {
    setObjectName("Nav");
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setText(QStringLiteral("  %1   %2").arg(glyph, text));
    setMinimumHeight(36);
}

// ------------------------------------------------------------- misc helpers
QString fmt_db(double db, int decimals) {
    if (db < -900.0) return QStringLiteral("\u2014");
    return QString::number(db, 'f', decimals);
}

QString fmt_lufs(double lufs) {
    if (lufs < -900.0) return QStringLiteral("\u2014");
    return QString::number(lufs, 'f', 1);
}

QString fmt_seconds(double seconds) {
    if (seconds < 0) seconds = 0;
    const int total = static_cast<int>(seconds + 0.5);
    return QStringLiteral("%1:%2").arg(total / 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

QString fmt_ms(double ms) {
    if (ms >= 1000.0) return QString::number(ms / 1000.0, 'f', 2) + QStringLiteral(" s");
    return QString::number(ms, 'f', 0) + QStringLiteral(" ms");
}

QString fmt_bytes(qint64 bytes) {
    static const char* units[] = {"B", "kB", "MB", "GB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        u++;
    }
    return QString::number(v, 'f', u == 0 ? 0 : 1) + QStringLiteral(" ") + units[u];
}

QWidget* make_row(const QString& label, QWidget* control, const QString& hint) {
    auto* row = new QWidget();
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(12);

    auto* left = new QVBoxLayout();
    left->setSpacing(1);
    auto* l = new QLabel(label, row);
    l->setWordWrap(true);
    left->addWidget(l);
    if (!hint.isEmpty()) {
        auto* h = new QLabel(hint, row);
        h->setObjectName("Hint");
        h->setWordWrap(true);
        left->addWidget(h);
    }
    hl->addLayout(left, 1);
    hl->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
    return row;
}

QWidget* make_section(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("SectionLabel");
    return l;
}

}  // namespace gui
