// podcast-sanity GUI - recording source abstraction
//
// Three interchangeable sources feed the recorder:
//
//   Microphone  live capture through Qt Multimedia
//   File        a WAV played back in real time (test mode, no hardware needed)
//   Corpus      the synthetic episode generator, for demos and for the tests
//
// All three deliver 16 bit interleaved PCM chunks at a steady pace, so the
// recorder logic, the level metering and the crash-safe writer are identical no
// matter where the audio came from.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QTimer;

namespace gui {

class Capture : public QObject {
    Q_OBJECT
public:
    enum class Mode { Microphone, File, Corpus };

    explicit Capture(QObject* parent = nullptr);
    ~Capture() override;

    // `source` is a device id (microphone), a WAV path (file) or a profile name
    // ("studio", "home", "rough") for the corpus.
    bool start(Mode mode, const QString& source, int sample_rate, int channels, double gain_db);
    void pause();
    void resume();
    void stop();
    bool active() const { return active_; }
    bool paused() const { return paused_; }
    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }
    double elapsed() const;
    bool using_hardware() const { return mode_ == Mode::Microphone; }
    static bool microphone_available();

signals:
    void meter(float peak_db, float rms_db, float lufs);
    void chunk(const QByteArray& pcm16);
    void stopped();
    void failed(const QString& message);

private:
    void feed_file_or_corpus();
    void on_ready_read();
    void emit_meter();
    void teardown();

    Mode mode_ = Mode::File;
    QString source_;
    int sample_rate_ = 48000;
    int channels_ = 1;
    double gain_db_ = 0.0;
    bool active_ = false;
    bool paused_ = false;

    QTimer* feeder_ = nullptr;
    std::size_t feed_pos_ = 0;
    std::vector<float> feed_mono_;  // file / corpus material, mono at sample_rate_

    std::vector<float> meter_buf_;  // rolling window for the loudness meter
    std::size_t meter_hop_ = 0;

    class Private;
    std::unique_ptr<Private> d_;
};

}  // namespace gui
