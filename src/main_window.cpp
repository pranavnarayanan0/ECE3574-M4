#include "aiws/gui/main_window.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"
#include "aiws/retrieval_engine.hpp"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <utility>

namespace aiws::gui {

namespace {

class BuildCancelled final : public std::exception {};

// ProcessingCore calls this strategy once per document.
class CancellableChunker final : public ChunkingStrategy {
public:
    CancellableChunker(std::shared_ptr<std::atomic<bool>> cancelled,
                       int delay_ms)
        : cancelled_(std::move(cancelled)),
          delay_ms_(delay_ms) {}

    std::vector<Chunk> chunk(const Document& document,
                             std::size_t document_order) const override {
        check_cancelled();

        if (delay_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        }

        check_cancelled();
        auto chunks = chunker_.chunk(document, document_order);
        check_cancelled();
        return chunks;
    }

private:
    void check_cancelled() const {
        if (cancelled_->load()) {
            throw BuildCancelled{};
        }
    }

    std::shared_ptr<std::atomic<bool>> cancelled_;
    int delay_ms_;
    Chunker chunker_;
};

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("AI Workspace");
    resize(900, 900);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* mainLayout = new QVBoxLayout(central);

    auto* header = new QLabel("AI Workspace", central);
    header->setAlignment(Qt::AlignCenter);
    header->setStyleSheet(
        "background-color: #234567;"
        "color: white;"
        "font-size: 22px;"
        "font-weight: bold;"
        "padding: 12px;");
    mainLayout->addWidget(header);

    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    mainLayout->addWidget(workspace_widget_, 2);
    mainLayout->addWidget(search_widget_, 3);

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(term_inspector_widget_, 1);
    bottomLayout->addWidget(context_widget_, 1);
    mainLayout->addLayout(bottomLayout, 2);

    auto* statusLayout = new QHBoxLayout();
    status_label_ = new QLabel("Ready", central);
    reset_button_ = new QPushButton("Reset", central);
    exit_button_ = new QPushButton("Exit", central);

    reset_button_->setStyleSheet(
        "background-color: #DDAA44;"
        "color: black;"
        "padding: 8px;");
    exit_button_->setStyleSheet(
        "background-color: #AA3333;"
        "color: white;"
        "padding: 8px;");

    statusLayout->addWidget(status_label_, 1);
    statusLayout->addWidget(reset_button_);
    statusLayout->addWidget(exit_button_);
    mainLayout->addLayout(statusLayout);

    connect(workspace_widget_, &WorkspaceWidget::add_document_requested,
            this, &MainWindow::add_document);
    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested,
            this, &MainWindow::build_corpus);
    connect(workspace_widget_, &WorkspaceWidget::document_selected,
            this, &MainWindow::document_selected);

    connect(search_widget_, &SearchWidget::search_requested,
            this, &MainWindow::search_requested);
    connect(search_widget_, &SearchWidget::query_changed,
            this, &MainWindow::query_changed);
    connect(search_widget_, &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested);
    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);

    connect(term_inspector_widget_,
            &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);
    connect(context_widget_, &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);
    connect(reset_button_, &QPushButton::clicked,
            this, &MainWindow::reset_application);
    connect(exit_button_, &QPushButton::clicked,
            this, &MainWindow::exit_application);

    update_action_states();
}

MainWindow::~MainWindow() {
    stop_worker();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    build_delay_ms_ = std::max(0, ms_per_document);
}

void MainWindow::worker_loop() {
    for (;;) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(build_mutex_);
            build_ready_.wait(lock, [this] {
                return worker_stopping_ || static_cast<bool>(pending_build_);
            });

            if (worker_stopping_) {
                return;
            }

            task.swap(pending_build_);
        }

        // The mutex is released before processing the workspace.
        task();
    }
}

void MainWindow::cancel_build() {
    if (build_cancelled_) {
        build_cancelled_->store(true);
    }

    {
        std::lock_guard<std::mutex> lock(build_mutex_);
        pending_build_ = {};
    }

    build_cancelled_.reset();
}

