#include "aiws/gui/workspace_widget.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>

namespace aiws::gui {

//creates workspace GUI, document list, buttons,
//selected-document display, signal connections
WorkspaceWidget::WorkspaceWidget(QWidget* parent)
    : QWidget(parent) {

    auto* mainLayout = new QHBoxLayout(this);

    // Workspace area
    auto* workspaceGroup = new QGroupBox("Workspace", this);
    auto* workspaceLayout = new QVBoxLayout(workspaceGroup);

    document_list_ = new QListWidget(workspaceGroup);

    add_document_button_ =
        new QPushButton("Add Document", workspaceGroup);

    build_corpus_button_ =
        new QPushButton("Build Corpus", workspaceGroup);

    build_corpus_button_->setEnabled(false);

    corpus_status_label_ =
        new QLabel("Corpus: Not built", workspaceGroup);

    workspaceLayout->addWidget(document_list_);
    workspaceLayout->addWidget(add_document_button_);
    workspaceLayout->addWidget(build_corpus_button_);
    workspaceLayout->addWidget(corpus_status_label_);

    // Selected Document area
    auto* selectedGroup =
        new QGroupBox("Selected Document", this);

    auto* selectedLayout = new QVBoxLayout(selectedGroup);

    document_title_label_ = new QLabel("", selectedGroup);
    document_text_ = new QPlainTextEdit(selectedGroup);

    document_text_->setReadOnly(true);

    selectedLayout->addWidget(document_title_label_);
    selectedLayout->addWidget(document_text_);

    // Add both sections to the main layout
    mainLayout->addWidget(workspaceGroup, 1);
    mainLayout->addWidget(selectedGroup, 1);

    // Connect local events to component signals
    connect(add_document_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::add_document_requested);

    connect(build_corpus_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::build_corpus_requested);

    connect(document_list_, &QListWidget::currentRowChanged,
            this, &WorkspaceWidget::document_selected);
}

//adds document title to workspace list
void WorkspaceWidget::add_document(const QString& title) {
    document_list_->addItem(title);
}

//selects document in list using row number
void WorkspaceWidget::select_document(int row) {
    document_list_->setCurrentRow(row);
}

//displays selected document's title and text in GUI
void WorkspaceWidget::show_selected_document(const QString& title,
                                              const QString& text) {
    document_title_label_->setText(title);
    document_text_->setPlainText(text);
}

//clears selected-document title/text display in GUI
void WorkspaceWidget::clear_selected_document() {
    document_title_label_->clear();
    document_text_->clear();
}

//updates label showing if corpus is built, stale, not built
void WorkspaceWidget::set_corpus_status(const QString& text) {
    corpus_status_label_->setText(text);
}

//enables/disables Build Corpus button
void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

//clears document list, display, resets corpus status, 
//disables Build Corpus button
void WorkspaceWidget::reset() {
    document_list_->clear();
    clear_selected_document();
    set_corpus_status("Corpus: Not built");
    set_build_corpus_enabled(false);
}

}  // namespace aiws::gui
