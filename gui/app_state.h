// podcast-sanity GUI - shared application state
//
// The pages never talk to each other directly: they read and write this object
// and listen to its signals. The mastering job runs on a worker thread so the
// interface never freezes, and every long operation reports progress and a
// final quality report through here.
#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <memory>

#include "sanity/audio.hpp"
#include "sanity/encode.hpp"
#include "sanity/master.hpp"

Q_DECLARE_METATYPE(sanity::AudioBuffer)
Q_DECLARE_METATYPE(sanity::Report)
Q_DECLARE_METATYPE(sanity::Params)
Q_DECLARE_METATYPE(std::shared_ptr<const sanity::AudioBuffer>)

namespace gui {

// One episode: the audio it was made from, the settings, the result and the
// quality report. Sessions are persisted under the library folder.
struct Session {
    QString id;
    QString name;
    QString source_path;
    QString source_kind;  // "microphone", "file", "corpus"
    QString created;      // ISO date
    QString notes;

    sanity::AudioBuffer audio;  // working audio (what gets mastered)
    sanity::Params params;
    sanity::AudioBuffer mastered;
    sanity::Report report;
    QString music_path;
    std::shared_ptr<const sanity::AudioBuffer> music;
    bool has_music = false;

    bool has_audio = false;
    bool has_mastered = false;
    bool has_report = false;
    bool saved = false;

    double seconds_hint = 0.0;  // duration from session.json while the audio is not loaded
    double duration() const { return has_audio ? audio.duration() : seconds_hint; }
};

class AppState : public QObject {
    Q_OBJECT
public:
    ~AppState() override;
    static AppState& get();

    Session& session() { return session_; }
    const Session& session() const { return session_; }

    // ---- working audio -------------------------------------------------
    void set_audio(const sanity::AudioBuffer& b, const QString& path, const QString& kind);
    void clear_audio();
    void set_mastered(const sanity::AudioBuffer& b, const sanity::Report& r);
    void set_params(const sanity::Params& p);
    bool set_music(const QString& path, QString* error);
    void set_music_buffer(std::shared_ptr<const sanity::AudioBuffer> music, const QString& label);
    void clear_music();

    // ---- library -------------------------------------------------------
    QString library_dir() const;
    void set_library_dir(const QString& dir);
    QVector<Session> library_sessions() const;
    QString save_session(bool also_write_audio);
    bool load_session(const QString& id, QString* error);
    bool delete_session(const QString& id);
    // Writes one audio file into the session folder, returns the path.
    QString export_audio(const sanity::AudioBuffer& b, const sanity::ExportOptions& opt,
                         const QString& base_name, QString* error) const;

    // ---- defaults ------------------------------------------------------
    sanity::Params default_params() const;

    // ---- background mastering ------------------------------------------
    bool busy() const { return busy_; }
    void start_mastering(const sanity::Params& p);
    void cancel_mastering();

    double last_elapsed_ms() const { return last_elapsed_ms_; }

signals:
    void audio_changed();
    void mastered_changed();
    void params_changed();
    void music_changed();
    void library_changed();
    void status_message(const QString& text);
    void mastering_started();
    void mastering_finished(bool ok, const QString& error);
    void theme_changed();

private:
    explicit AppState(QObject* parent = nullptr);
    Session session_;
    QString library_dir_;
    bool busy_ = false;
    double last_elapsed_ms_ = 0.0;
    int job_id_ = 0;
    int cancelled_job_ = -1;

    class Worker;
    Worker* worker_ = nullptr;
    QThread worker_thread_;
};

// Formats an AudioBuffer's peak and loudness for display.
QString describe_audio(const sanity::AudioBuffer& b);
QString preset_name(sanity::Preset p);
QString filler_name(sanity::FillerMode m);
QString analysis_line(const sanity::Analysis& a);

}  // namespace gui
