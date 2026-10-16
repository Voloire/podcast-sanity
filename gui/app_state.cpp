#include "app_state.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <exception>

#include "sanity/loudness.hpp"
#include "theme.h"
#include "widgets.h"

namespace gui {

namespace {

constexpr const char* kInputName = "input.wav";
constexpr const char* kMasterName = "master.wav";
constexpr const char* kReportName = "report.json";
constexpr const char* kSessionName = "session.json";

QString slugify(const QString& s) {
    QString out;
    for (const QChar& c : s) {
        if (c.isLetterOrNumber()) out += c.toLower();
        else if (c == ' ' || c == '-' || c == '_') out += '-';
        if (out.size() >= 40) break;
    }
    while (out.startsWith('-')) out.remove(0, 1);
    while (out.endsWith('-')) out.chop(1);
    return out.isEmpty() ? QStringLiteral("episodio") : out;
}

QString now_iso() { return QDateTime::currentDateTime().toString(Qt::ISODate); }

}  // namespace

QString preset_name(sanity::Preset p) {
    return p == sanity::Preset::Music ? QStringLiteral("Musica") : QStringLiteral("Voce");
}

QString filler_name(sanity::FillerMode m) {
    switch (m) {
        case sanity::FillerMode::Off: return QStringLiteral("Disattivato");
        case sanity::FillerMode::Cautious: return QStringLiteral("Prudente");
        case sanity::FillerMode::Balanced: return QStringLiteral("Bilanciato");
        case sanity::FillerMode::Aggressive: return QStringLiteral("Aggressivo");
    }
    return QStringLiteral("Disattivato");
}

QString describe_audio(const sanity::AudioBuffer& b) {
    if (b.data.empty()) return QStringLiteral("nessun audio");
    const std::vector<float> mono = sanity::to_mono(b);
    const double peak = sanity::peak_abs(mono);
    const double rms = sanity::rms(mono);
    return QStringLiteral("%1 Hz, %2 canale/i, %3, picco %4 dB, RMS %5 dB")
        .arg(b.sample_rate)
        .arg(b.channels)
        .arg(fmt_seconds(b.duration()))
        .arg(fmt_db(sanity::gain_to_db(peak)))
        .arg(fmt_db(sanity::gain_to_db(rms)));
}

QString analysis_line(const sanity::Analysis& a) {
    return QStringLiteral("%1 LUFS, TP %2 dBTP, LRA %3 LU")
        .arg(fmt_lufs(a.integrated_lufs))
        .arg(fmt_db(a.true_peak_db))
        .arg(fmt_db(a.lra, 1));
}

// ------------------------------------------------------------------- worker
class AppState::Worker : public QObject {
    Q_OBJECT
public slots:
    void run_job(sanity::AudioBuffer in, sanity::Params p, int job) {
        try {
            sanity::AudioBuffer out;
            const sanity::Report rep = sanity::master(in, p, out);
            emit finished(true, rep, out, QString(), job);
        } catch (const std::exception& e) {
            emit finished(false, sanity::Report{}, sanity::AudioBuffer{},
                          QStringLiteral("Errore interno durante l'elaborazione: %1")
                              .arg(QString::fromUtf8(e.what())),
                          job);
        } catch (...) {
            emit finished(false, sanity::Report{}, sanity::AudioBuffer{},
                          QStringLiteral("Errore interno durante l'elaborazione"), job);
        }
    }

signals:
    void finished(bool ok, sanity::Report rep, sanity::AudioBuffer out, QString error, int job);
};

// ------------------------------------------------------------------ AppState
AppState& AppState::get() {
    static AppState s;
    return s;
}

AppState::AppState(QObject* parent) : QObject(parent) {
    qRegisterMetaType<sanity::AudioBuffer>("sanity::AudioBuffer");
    qRegisterMetaType<sanity::Report>("sanity::Report");
    qRegisterMetaType<sanity::Params>("sanity::Params");

    worker_ = new Worker();
    worker_->moveToThread(&worker_thread_);
    connect(&worker_thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &Worker::finished, this, [this](bool ok, sanity::Report rep,
                                                     sanity::AudioBuffer out, QString error,
                                                     int job) {
        if (job == cancelled_job_) {
            emit status_message(QStringLiteral("Elaborazione annullata"));
            return;
        }
        busy_ = false;
        if (ok) {
            set_mastered(out, rep);
            emit mastering_finished(true, QString());
        } else {
            emit mastering_finished(false, error);
        }
    });
    worker_thread_.start();
}

AppState::~AppState() {
    // Stop the mastering worker before the application tears down, otherwise Qt
    // destroys a running QThread.
    worker_thread_.quit();
    worker_thread_.wait(3000);
}

QString AppState::library_dir() const {
    QSettings st;
    QString dir = st.value("library/dir").toString();
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/PodcastSanity");
    }
    return dir;
}

