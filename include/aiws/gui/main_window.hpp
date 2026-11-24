#pragma once

#include <QMainWindow>

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"
#include "aiws/gui/corpus_lifecycle.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class QLabel;
class QPushButton;
class QString;
class QWidget;

namespace aiws::gui {

class ContextWidget;
class SearchWidget;
class TermInspectorWidget;
class WorkspaceWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    using ExitCallback = void (*)(QWidget* parent);

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void set_exit_callback(ExitCallback callback) noexcept;
    void request_shutdown();
    bool add_document_file(const QString& path);
    void set_build_delay_ms(int ms_per_document);

private slots:
    void add_document();
    void build_corpus();
    void document_selected(int row);
    void search_requested(const QString& query, int k);
    void query_changed(const QString& query);
    void build_context_requested(int token_budget);
    void source_document_requested(const QString& document_id);
    void term_inspection_requested(const QString& term,
                                   const QString& chunk_id);
    void reset_application();
    void exit_application();

private:
    struct BuildResult {
        std::unique_ptr<ProcessingCore> core;
        std::string error;
        std::uint64_t version{0};
    };

    void worker_loop();
    void stop_worker();
    void cancel_build();
    void finish_build(BuildResult& result);

    void update_action_states();
    void clear_processing_outputs();

    Workspace workspace_;
    ProcessingCore processing_core_;
    CorpusLifecycle lifecycle_;
    ExitCallback exit_callback_{nullptr};

    // Both threads access the pending task and stop flag under build_mutex_.
    std::mutex build_mutex_;
    std::condition_variable build_ready_;
    std::function<void()> pending_build_;
    bool worker_stopping_{false};
    std::thread build_thread_;

    // The GUI owns this handle. Each task captures its own copy of the
    // handle so a later request cannot reset an earlier task's stop flag.
    std::shared_ptr<std::atomic<bool>> build_cancelled_;

    std::uint64_t workspace_version_{0};
    int build_delay_ms_{0};
    bool exit_pending_{false};

    WorkspaceWidget* workspace_widget_{nullptr};
    SearchWidget* search_widget_{nullptr};
    TermInspectorWidget* term_inspector_widget_{nullptr};
    ContextWidget* context_widget_{nullptr};
    QLabel* status_label_{nullptr};
    QPushButton* reset_button_{nullptr};
    QPushButton* exit_button_{nullptr};
};

}  // namespace aiws::gui