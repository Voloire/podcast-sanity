// podcast-sanity - desktop application entry point
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSettings>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>
#include <QIcon>
#if SANITY_HAVE_QTMM
#include <QAudioDevice>
#include <QMediaDevices>
#endif

#include <cstdio>

#include "app_state.h"
#include "mainwindow.h"
#include "sanity/corpus.hpp"
#include "sanity/distribution.hpp"
#include "sanity/encode.hpp"
#include "sanity/master.hpp"
#include "theme.h"
#include "widgets.h"

namespace {

// Headless self test: walks every page, runs the mastering chain through the
// same code path the buttons use, exports an MP3 and checks the result. It is
// what CI runs under xvfb, so a broken signal connection or a null widget is
// caught without a human at the keyboard.
class SelfTest {
public:
    SelfTest(gui::MainWindow* w, double seconds, const QString& shots_dir)
        : window_(w), seconds_(seconds), shots_(shots_dir) {}

    void start() {
        if (!shots_.isEmpty()) {
            gui::set_mode(gui::ThemeMode::Studio);
            gui::AppState::get().theme_changed();
        }
        // Walk the pages: constructing and painting each one exercises the
        // signal wiring.
        step(0);
        QTimer::singleShot(static_cast<int>(seconds_ * 1000) + 8000, [] {
            std::fprintf(stderr, "selftest: TIMEOUT\n");
            std::exit(2);
        });
    }

private:
    void step(int page) {
        if (page >= 7) {
            run_mastering();
            return;
        }
        window_->select_page(page);
        QTimer::singleShot(320, [this, page] {
            grab(page);
            step(page + 1);
        });
    }

    // Visual QA: with --selftest-shots the whole interface is captured page by
    // page, so a layout regression is visible without a human at the keyboard.
    void grab(int page, const QString& prefix = QStringLiteral("page")) {
        if (shots_.isEmpty()) return;
        QDir(shots_).mkpath(QStringLiteral("."));
        const QString path =
            QDir(shots_).filePath(QStringLiteral("%1-%2.png").arg(prefix).arg(page));
        if (window_->grab().save(path))
            std::printf("selftest: wrote %s\n", path.toUtf8().constData());
    }

    void run_mastering() {
        auto& st = gui::AppState::get();
        QObject::connect(&st, &gui::AppState::mastering_finished, &st,
                         [this](bool ok, const QString& err) { verify(ok, err); });
        if (!st.session().has_audio) {
            sanity::Degradations d = sanity::degradation_home();
            const sanity::CorpusItem item =
                sanity::make_corpus_talk(48000, 12.0, d, d.seed, "selftest", false);
            sanity::AudioBuffer b;
            b.sample_rate = 48000;
            b.channels = 1;
            b.data = item.degraded;
            st.set_audio(b, QString(), QStringLiteral("corpus"));
        }
        if (!st.session().has_music) {
            auto music = std::make_shared<sanity::AudioBuffer>(sanity::AudioBuffer::zeros(44100, 2, 44100 * 3));
            for (std::size_t i = 0; i < music->frames(); i++) {
                const double t = static_cast<double>(i) / 44100;
                const float v = static_cast<float>(0.2 * (std::sin(sanity::kTwoPi * 220 * t) +
                    0.6 * std::sin(sanity::kTwoPi * 330 * t) + 0.4 * std::sin(sanity::kTwoPi * 440 * t)));
                music->data[2 * i] = v;
                music->data[2 * i + 1] = v;
            }
            st.set_music_buffer(music, QStringLiteral("Accordo di prova"));
        }
        sanity::Params p = st.session().params;
        p.music_bed.enabled = true;
        p.music_bed.tail_s = 1.0;
        st.set_params(p);
        st.start_mastering(p);
    }