void AppState::set_library_dir(const QString& dir) {
    QSettings st;
    st.setValue("library/dir", dir);
    emit library_changed();
}

void AppState::set_audio(const sanity::AudioBuffer& b, const QString& path, const QString& kind) {
    session_.audio = b;
    session_.has_audio = !b.data.empty();
    session_.source_path = path;
    session_.source_kind = kind;
    session_.has_mastered = false;
    session_.has_report = false;
    session_.mastered = {};
    session_.report = {};
    if (session_.name.isEmpty()) {
        session_.name = path.isEmpty() ? QStringLiteral("Nuova registrazione")
                                       : QFileInfo(path).completeBaseName();
    }
    session_.created = now_iso();
    emit audio_changed();
    emit status_message(QStringLiteral("Audio caricato: %1").arg(describe_audio(b)));
}

void AppState::clear_audio() {
    session_.audio = {};
    session_.has_audio = false;
    session_.has_mastered = false;
    session_.has_report = false;
    emit audio_changed();
}

void AppState::set_mastered(const sanity::AudioBuffer& b, const sanity::Report& r) {
    session_.mastered = b;
    session_.has_mastered = !b.data.empty();
    session_.report = r;
    session_.has_report = true;
    last_elapsed_ms_ = r.total_ms;
    emit mastered_changed();
    emit status_message(
        QStringLiteral("Mastering completato: %1 LUFS, TP %2 dBTP, %3")
            .arg(fmt_lufs(r.after.integrated_lufs))
            .arg(fmt_db(r.after.true_peak_db))
            .arg(r.conformant ? QStringLiteral("conforme") : QStringLiteral("non conforme")));
}

void AppState::set_params(const sanity::Params& p) {
    session_.params = p;
    emit params_changed();
}

sanity::Params AppState::default_params() const {
    QSettings st;
    sanity::Params p;
    p.preset = st.value("master/preset", 0).toInt() == 1 ? sanity::Preset::Music
                                                         : sanity::Preset::Talk;
    p.power_clean = st.value("master/power_clean", true).toBool();
    p.dereverb = p.power_clean;
    p.filler = static_cast<sanity::FillerMode>(st.value("master/filler", 1).toInt());
    p.adaptive_leveler = st.value("master/leveler", true).toBool();
    p.compress = st.value("master/compress", true).toBool();
    p.dehum = st.value("master/dehum", true).toBool();
    p.declick = st.value("master/declick", true).toBool();
    p.deess = st.value("master/deess", true).toBool();
    p.adaptive_highpass = st.value("master/highpass", true).toBool();
    p.normalize = st.value("master/normalize", true).toBool();
    p.target_lufs = st.value("master/target_lufs", 0.0).toDouble();
    p.target_true_peak_db = st.value("master/true_peak", -1.0).toDouble();
    return p;
}

