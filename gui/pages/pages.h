// podcast-sanity GUI - page base class and the seven pages
#pragma once

#include <QElapsedTimer>
#include <QStringList>
#include <QWidget>

#include <vector>

#include "app_state.h"
#include "widgets.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QTableWidget;
class QThread;
class QTimer;
class QVBoxLayout;
class QScrollArea;
class QMediaPlayer;

namespace gui {

class Card;
class Chip;
class LevelMeter;
class StatTile;
class WaveformView;
class VuMeter;

// A scrolled page with a title block, a subtitle and a single column of cards.
class Page : public QWidget {
public:
    Page(const QString& title, const QString& subtitle, const QString& badge,
         QWidget* parent = nullptr);
    Card* add_card(const QString& title, const QString& subtitle = QString());
    QVBoxLayout* column() const { return column_; }
    QScrollArea* scroll() const { return scroll_; }
    void set_badge(const QString& text, State state);

private:
    QVBoxLayout* column_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    Chip* badge_ = nullptr;
};

// ------------------------------------------------------------------ record
class RecordPage : public QWidget {
    Q_OBJECT
public:
    explicit RecordPage(QWidget* parent = nullptr);

signals:
    void recording_ready();
    void recording_started();
    void recording_stopped();

private:
    void refresh_devices();
    void start_recording();
    void pause_resume();
    void stop_recording();
    void add_marker();
    void run_mic_test();
    void on_level(float peak_db, float rms_db, float lufs, float true_peak_db);
    void on_chunk(const QByteArray& pcm);
    void tick();
    void paint_idle_waveform();
    void set_state(State s, const QString& text);
    void update_recovery_status();

    Card* transport_ = nullptr;
    Card* devices_ = nullptr;
    Card* waveform_ = nullptr;
    Card* test_ = nullptr;

    QComboBox* source_ = nullptr;
    QComboBox* input_dev_ = nullptr;
    QComboBox* rate_ = nullptr;
    QComboBox* channels_ = nullptr;
    QSlider* gain_ = nullptr;
    QLabel* gain_label_ = nullptr;

    TransportButton* rec_ = nullptr;
    TransportButton* pause_ = nullptr;
    TransportButton* stop_ = nullptr;
    TransportButton* marker_ = nullptr;
    VuMeter* vu_ = nullptr;

    LevelMeter* in_meter_ = nullptr;
    LevelMeter* tp_meter_ = nullptr;
    WaveformView* wave_ = nullptr;
    QLabel* timer_ = nullptr;
    QLabel* counter_ = nullptr;
    QLabel* recovery_ = nullptr;
    QLabel* test_result_ = nullptr;
    QTimer* ticker_ = nullptr;

    std::vector<float> marker_times_;
    QStringList marker_labels_;
    bool recording_ = false;
    bool paused_ = false;
    double elapsed_ = 0.0;
    double last_mic_test_db_ = -200.0;
    double mic_test_floor_db_ = 0.0;
    bool mic_testing_ = false;
    std::vector<float> live_display_;   // rolling window shown in the waveform
    int sample_rate_of_live_ = 48000;
    int live_rate_ = 48000;      // rate the device actually delivered
    int live_channels_ = 1;      // channels the device actually delivered
    double last_paint_ = -1.0;

    class Capture* capture_ = nullptr;
};

// ------------------------------------------------------------------ master
class MasterPage : public QWidget {
    Q_OBJECT
public:
    explicit MasterPage(QWidget* parent = nullptr);
    ~MasterPage() override;
    void load_params(const sanity::Params& p);

private:
    void sync_params();
    void process();
    void show_report();
    void show_candidates();
    void build_stage_table();
    void build_waveform();
    void play(const QString& which);
    void play_file(const QString& path);
    void stop_playback();
    void preview_music();
    void refresh_music();
    void refresh_from_state();

    Card* audio_card_ = nullptr;
    Card* chain_card_ = nullptr;
    Card* filler_card_ = nullptr;
    Card* music_card_ = nullptr;
    QPushButton* music_pick_ = nullptr;
    QLabel* music_info_ = nullptr;
    QCheckBox* music_enabled_ = nullptr;
    QSlider* music_level_ = nullptr;
    QLabel* music_level_label_ = nullptr;
    QSlider* music_duck_ = nullptr;
    QLabel* music_duck_label_ = nullptr;
    QDoubleSpinBox* music_fade_in_ = nullptr;
    QDoubleSpinBox* music_fade_out_ = nullptr;
    QDoubleSpinBox* music_offset_ = nullptr;
    QDoubleSpinBox* music_tail_ = nullptr;
    QCheckBox* music_loop_ = nullptr;
    QPushButton* music_preview_ = nullptr;
    QPushButton* music_preview_stop_ = nullptr;
    QThread* music_thread_ = nullptr;
    QMediaPlayer* player_ = nullptr;
    bool loading_params_ = true;
    int preview_generation_ = 0;
    Card* result_card_ = nullptr;
    Card* stages_card_ = nullptr;

    QComboBox* preset_ = nullptr;
    QCheckBox* power_clean_ = nullptr;
    QSlider* denoise_ = nullptr;
    QLabel* denoise_label_ = nullptr;
    QCheckBox* dereverb_ = nullptr;
    QCheckBox* highpass_ = nullptr;
    QCheckBox* dehum_ = nullptr;
    QCheckBox* declick_ = nullptr;
    QCheckBox* deess_ = nullptr;
    QCheckBox* leveler_ = nullptr;
    QSlider* leveler_strength_ = nullptr;
    QLabel* leveler_strength_label_ = nullptr;
    QCheckBox* compress_ = nullptr;
    QComboBox* filler_ = nullptr;
    QCheckBox* normalize_ = nullptr;

