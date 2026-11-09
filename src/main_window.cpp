#include "aiws/gui/main_window.hpp"
#include "aiws/gui/workspace_widget.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/resettable.hpp"

#include <cstddef>
#include <utility>
#include <exception>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>
#include <QApplication>

#include <thread>
#include <chrono>
#include <QTimer>

#include <algorithm>

namespace aiws::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle("AI Workspace");
    resize(900, 900);

    // container widget for the main window
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* mainLayout = new QVBoxLayout(central);

    // Application header
    auto* header = new QLabel("AI Workspace", central);
    header->setAlignment(Qt::AlignCenter);
    header->setStyleSheet(
        "background-color: #234567;"
        "color: white;"
        "font-size: 22px;"
        "font-weight: bold;"
        "padding: 12px;"
    );

    mainLayout->addWidget(header);

    // Create major GUI components
    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    // Workspace section
    mainLayout->addWidget(workspace_widget_, 2);

    // Search section
    mainLayout->addWidget(search_widget_, 3);

    // Bottom section
    auto* bottomLayout = new QHBoxLayout();

    bottomLayout->addWidget(term_inspector_widget_, 1);
    bottomLayout->addWidget(context_widget_, 1);

    mainLayout->addLayout(bottomLayout, 2);

    // Status / Reset / Exit section
    auto* statusLayout = new QHBoxLayout();

    status_label_ = new QLabel("Ready", central);

    reset_button_ = new QPushButton("Reset", central);
    exit_button_ = new QPushButton("Exit", central);

    reset_button_->setStyleSheet(
        "background-color: #DDAA44;"
        "color: black;"
        "padding: 8px;"
    );

    exit_button_->setStyleSheet(
        "background-color: #AA3333;"
        "color: white;"
        "padding: 8px;"
    );

    statusLayout->addWidget(status_label_, 1);
    statusLayout->addWidget(reset_button_);
    statusLayout->addWidget(exit_button_);

    mainLayout->addLayout(statusLayout);

    // Workspace signals
    connect(workspace_widget_,
            &WorkspaceWidget::add_document_requested,
            this, &MainWindow::add_document);

    connect(workspace_widget_,
            &WorkspaceWidget::build_corpus_requested,
            this, &MainWindow::build_corpus);

    connect(workspace_widget_,
            &WorkspaceWidget::document_selected,
            this, &MainWindow::document_selected);

    // Search signals
    connect(search_widget_,
            &SearchWidget::search_requested,
            this, &MainWindow::search_requested);

    connect(search_widget_,
            &SearchWidget::query_changed,
            this, &MainWindow::query_changed);

    connect(search_widget_,
            &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested);

    connect(search_widget_,
            &SearchWidget::selected_chunk_changed,
            term_inspector_widget_,
            &TermInspectorWidget::set_selected_chunk);

    // Term Inspector
    connect(term_inspector_widget_,
            &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);

    // Context
    connect(context_widget_,
            &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);

    // Reset and Exit
    connect(reset_button_, &QPushButton::clicked,
            this, &MainWindow::reset_application);

    connect(exit_button_, &QPushButton::clicked,
            this, &MainWindow::exit_application);

// Check for completed background builds
build_timer_ = new QTimer(this);
build_timer_->setInterval(20);

connect(build_timer_, &QTimer::timeout,
        this, &MainWindow::finish_build);

// Set initial button states
update_action_states();
}

// Destructor - runs when MainWindow is destroyed
MainWindow::~MainWindow() {
    if (build_timer_) {
        build_timer_->stop();
    }

    if (build_thread_.joinable()) {
        build_thread_.join();
    }
}

// Stores callback to run when exit is requested
void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    build_delay_ms_ = std::max(0, ms_per_document);
}
void MainWindow::request_shutdown() {
    if (exit_pending_) {
        return;
    }

    exit_pending_ = true;

    if (!build_running_) {
        QApplication::quit();
        return;
    }

    status_label_->setText(
        "Waiting for background build to finish before exiting..."
    );
}

// opens file, reads text, creates Document, adds to workspace,
// updates GUI, marks corpus as stale
void MainWindow::add_document() {
    if (exit_pending_) {
    return;
}
    QString path = QFileDialog::getOpenFileName(
        this,
        "Add Document",
        QString(),
        "Text Files (*.txt);;All Files (*)"
    );

    if (path.isEmpty()) {
        return;
    }

    if (!add_document_file(path)) {
        QMessageBox::warning(
            this,
            "Error",
            "Could not open the selected file."
        );
    }
}