void AppState::start_mastering(const sanity::Params& p) {
    if (busy_) return;
    if (!session_.has_audio) {
        emit mastering_finished(false, QStringLiteral("Nessun audio da elaborare"));
        return;
    }
    busy_ = true;
    job_id_++;
    session_.params = p;
    emit mastering_started();
    emit status_message(QStringLiteral("Elaborazione in corso..."));
    QMetaObject::invokeMethod(worker_, "run_job", Qt::QueuedConnection,
                              Q_ARG(sanity::AudioBuffer, session_.audio),
                              Q_ARG(sanity::Params, p),
                              Q_ARG(int, job_id_));
}

void AppState::cancel_mastering() {
    // The pipeline is not interruptible mid stage; cancellation only prevents
    // the result from being adopted.
    cancelled_job_ = job_id_;
    busy_ = false;
    emit mastering_finished(false, QStringLiteral("Annullato"));
}

// ------------------------------------------------------------------- library
QVector<Session> AppState::library_sessions() const {
    QVector<Session> out;
    QDir root(library_dir());
    if (!root.exists()) return out;
    const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& d : dirs) {
        QFile f(root.filePath(d + "/" + kSessionName));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        Session s;
        s.id = d;
        s.name = o.value("name").toString(d);
        s.source_path = o.value("source_path").toString();
        s.source_kind = o.value("source_kind").toString();
        s.created = o.value("created").toString();
        s.notes = o.value("notes").toString();
        s.saved = true;
        const QJsonObject mix = o.value("master").toObject();
        if (!mix.isEmpty()) {
            s.has_report = true;
            s.has_mastered = true;
            s.report.after.integrated_lufs = mix.value("integrated_lufs").toDouble(-1e30);
            s.report.after.true_peak_db = mix.value("true_peak_db").toDouble(-1e30);
            s.report.after.lra = mix.value("lra").toDouble(0.0);
            s.report.conformant = mix.value("conformant").toBool();
            s.report.filler_seconds_removed = mix.value("filler_seconds").toDouble();
            s.report.fillers_removed =
                static_cast<std::size_t>(mix.value("fillers").toInt());
            s.report.total_ms = mix.value("total_ms").toDouble();
            s.report.rtf = mix.value("rtf").toDouble();
        }
        const QJsonObject in = o.value("input").toObject();
        s.audio.sample_rate = in.value("sample_rate").toInt(48000);
        s.audio.channels = in.value("channels").toInt(1);
        s.seconds_hint = in.value("seconds").toDouble(0.0);
        s.has_audio = false;  // the samples stay on disk until load_session()
        out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const Session& a, const Session& b) {
        return a.created > b.created;
    });
    return out;
}

QString AppState::save_session(bool also_write_audio) {
    QDir root(library_dir());
    if (!root.exists()) root.mkpath(".");
    if (session_.id.isEmpty()) {
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        session_.id = slugify(session_.name) + "-" + stamp;
    }
    QDir dir(root.filePath(session_.id));
    if (!dir.exists()) dir.mkpath(".");

    if (also_write_audio && session_.has_audio) {
        std::string err;
        sanity::wav_write(dir.filePath(kInputName).toStdString(), session_.audio, true, &err);
        if (session_.has_mastered)
            sanity::wav_write(dir.filePath(kMasterName).toStdString(), session_.mastered, true,
                              &err);
    }

    QJsonObject o;
    o.insert("name", session_.name);
    o.insert("source_path", session_.source_path);
    o.insert("source_kind", session_.source_kind);
    o.insert("created", session_.created.isEmpty() ? now_iso() : session_.created);
    o.insert("notes", session_.notes);
    o.insert("app_version", QStringLiteral(SANITY_VERSION));
    QJsonObject in;
    in.insert("sample_rate", session_.audio.sample_rate);
    in.insert("channels", session_.audio.channels);
    in.insert("seconds", session_.duration());
    o.insert("input", in);
    if (session_.has_report) {
        QJsonObject m;
        m.insert("integrated_lufs", session_.report.after.integrated_lufs);
        m.insert("true_peak_db", session_.report.after.true_peak_db);
        m.insert("lra", session_.report.after.lra);
        m.insert("conformant", session_.report.conformant);
        m.insert("filler_seconds", session_.report.filler_seconds_removed);
        m.insert("fillers", static_cast<int>(session_.report.fillers_removed));
        m.insert("total_ms", session_.report.total_ms);
        m.insert("rtf", session_.report.rtf);
        m.insert("params_hash", QString::fromStdString(session_.report.params_hash));
        o.insert("master", m);
    }
    QFile f(dir.filePath(kSessionName));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.close();
    }
    if (session_.has_report) {
        QFile rf(dir.filePath(kReportName));
        if (rf.open(QIODevice::WriteOnly)) {
            const std::string j = session_.report.to_json();
            rf.write(j.data(), static_cast<qint64>(j.size()));
            rf.close();
        }
    }
    session_.saved = true;
    emit library_changed();
    return dir.absolutePath();
}