    QPushButton* process_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* candidates_btn_ = nullptr;
    QPushButton* play_before_ = nullptr;
    QPushButton* play_after_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* elapsed_ = nullptr;
    QTimer* elapsed_timer_ = nullptr;
    QElapsedTimer since_start_;
    WaveformView* wave_ = nullptr;
    QTableWidget* stages_ = nullptr;
    QTableWidget* candidates_ = nullptr;
    QLabel* verdict_ = nullptr;
    StatTile* tile_lufs_before_ = nullptr;
    StatTile* tile_lufs_after_ = nullptr;
    StatTile* tile_tp_before_ = nullptr;
    StatTile* tile_tp_after_ = nullptr;
    StatTile* tile_fillers_ = nullptr;
    StatTile* tile_time_ = nullptr;
    bool processing_ = false;
};

// --------------------------------------------------------------- standards
class StandardsPage : public QWidget {
    Q_OBJECT
public:
    explicit StandardsPage(QWidget* parent = nullptr);
    void apply_preset_target();  // called when the preset changes elsewhere

private:
    void on_standard_changed();
    void measure_file();
    void refresh_conformance();
    sanity::Params current_params() const;

    QComboBox* standard_ = nullptr;
    QDoubleSpinBox* target_ = nullptr;
    QDoubleSpinBox* true_peak_ = nullptr;
    QDoubleSpinBox* max_lra_ = nullptr;
    QDoubleSpinBox* max_short_ = nullptr;
    QDoubleSpinBox* max_moment_ = nullptr;

    Card* conform_card_ = nullptr;
    Chip* loudness_chip_ = nullptr;
    Chip* peak_chip_ = nullptr;
    Chip* lra_chip_ = nullptr;
    StatTile* t_target_ = nullptr;
    StatTile* t_achieved_ = nullptr;
    StatTile* t_error_ = nullptr;
    StatTile* t_tp_ = nullptr;

    Card* measure_card_ = nullptr;
    QLabel* measured_ = nullptr;
    QPushButton* measure_btn_ = nullptr;
};

// ---------------------------------------------------------------- library
class LibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPage(QWidget* parent = nullptr);

private:
    void reload();
    void open_selected();
    void delete_selected();
    void reveal_selected();
    void choose_folder();
    void export_selected();
    QString selected_id() const;

    QTableWidget* table_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* folder_ = nullptr;
    QLabel* summary_ = nullptr;
    QVector<Session> sessions_;
    Page* page_ = nullptr;
};

// ----------------------------------------------------------------- export
class ExportPage : public QWidget {
    Q_OBJECT
public:
    explicit ExportPage(QWidget* parent = nullptr);

private:
    void apply_profile();
    void custom_profile();
    void refresh();
    void do_export();
    void copy_rss();
    void open_spotify();
    sanity::ExportOptions options() const;
    // True when a master exists but was rendered with other delivery settings.
    bool master_is_stale() const;

    Card* format_card_ = nullptr;
    Card* conform_card_ = nullptr;
    Card* publish_card_ = nullptr;
    QComboBox* profile_ = nullptr;
    QLabel* profile_note_ = nullptr;
    bool applying_profile_ = false;
    QComboBox* format_ = nullptr;
    QComboBox* channels_ = nullptr;
    QComboBox* rate_ = nullptr;
    QLineEdit* filename_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* target_path_ = nullptr;
    QLabel* conform_label_ = nullptr;
    Chip* conform_chip_ = nullptr;
    QPushButton* export_ = nullptr;
    QPushButton* copy_ = nullptr;
    QPushButton* spotify_ = nullptr;
    QCheckBox* also_input_ = nullptr;
    bool export_after_ = false;
};

// --------------------------------------------------------------- settings
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);

private:
    void apply_theme();
    void choose_library();
    void save_defaults();
    void reset_defaults();

    QComboBox* theme_ = nullptr;
    QComboBox* preset_ = nullptr;
    QComboBox* filler_ = nullptr;
    QCheckBox* power_clean_ = nullptr;
    QComboBox* default_format_ = nullptr;
    QLineEdit* library_ = nullptr;
    QLabel* version_ = nullptr;
};

// ------------------------------------------------------------ diagnostics
// One measured scenario for the in-app harness table.
struct ScenarioRow {
    QString profile;
    double lufs = 0.0;
    double tp = 0.0;
    double precision = 0.0;
    double recall = 0.0;
    double snr_gain = 0.0;
    double si_sdr = 0.0;
    double rtf = 0.0;
    bool conformant = false;
    bool deterministic = false;
    int fillers_found = 0;
    int fillers_true = 0;
};

class DiagnosticsPage : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPage(QWidget* parent = nullptr);
    ~DiagnosticsPage() override;

private:
    void run_harness();
    void run_analysis();
    void show_last_report();
    void save_report();
    void copy_report();
    // Runs one synthetic episode through the whole chain. Called on a worker
    // thread, so it must not touch any widget; the JSON comes back in `json`.
    ScenarioRow run_scenario(const QString& profile, double seconds, QString* json);
    void add_row(const ScenarioRow& row);
    void set_running(bool on);

    Card* harness_card_ = nullptr;
    Card* report_card_ = nullptr;
    Card* system_card_ = nullptr;
    Card* contract_card_ = nullptr;

    QTableWidget* table_ = nullptr;
    QPlainTextEdit* report_ = nullptr;
    QPushButton* run_ = nullptr;
    QPushButton* analyse_ = nullptr;
    QPushButton* copy_ = nullptr;
    QPushButton* save_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* seconds_ = nullptr;
    QThread* harness_thread_ = nullptr;
    bool running_ = false;
    QString last_json_;
};

}  // namespace gui