bool MainWindow::add_document_file(const QString& path) {
    if (exit_pending_) {
        return false;
    }

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QString text = QString::fromUtf8(file.readAll());
    QFileInfo info(path);

    Document document(
        info.absoluteFilePath().toStdString(),
        info.fileName().toStdString(),
        text.toStdString()
    );

    workspace_.add_document(std::move(document));
    workspace_widget_->add_document(info.fileName());

    invalidate_corpus();

    status_label_->setText("Document added.");

    return true;
}

// processes workspace documents, 
// builds search index and marks corpus as current
void MainWindow::build_corpus() {
    if (exit_pending_ ||
    workspace_.documents().empty() ||
    build_running_) {
    return;
}

    // Snapshot the workspace on the GUI thread
    Workspace snapshot = workspace_;

    const auto version = workspace_version_;
    const int delay = build_delay_ms_;

    build_running_ = true;
    corpus_current_ = false;

    clear_processing_outputs();
    update_action_states();

    workspace_widget_->set_corpus_status("Corpus: Building...");
    status_label_->setText("Building corpus...");

    // Worker uses copies, not live GUI state
    std::packaged_task<BuildResult()> task(
        [snapshot = std::move(snapshot), version, delay]()
            mutable -> BuildResult {

            BuildResult result;
            result.version = version;

            try {
                auto core = std::make_unique<ProcessingCore>();

                core->rebuild(snapshot);

                for (std::size_t i = 0;
                     i < snapshot.documents().size(); ++i) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(delay));
                }

                result.core = std::move(core);
            }
            catch (const std::exception& e) {
                result.error = e.what();
            }
            catch (...) {
                result.error = "Unknown build error";
            }

            return result;
        }
    );

    build_future_ = task.get_future();
    build_thread_ = std::thread(std::move(task));

    build_timer_->start();
}

void MainWindow::finish_build() {
    if (!build_running_ || !build_future_.valid()) {
        return;
    }

    // Do not wait if the worker is still running
    if (build_future_.wait_for(std::chrono::milliseconds(0))
        != std::future_status::ready) {
        return;
    }

    build_timer_->stop();

    if (build_thread_.joinable()) {
        build_thread_.join();
    }

    BuildResult result = build_future_.get();

#ifdef AIWS_FAULT_LOSE_FAILURE
    if (!result.core && !exit_pending_) {
        return;
    }
#endif

build_running_ = false;

    if (exit_pending_) {
    QApplication::quit();
    return;
}

    // Reject a result built from an outdated workspace
#ifndef AIWS_FAULT_ACCEPT_STALE
    if (result.version != workspace_version_) {
        status_label_->setText(
            "Workspace changed during build. Rebuild required.");
        update_action_states();
        return;
    }
#endif

    if (!result.core) {
        invalidate_corpus();

        QMessageBox::warning(
            this,
            "Build Failed",
            QString::fromStdString(result.error)
        );

        status_label_->setText("Corpus build failed.");
        return;
    }

    // Accept the finished corpus on the GUI thread
    processing_core_ = std::move(*result.core);
    corpus_current_ = true;

    workspace_widget_->set_corpus_status(
        "Corpus: " +
        QString::number(
            static_cast<qulonglong>(
                processing_core_.chunk_count()
            )
        ) +
        " chunks"
    );

    clear_processing_outputs();
    update_action_states();

    status_label_->setText("Corpus built successfully.");
}

//fetches document from workspace and shows text in GUI
void MainWindow::document_selected(int row) {
    const auto& documents = workspace_.documents();

    if (row < 0 ||
        static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }

    const Document& document = documents[row];

    workspace_widget_->show_selected_document(
        QString::fromStdString(document.title()),
        QString::fromStdString(document.text())
    );
}