bool AppState::load_session(const QString& id, QString* error) {
    QDir root(library_dir());
    QDir dir(root.filePath(id));
    if (!dir.exists()) {
        if (error) *error = QStringLiteral("Sessione non trovata");
        return false;
    }
    Session s;
    s.id = id;
    QFile sf(dir.filePath(kSessionName));
    if (sf.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(sf.readAll()).object();
        s.name = o.value("name").toString(id);
        s.source_path = o.value("source_path").toString();
        s.source_kind = o.value("source_kind").toString();
        s.created = o.value("created").toString();
        s.notes = o.value("notes").toString();
    } else {
        s.name = id;
    }
    sanity::AudioBuffer in;
    std::string err;
    if (sanity::wav_read(dir.filePath(kInputName).toStdString(), in, err)) {
        s.audio = in;
        s.has_audio = true;
    }
    sanity::AudioBuffer mst;
    std::string err2;
    if (sanity::wav_read(dir.filePath(kMasterName).toStdString(), mst, err2)) {
        s.mastered = mst;
        s.has_mastered = true;
    }
    s.params = default_params();
    s.has_report = false;
    if (s.has_mastered) {
        // The engine is the source of truth: the numeric report is re-derived
        // from the mastered audio; report.json on disk is for humans and tools.
        s.report = {};
        s.report.version = SANITY_VERSION;
        s.report.params = s.params;
        s.report.before = sanity::analyze(s.audio);
        s.report.after = sanity::analyze(s.mastered);
        const double target = s.params.target_lufs != 0.0
                                  ? s.params.target_lufs
                                  : (s.mastered.channels > 1 ? -16.0 : -19.0);
        s.report.conformant_loudness = std::fabs(s.report.after.integrated_lufs - target) <= 1.0;
        s.report.conformant_true_peak =
            s.report.after.true_peak_db <= s.params.target_true_peak_db + 0.05;
        s.report.conformant = s.report.conformant_loudness && s.report.conformant_true_peak;
        s.has_report = true;
    }
    session_ = s;
    emit audio_changed();
    emit mastered_changed();
    emit status_message(QStringLiteral("Sessione \"%1\" caricata").arg(s.name));
    return true;
}

bool AppState::delete_session(const QString& id) {
    QDir dir(QDir(library_dir()).filePath(id));
    if (!dir.exists()) return false;
    const bool ok = dir.removeRecursively();
    emit library_changed();
    return ok;
}

QString AppState::export_audio(const sanity::AudioBuffer& b, const sanity::ExportOptions& opt,
                               const QString& base_name, QString* error) const {
    QDir root(library_dir());
    const QString name = base_name.isEmpty() ? (session_.id.isEmpty() ? QStringLiteral("episodio")
                                                                      : session_.id)
                                            : base_name;
    QDir dir(root.filePath(session_.id.isEmpty() ? QStringLiteral("export") : session_.id));
    if (!dir.exists()) dir.mkpath(".");
    const QString path =
        dir.filePath(name + QString::fromStdString(sanity::export_extension(opt.format)));
    std::string err;
    if (!sanity::encode_audio(path.toStdString(), b, opt, err)) {
        if (error) *error = QString::fromStdString(err);
        return QString();
    }
    return path;
}

}  // namespace gui

#include "app_state.moc"
