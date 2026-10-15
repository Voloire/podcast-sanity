#include "audio_capture.h"

#include <QElapsedTimer>
#include <QIODevice>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "sanity/audio.hpp"
#include "sanity/corpus.hpp"
#include "sanity/loudness.hpp"

#if SANITY_HAVE_QTMM
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QMediaDevices>
#endif

namespace gui {

namespace {

constexpr int kMeterWindowMs = 1500;
constexpr int kFeederIntervalMs = 20;

float peak_db_of(const std::vector<float>& x) {
    double p = 0.0;
    for (float v : x) p = std::max(p, std::fabs(static_cast<double>(v)));
    return static_cast<float>(sanity::gain_to_db(p));
}

float rms_db_of(const std::vector<float>& x) {
    return static_cast<float>(sanity::gain_to_db(sanity::rms(x)));
}

// float in [-1,1] to signed 16 bit little endian, with a hard clip guard.
void float_to_pcm16(const std::vector<float>& in, QByteArray& out) {
    out.resize(static_cast<int>(in.size() * sizeof(qint16)));
    auto* p = reinterpret_cast<qint16*>(out.data());
    for (std::size_t i = 0; i < in.size(); i++) {
        double v = in[i];
        v = std::clamp(v, -1.0, 1.0);
        p[i] = static_cast<qint16>(std::lround(v * 32767.0));
    }
}

}  // namespace

// -------------------------------------------------------------------- private
class Capture::Private {
public:
#if SANITY_HAVE_QTMM
    QAudioSource* source = nullptr;
    QIODevice* device = nullptr;
#endif
    QElapsedTimer clock;
    qint64 paused_ms = 0;
    qint64 pause_started = 0;
};

Capture::Capture(QObject* parent) : QObject(parent), d_(new Private) {}

Capture::~Capture() { teardown(); }

bool Capture::microphone_available() {
#if SANITY_HAVE_QTMM
    return !QMediaDevices::audioInputs().isEmpty();
#else
    return false;
#endif
}

bool Capture::start(Mode mode, const QString& source, int sample_rate, int channels, double gain_db) {
    teardown();
    mode_ = mode;
    source_ = source;
    sample_rate_ = sample_rate > 0 ? sample_rate : 48000;
    channels_ = std::clamp(channels, 1, 2);
    gain_db_ = gain_db;
    feed_pos_ = 0;
    feed_mono_.clear();
    meter_buf_.clear();
    meter_hop_ = static_cast<std::size_t>(sample_rate_ * kMeterWindowMs / 1000 / 8);
    d_->paused_ms = 0;

    if (mode_ == Mode::Microphone) {
#if SANITY_HAVE_QTMM
        QAudioFormat fmt;
        fmt.setSampleRate(sample_rate_);
        fmt.setChannelCount(channels_);
        fmt.setSampleFormat(QAudioFormat::Int16);
        QAudioDevice dev;
        for (const QAudioDevice& d : QMediaDevices::audioInputs())
            if (QString::fromUtf8(d.id()) == source_) dev = d;
        if (dev.isNull()) {
            const auto inputs = QMediaDevices::audioInputs();
            if (inputs.isEmpty()) {
                emit failed(QStringLiteral("Nessun dispositivo di ingresso disponibile"));
                return false;
            }
            dev = inputs.first();
        }
        if (!dev.isFormatSupported(fmt)) {
            fmt = dev.preferredFormat();
            fmt.setSampleFormat(QAudioFormat::Int16);
            sample_rate_ = fmt.sampleRate();
            channels_ = fmt.channelCount();
        }
        d_->source = new QAudioSource(dev, fmt, this);
        d_->device = d_->source->start();
        if (!d_->device) {
            emit failed(QStringLiteral("Impossibile avviare la cattura dal microfono"));
            teardown();
            return false;
        }
        connect(d_->device, &QIODevice::readyRead, this, &Capture::on_ready_read);
        d_->clock.start();
        active_ = true;
        paused_ = false;
        return true;
#else
        emit failed(QStringLiteral("Questa build non include il supporto microfono "
                                   "(Qt Multimedia assente). Usa la modalita test da file."));
        return false;
#endif
    }

    // File and corpus modes synthesise or stream mono material and then feed it
    // in real time, so the recorder behaves exactly as it does with a mic.
    if (mode_ == Mode::File) {
        sanity::AudioBuffer b;
        std::string err;
        if (!sanity::wav_read(source.toStdString(), b, err)) {
            emit failed(QStringLiteral("Impossibile leggere %1: %2")
                            .arg(source, QString::fromStdString(err)));
            return false;
        }
        feed_mono_ = sanity::to_mono(b);
        sample_rate_ = b.sample_rate;
    } else {
        sanity::Degradations dg = sanity::degradation_home();
        if (source == QLatin1String("studio")) dg = sanity::degradation_studio();
        else if (source == QLatin1String("rough")) dg = sanity::degradation_rough();
        const sanity::CorpusItem item =
            sanity::make_corpus_talk(sample_rate_, 180.0, dg, dg.seed, source.toStdString(), false);
        feed_mono_ = item.degraded;
    }

    feeder_ = new QTimer(this);
    feeder_->setTimerType(Qt::PreciseTimer);
    feeder_->setInterval(kFeederIntervalMs);
    connect(feeder_, &QTimer::timeout, this, &Capture::feed_file_or_corpus);
    feeder_->start();
    d_->clock.start();
    active_ = true;
    paused_ = false;
    return true;
}

void Capture::feed_file_or_corpus() {
    if (!active_ || paused_) return;
    const std::size_t want =
        static_cast<std::size_t>(sample_rate_ * kFeederIntervalMs / 1000.0);
    std::vector<float> block(want * static_cast<std::size_t>(channels_), 0.0f);
    const double g = sanity::db_to_gain(gain_db_);
    for (std::size_t i = 0; i < want; i++) {
        const float s = feed_pos_ < feed_mono_.size()
                            ? static_cast<float>(feed_mono_[feed_pos_] * g)
                            : 0.0f;
        for (int c = 0; c < channels_; c++) block[i * channels_ + c] = s;
        feed_pos_++;
    }
    // Loop the test material so a long recording session never runs dry.
    if (feed_pos_ >= feed_mono_.size()) feed_pos_ = 0;

    QByteArray pcm;
    float_to_pcm16(block, pcm);
    emit chunk(pcm);

    std::vector<float> mono(want);
    for (std::size_t i = 0; i < want; i++) mono[i] = block[i * channels_];
    meter_buf_.insert(meter_buf_.end(), mono.begin(), mono.end());
    if (meter_buf_.size() > static_cast<std::size_t>(sample_rate_ * kMeterWindowMs / 1000))
        meter_buf_.erase(meter_buf_.begin(),
                         meter_buf_.end() -
                             static_cast<std::size_t>(sample_rate_ * kMeterWindowMs / 1000));
    emit_meter();
}

void Capture::on_ready_read() {
#if SANITY_HAVE_QTMM
    if (!d_->device) return;
    const QByteArray pcm = d_->device->readAll();
    if (pcm.isEmpty()) return;
    if (gain_db_ != 0.0) {
        // Apply the input gain sample by sample; Int16 little endian.
        QByteArray out = pcm;
        auto* p = reinterpret_cast<qint16*>(out.data());
        const double g = sanity::db_to_gain(gain_db_);
        const int n = out.size() / 2;
        for (int i = 0; i < n; i++) {
            const double v = std::clamp(p[i] * g / 32767.0, -1.0, 1.0);
            p[i] = static_cast<qint16>(std::lround(v * 32767.0));
        }
        emit chunk(out);
    } else {
        emit chunk(pcm);
    }

    // Metering on the fly.
    const int n = pcm.size() / 2;
    const qint16* p = reinterpret_cast<const qint16*>(pcm.constData());
    std::vector<float> mono;
    mono.reserve(static_cast<std::size_t>(n / channels_ + 1));
    for (int i = 0; i + channels_ <= n; i += channels_) {
        mono.push_back(static_cast<float>(p[i] / 32768.0));
    }
    meter_buf_.insert(meter_buf_.end(), mono.begin(), mono.end());
    const std::size_t cap = static_cast<std::size_t>(sample_rate_ * kMeterWindowMs / 1000);
    if (meter_buf_.size() > cap)
        meter_buf_.erase(meter_buf_.begin(), meter_buf_.end() - cap);
    emit_meter();
#endif
}

void Capture::emit_meter() {
    if (meter_buf_.empty()) return;
    const float peak = peak_db_of(meter_buf_);
    const float rms = rms_db_of(meter_buf_);
    double lufs = -1e30;
    if (meter_buf_.size() > static_cast<std::size_t>(sample_rate_ / 4))
        lufs = sanity::integrated_lufs(meter_buf_, sample_rate_);
    emit meter(peak, rms, static_cast<float>(lufs));
}

void Capture::pause() {
    if (!active_ || paused_) return;
    paused_ = true;
    d_->pause_started = d_->clock.elapsed();
#if SANITY_HAVE_QTMM
    if (d_->source) d_->source->suspend();
#endif
}

void Capture::resume() {
    if (!active_ || !paused_) return;
    paused_ = false;
    d_->paused_ms += d_->clock.elapsed() - d_->pause_started;
#if SANITY_HAVE_QTMM
    if (d_->source) d_->source->resume();
#endif
}

void Capture::stop() {
    if (!active_) return;
    const bool hardware = mode_ == Mode::Microphone;
    teardown();
    active_ = false;
    paused_ = false;
    emit stopped();
    (void)hardware;
}

double Capture::elapsed() const {
    if (!active_) return 0.0;
    qint64 ms = d_->clock.elapsed();
    if (paused_) ms = d_->pause_started;
    ms -= d_->paused_ms;
    return ms > 0 ? ms / 1000.0 : 0.0;
}

void Capture::teardown() {
    if (feeder_) {
        feeder_->stop();
        feeder_->deleteLater();
        feeder_ = nullptr;
    }
#if SANITY_HAVE_QTMM
    if (d_->source) {
        d_->source->stop();
        d_->source->deleteLater();
        d_->source = nullptr;
    }
    d_->device = nullptr;
#endif
}

}  // namespace gui