void MainWindow::stop_worker() {
    cancel_build();

    {
        std::lock_guard<std::mutex> lock(build_mutex_);
        worker_stopping_ = true;
    }

    build_ready_.notify_one();

    if (build_thread_.joinable()) {
        build_thread_.join();
    }
}

void MainWindow::request_shutdown() {
    if (exit_pending_) {
        return;
    }

    exit_pending_ = true;
    lifecycle_.exit();
    stop_worker();
    QApplication::quit();
}

void MainWindow::add_document() {
    if (exit_pending_) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, "Add Document", QString(), "Text Files (*.txt);;All Files (*)");

    if (path.isEmpty()) {
        return;
    }

    if (!add_document_file(path)) {
        QMessageBox::warning(this, "Error",
                             "Could not open the selected file.");
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

    const QString text = QString::fromUtf8(file.readAll());
    const QFileInfo info(path);

    Document document(info.absoluteFilePath().toStdString(),
                      info.fileName().toStdString(),
                      text.toStdString());

    workspace_.add_document(std::move(document));
    workspace_widget_->add_document(info.fileName());

    // Building and Current give different explanations for the same Add.
    const QString status = QString::fromLatin1(lifecycle_.added_message());
    const QString corpus = QString::fromLatin1(lifecycle_.corpus_after_add());

    cancel_build();
    ++workspace_version_;
    lifecycle_.document_added();

    workspace_widget_->set_corpus_status(corpus);
    clear_processing_outputs();
    update_action_states();
    status_label_->setText(status);
    return true;
}

void MainWindow::build_corpus() {
    if (!lifecycle_.can_build()) {
        return;
    }

    Workspace snapshot = workspace_;
    const auto version = workspace_version_;
    const int delay = build_delay_ms_;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);

    if (!build_thread_.joinable()) {
        build_thread_ = std::thread(&MainWindow::worker_loop, this);
    }

    auto task = [this, snapshot = std::move(snapshot),
                 version, delay, cancelled]() {
        if (cancelled->load()) {
            return;
        }

        auto result = std::make_shared<BuildResult>();
        result->version = version;

        try {
            auto core = std::make_unique<ProcessingCore>(
                std::make_unique<CancellableChunker>(cancelled, delay),
                std::make_unique<RetrievalEngine>(),
                std::make_unique<ContextBuilder>());

            core->rebuild(snapshot);
            result->core = std::move(core);
        }
        catch (const BuildCancelled&) {
            return;
        }
        catch (const std::exception& e) {
            result->error = e.what();
        }
        catch (...) {
            result->error = "Unknown build error";
        }

        if (cancelled->load()) {
            return;
        }

        // This callback runs on the GUI thread. Add or Reset could have
        // invalidated this request while the callback was in Qt's queue.
        QMetaObject::invokeMethod(
            this,
            [this, result, cancelled] {
                if (build_cancelled_ == cancelled && !cancelled->load()) {
                    finish_build(*result);
                }
            },
            Qt::QueuedConnection);
    };

    build_cancelled_ = cancelled;
    lifecycle_.request_build();

    clear_processing_outputs();
    update_action_states();
    workspace_widget_->set_corpus_status("Corpus: Building...");
    status_label_->setText("Building corpus...");

    {
        std::lock_guard<std::mutex> lock(build_mutex_);
        pending_build_ = std::move(task);
    }

    build_ready_.notify_one();
}

void MainWindow::finish_build(BuildResult& result) {
    if (!lifecycle_.is_building() || result.version != workspace_version_) {
        return;
    }

    build_cancelled_.reset();

    if (!result.core) {
        lifecycle_.build_failed();
        workspace_widget_->set_corpus_status("Corpus: Not built");
        clear_processing_outputs();
        update_action_states();
        status_label_->setText("Corpus build failed.");
        QMessageBox::warning(this, "Build Failed",
                             QString::fromStdString(result.error));
        return;
    }

    processing_core_ = std::move(*result.core);
    lifecycle_.build_succeeded();

    workspace_widget_->set_corpus_status(
        "Corpus: " +
        QString::number(static_cast<qulonglong>(
            processing_core_.chunk_count())) +
        " chunks");

    clear_processing_outputs();
    update_action_states();
    status_label_->setText("Corpus built successfully.");
}