//checks if query and corpus are valid, runs search through 
// ProcessingCore, shows results
void MainWindow::search_requested(const QString& query, int k) {
    if (!corpus_current_ || query.trimmed().isEmpty()) {
        return;
    }

    try {
        auto results = processing_core_.search(
            query.toStdString(), k
        );

        std::size_t count = results.size();

        search_widget_->set_results(std::move(results));
        context_widget_->clear_context();

        status_label_->setText(
            "Search completed: " +
            QString::number(static_cast<qulonglong>(count)) +
            " results."
        );
    }
    catch (const std::exception& e) {
        QMessageBox::warning(
            this, "Search Failed", QString::fromUtf8(e.what())
        );
    }
}

//updates GUI when user edits search
void MainWindow::query_changed(const QString& query) {
    (void)query;
    update_action_states();
}

//gathers selected results, asks ProcessingCore to build
//context within token budget and displays it
void MainWindow::build_context_requested(int token_budget) {
    if (!corpus_current_ || !search_widget_->has_query()) {
        return;
    }

    try {
        auto items = processing_core_.build_context(
            search_widget_->query().toStdString(),
            search_widget_->result_count(),
            static_cast<std::size_t>(token_budget)
        );

        context_widget_->set_context(items);
        status_label_->setText("Context built successfully.");
    }
    catch (const std::exception& e) {
        QMessageBox::warning(
            this, "Context Failed", QString::fromUtf8(e.what())
        );
    }
}

//Opens original doc for a result by fetching from workspace
//and displaying it
void MainWindow::source_document_requested(
    const QString& document_id) {

    const auto& documents = workspace_.documents();

    for (std::size_t i = 0; i < documents.size(); ++i) {
        if (QString::fromStdString(documents[i].id()) == document_id) {
            int row = static_cast<int>(i);

            workspace_widget_->select_document(row);
            document_selected(row);

            status_label_->setText("Source document selected.");
            return;
        }
    }
}

//asks ProcessingCore for term stats and displays in GUI
void MainWindow::term_inspection_requested(
    const QString& term,
    const QString& chunk_id) {

    if (!corpus_current_ || term.trimmed().isEmpty()) {
        return;
    }

    try {
        std::string normalized_term =
            ProcessingCore::normalize(term.trimmed().toStdString());

        if (normalized_term.empty()) {
            return;
        }

        std::size_t df =
            processing_core_.document_frequency(normalized_term);

        bool has_chunk = !chunk_id.isEmpty();
        std::size_t tf = 0;

        if (has_chunk) {
            tf = processing_core_.term_frequency(
                normalized_term,
                chunk_id.toStdString()
            );
        }

        term_inspector_widget_->set_statistics(
            df, has_chunk, tf
        );

        status_label_->setText("Term inspection completed.");
    }
    catch (const std::exception& e) {
        QMessageBox::warning(
            this, "Inspection Failed", QString::fromUtf8(e.what())
        );
    }
}

//clears workspace, search results, context, restores GUI
void MainWindow::reset_application() {
    if (exit_pending_) {
        return;
    }
    ++workspace_version_;
    workspace_.clear();
    processing_core_ = ProcessingCore{};
    corpus_current_ = false;

    Resettable* widgets[] = {
        workspace_widget_,
        search_widget_,
        term_inspector_widget_,
        context_widget_
    };

    for (Resettable* widget : widgets) {
        widget->reset();
    }

    update_action_states();
    status_label_->setText("Application reset.");
}

//calls save exit callback if one was registered
void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

//enables/disables buttons based on current state
void MainWindow::update_action_states() {
    bool has_documents = !workspace_.documents().empty();

    bool can_process =
        corpus_current_ && search_widget_->has_query();

    workspace_widget_->set_build_corpus_enabled(
    has_documents && !build_running_);

    search_widget_->set_search_enabled(can_process);

    term_inspector_widget_->set_inspection_enabled(corpus_current_);

    context_widget_->set_build_context_enabled(can_process);
}

//marks corpus as outdated, clears old results, disables
//search until rebuilt
void MainWindow::invalidate_corpus() {
    ++workspace_version_;
    bool was_current = corpus_current_;
    corpus_current_ = false;

    workspace_widget_->set_corpus_status(
        was_current ? "Corpus: Stale (rebuild required)"
                    : "Corpus: Not built"
    );

    clear_processing_outputs();
    update_action_states();
}

//clears previous search results, context, term stats from GUI
void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();

    term_inspector_widget_->set_selected_chunk(QString());
    term_inspector_widget_->clear_statistics();

    context_widget_->clear_context();
}

}  // namespace aiws::gui