    void verify(bool ok, const QString& err) {
        if (!ok) {
            std::fprintf(stderr, "selftest: mastering FAILED: %s\n", err.toUtf8().constData());
            std::exit(3);
        }
        const sanity::Report& r = gui::AppState::get().session().report;
        bool bed_staged = false;
        for (const auto& stage : r.stages)
            if (stage.name == "music_bed" && stage.enabled) bed_staged = true;
        if (!bed_staged) {
            std::fprintf(stderr, "selftest: music bed stage missing\n");
            std::exit(7);
        }
        std::printf("selftest: lufs=%.2f tp=%.2f conformant=%s fillers=%zu stages=%zu rtf=%.3f\n",
                    r.after.integrated_lufs, r.after.true_peak_db, r.conformant ? "yes" : "no",
                    r.fillers_removed, r.stages.size(), r.rtf);
        if (r.stages.empty() || !r.conformant || r.after.true_peak_db > -0.95) {
            std::fprintf(stderr, "selftest: report contract FAILED\n");
            std::exit(4);
        }

        // Export through the real encoder and check the file really exists.
        sanity::Params unused = gui::AppState::get().session().params;
        sanity::ExportOptions o;
        sanity::apply_distribution_profile(sanity::recommended_distribution_profile(), unused, o);
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath(QStringLiteral("podcast-sanity-selftest") +
                                           QString::fromStdString(sanity::export_extension(o.format)));
        std::string e;
        if (sanity::encoder_available(o.format)) {
            if (!sanity::encode_audio(path.toStdString(), gui::AppState::get().session().mastered,
                                      o, e)) {
                std::fprintf(stderr, "selftest: export FAILED: %s\n", e.c_str());
                std::exit(5);
            }
            const qint64 size = QFileInfo(path).size();
            std::printf("selftest: wrote %s (%lld bytes, %s)\n", path.toUtf8().constData(),
                        static_cast<long long>(size),
                        sanity::distribution_label(sanity::recommended_distribution_profile()).c_str());
            if (size < 1024) {
                std::fprintf(stderr, "selftest: exported file looks empty\n");
                std::exit(6);
            }
        } else {
            std::printf("selftest: mp3 encoder not available, skipped export\n");
        }
        window_->select_page(1);
        QTimer::singleShot(400, [this] {
            grab(1);
            if (shots_.isEmpty()) {
                std::printf("selftest: OK\n");
                std::fflush(stdout);
                QApplication::quit();
                return;
            }
            // Capture the pages that only have content once a master exists.
            capture_after(0);
        });
    }

    // The page list is static so the deferred callbacks never hold a pointer to
    // a stack array that has already gone out of scope.
    static const int* after_pages() {
        static const int pages[] = {1, 2, 4, 6};
        return pages;
    }

    void capture_after(int i) {
        const int* pages = after_pages();
        if (i >= 4) {
            if (!shots_.isEmpty()) {
                QDir(shots_).mkpath(QStringLiteral("themes"));
                window_->select_page(0);
                for (auto mode : {gui::ThemeMode::Light, gui::ThemeMode::Dark}) {
                    gui::set_mode(mode);
                    gui::AppState::get().theme_changed();
                    QApplication::processEvents();
                    grab(0, mode == gui::ThemeMode::Light ? QStringLiteral("themes/light")
                                                        : QStringLiteral("themes/dark"));
                }
                gui::set_mode(gui::ThemeMode::Studio);
                gui::AppState::get().theme_changed();
            }
            std::printf("selftest: OK\n");
            std::fflush(stdout);
            QApplication::quit();
            return;
        }
        const int page = pages[i];
        window_->select_page(page);
        if (page == 1) {
            auto* card = window_->findChild<QWidget*>(QStringLiteral("MusicBedCard"));
            for (QWidget* parent = card ? card->parentWidget() : nullptr; parent; parent = parent->parentWidget()) {
                if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
                    scroll->ensureWidgetVisible(card);
                    break;
                }
            }
        }
        QTimer::singleShot(320, [this, i, page] {
            grab(page, QStringLiteral("mastered"));
            capture_after(i + 1);
        });
    }

    gui::MainWindow* window_;
    double seconds_ = 12.0;
    QString shots_;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("podcast-sanity"));
    QApplication::setApplicationVersion(QStringLiteral(SANITY_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Voloire"));
    QApplication::setOrganizationDomain(QStringLiteral("voloire.local"));
    QApplication::setDesktopFileName(QStringLiteral("podcast-sanity"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/podcast-sanity.ico")));

    // Fusion looks the same everywhere, which is what the QSS theme expects.
    for (const char* f : {":/fonts/Ubuntu-Regular.ttf", ":/fonts/Ubuntu-Medium.ttf",
                          ":/fonts/Ubuntu-Bold.ttf", ":/fonts/UbuntuMono-Regular.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(f));
    QApplication::setFont(QFont(gui::ui_family(), 10));
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion")))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Registrazione e mastering automatico per podcast, in locale."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selftest(QStringLiteral("selftest"),
                                QStringLiteral("Esegue il test automatico dell'interfaccia ed "
                                               "esce."));
    QCommandLineOption seconds(QStringLiteral("selftest-seconds"),
                               QStringLiteral("Durata del materiale per il test."),
                               QStringLiteral("N"), QStringLiteral("12"));
    QCommandLineOption shots(
        QStringLiteral("selftest-shots"),
        QStringLiteral("Salva una schermata di ogni pagina in questa cartella."),
        QStringLiteral("DIR"));
    parser.addOption(selftest);
    parser.addOption(seconds);
    parser.addOption(shots);
    QCommandLineOption devices(QStringLiteral("list-audio-devices"),
                               QStringLiteral("Elenca gli ingressi audio e termina."));
    parser.addOption(devices);
    parser.addPositionalArgument(QStringLiteral("audio"),
                                 QStringLiteral("File WAV da aprire all'avvio."));
    parser.process(app);
    if (parser.isSet(devices)) {
#if SANITY_HAVE_QTMM
        const auto inputs = QMediaDevices::audioInputs();
        for (const auto& device : inputs) {
            const auto format = device.preferredFormat();
            std::printf("%s%s: %d Hz, %d channels\n", device.isDefault() ? "* " : "  ",
                        device.description().toUtf8().constData(), format.sampleRate(),
                        format.channelCount());
        }
        std::fflush(stdout);
        return inputs.isEmpty() ? 1 : 0;
#else
        return 1;
#endif
    }

    QSettings st;
    const int theme = st.value("ui/theme", 2).toInt();
    gui::set_mode(theme == 0 ? gui::ThemeMode::Light : theme == 1 ? gui::ThemeMode::Dark
                                                               : gui::ThemeMode::Studio);

    gui::MainWindow window;
    app.setStyleSheet(gui::style_sheet());
    window.show();

    // Preload the recording page with test audio so the smoke test has material.
    const QStringList rest = parser.positionalArguments();
    if (!rest.isEmpty() && QFileInfo::exists(rest.first())) {
        sanity::AudioBuffer b;
        std::string err;
        if (sanity::audio_read(rest.first().toStdString(), b, err)) {
            gui::AppState::get().set_audio(b, rest.first(), QStringLiteral("file"));
            gui::AppState::get().session().name = QFileInfo(rest.first()).completeBaseName();
        }
    } else if (parser.isSet(selftest)) {
        sanity::Degradations d = sanity::degradation_home();
        const sanity::CorpusItem item =
            sanity::make_corpus_talk(48000, parser.value(seconds).toDouble(), d, d.seed, "selftest",
                                     false);
        sanity::AudioBuffer b;
        b.sample_rate = 48000;
        b.channels = 1;
        b.data = item.degraded;
        gui::AppState::get().set_audio(b, QString(), QStringLiteral("corpus"));
        gui::AppState::get().session().name = QStringLiteral("Self test");
    }

    if (parser.isSet(selftest)) {
        static SelfTest test(&window, parser.value(seconds).toDouble(), parser.value(shots));
        test.start();
    }
    return app.exec();
}