void MainWindow::document_selected(int row) {
    const auto& documents = workspace_.documents();

    if (row < 0 || static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }

    const Document& document = documents[row];
    workspace_widget_->show_selected_document(
        QString::fromStdString(document.title()),
        QString::fromStdString(document.text()));
}

void MainWindow::search_requested(const QString& query, int k) {
    if (!lifecycle_.can_search() || query.trimmed().isEmpty()) {
        return;
    }

    try {
        auto results = processing_core_.search(query.toStdString(), k);
        const auto count = results.size();
        search_widget_->set_results(std::move(results));
        context_widget_->clear_context();

        status_label_->setText(
            "Search completed: " +
            QString::number(static_cast<qulonglong>(count)) +
            " results.");
    }
    catch (const std::exception& e) {
        QMessageBox::warning(this, "Search Failed",
                             QString::fromUtf8(e.what()));
    }
}

void MainWindow::query_changed(const QString& query) {
    (void)query;
    update_action_states();
}

void MainWindow::build_context_requested(int token_budget) {
    if (!lifecycle_.can_search() || !search_widget_->has_query()) {
        return;
    }

    try {
        auto items = processing_core_.build_context(
            search_widget_->query().toStdString(),
            search_widget_->result_count(),
            static_cast<std::size_t>(token_budget));

        context_widget_->set_context(items);
        status_label_->setText("Context built successfully.");
    }
    catch (const std::exception& e) {
        QMessageBox::warning(this, "Context Failed",
                             QString::fromUtf8(e.what()));
    }
}

void MainWindow::source_document_requested(const QString& document_id) {
    const auto& documents = workspace_.documents();

    for (std::size_t i = 0; i < documents.size(); ++i) {
        if (QString::fromStdString(documents[i].id()) == document_id) {
            const int row = static_cast<int>(i);
            workspace_widget_->select_document(row);
            document_selected(row);
            status_label_->setText("Source document selected.");
            return;
        }
    }
}

void MainWindow::term_inspection_requested(const QString& term,
                                           const QString& chunk_id) {
    if (!lifecycle_.can_search() || term.trimmed().isEmpty()) {
        return;
    }

    try {
        const std::string normalized_term =
            ProcessingCore::normalize(term.trimmed().toStdString());

        if (normalized_term.empty()) {
            return;
        }

        const auto df = processing_core_.document_frequency(normalized_term);
        const bool has_chunk = !chunk_id.isEmpty();
        std::size_t tf = 0;

        if (has_chunk) {
            tf = processing_core_.term_frequency(
                normalized_term, chunk_id.toStdString());
        }

        term_inspector_widget_->set_statistics(df, has_chunk, tf);
        status_label_->setText("Term inspection completed.");
    }
    catch (const std::exception& e) {
        QMessageBox::warning(this, "Inspection Failed",
                             QString::fromUtf8(e.what()));
    }
}

void MainWindow::reset_application() {
    if (exit_pending_) {
        return;
    }

    const QString status = QString::fromLatin1(lifecycle_.reset_message());

    cancel_build();
    ++workspace_version_;
    lifecycle_.reset();

    workspace_.clear();
    processing_core_ = ProcessingCore{};

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
    status_label_->setText(status);
}

void MainWindow::exit_application() {
    if (exit_callback_) {
        exit_callback_(this);
    }
}

void MainWindow::update_action_states() {
    const bool current = lifecycle_.can_search();
    const bool can_process = current && search_widget_->has_query();

    workspace_widget_->set_build_corpus_enabled(lifecycle_.can_build());
    search_widget_->set_search_enabled(can_process);
    term_inspector_widget_->set_inspection_enabled(current);
    context_widget_->set_build_context_enabled(can_process);
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    term_inspector_widget_->set_selected_chunk(QString());
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui