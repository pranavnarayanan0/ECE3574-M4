#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/search_result_model.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QString>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QTableView>
#include <QPlainTextEdit>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QAbstractItemView>
#include <utility>

namespace aiws::gui {

//creates search interface, layouts, buttons, text fields,
//results table, signal connections
SearchWidget::SearchWidget(QWidget* parent)
    : QWidget(parent) {

    auto* mainLayout = new QVBoxLayout(this);

    // Query area
    auto* queryGroup = new QGroupBox("Query", this);
    auto* queryLayout = new QHBoxLayout(queryGroup);

    query_edit_ = new QLineEdit(queryGroup);

    result_count_spin_ = new QSpinBox(queryGroup);
    result_count_spin_->setRange(1, 100);
    result_count_spin_->setValue(3);

    search_button_ = new QPushButton("Search", queryGroup);
    search_button_->setEnabled(false);

    queryLayout->addWidget(new QLabel("Query:", queryGroup));
    queryLayout->addWidget(query_edit_, 1);
    queryLayout->addWidget(new QLabel("Results (k):", queryGroup));
    queryLayout->addWidget(result_count_spin_);
    queryLayout->addWidget(search_button_);

    // Search Results area
    auto* resultsGroup =
        new QGroupBox("Search Results", this);

    auto* resultsLayout = new QVBoxLayout(resultsGroup);

    results_view_ = new QTableView(resultsGroup);
    result_model_ = new SearchResultModel(this);

    results_view_->setModel(result_model_);

    results_view_->setSelectionBehavior(
        QAbstractItemView::SelectRows);

    results_view_->setSelectionMode(
        QAbstractItemView::SingleSelection);

    results_view_->setEditTriggers(
        QAbstractItemView::NoEditTriggers);

    results_view_->horizontalHeader()->setStretchLastSection(true);

    resultsLayout->addWidget(results_view_);

    // Selected Result area
    auto* selectedGroup =
        new QGroupBox("Selected Result", this);

    auto* selectedLayout = new QVBoxLayout(selectedGroup);

    selected_result_text_ =
        new QPlainTextEdit(selectedGroup);

    selected_result_text_->setReadOnly(true);

    view_source_button_ =
        new QPushButton("View Source", selectedGroup);

    view_source_button_->setEnabled(false);

    selectedLayout->addWidget(selected_result_text_);
    selectedLayout->addWidget(view_source_button_);

    // Main layout
    mainLayout->addWidget(queryGroup);
    mainLayout->addWidget(resultsGroup, 2);
    mainLayout->addWidget(selectedGroup, 1);

    // Signals and slots
    connect(query_edit_, &QLineEdit::textChanged,
            this, &SearchWidget::query_changed);

    connect(search_button_, &QPushButton::clicked,
            this, &SearchWidget::request_search);

    connect(view_source_button_, &QPushButton::clicked,
            this, &SearchWidget::request_source_document);

    connect(results_view_->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &SearchWidget::show_selected_result);
}

//returns text entered in search box
QString SearchWidget::query() const {
    return query_edit_->text();
}

//returns maximum number of results requested by user
int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

//checks if search box contains non-whitespace text
bool SearchWidget::has_query() const {
    return !query_edit_->text().trimmed().isEmpty();
}

//enables/disables Search button
void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

//loads new search result into table, clears selected-result display
void SearchWidget::set_results(std::vector<SearchResult> results) {
    results_view_->clearSelection();
    result_model_->set_results(std::move(results));

    selected_result_text_->clear();
    view_source_button_->setEnabled(false);

    emit selected_chunk_changed(QString());
}

//removes existing result, clears existing result display
void SearchWidget::clear_results() {
    results_view_->clearSelection();
    result_model_->clear();

    selected_result_text_->clear();
    view_source_button_->setEnabled(false);

    emit selected_chunk_changed(QString());
}

//resetores search widget to initial state
void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(3);

    clear_results();
    set_search_enabled(false);
}

//checks if search is allowed, emits signal to request search
void SearchWidget::request_search() {
    if (!search_button_->isEnabled() || !has_query()) {
        return;
    }

    emit search_requested(query(), result_count());
}

//displays details of selected result,
//notifies other components of selected chunk change
void SearchWidget::show_selected_result() {
    const QModelIndexList rows =
        results_view_->selectionModel()->selectedRows();

    if (rows.isEmpty()) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    const SearchResult* result =
        result_model_->result_at(rows.first().row());

    if (result == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    QString details =
        "Document: " + QString::fromStdString(result->document_id) +
        "\nChunk: " + QString::number(result->chunk_sequence) +
        "\nScore: " + QString::number(result->score) +
        "\nMatched Terms: " + QString::number(result->matched_terms) +
        "\n\nText:\n" + QString::fromStdString(result->text);

    selected_result_text_->setPlainText(details);
    view_source_button_->setEnabled(true);

    emit selected_chunk_changed(
        QString::fromStdString(result->chunk_id));
}

//Emits signal requesting original document
//associated with selected result
void SearchWidget::request_source_document() {
    const QModelIndexList rows =
        results_view_->selectionModel()->selectedRows();

    if (rows.isEmpty()) {
        return;
    }

    const SearchResult* result =
        result_model_->result_at(rows.first().row());

    if (result == nullptr) {
        return;
    }

    emit source_document_requested(
        QString::fromStdString(result->document_id));
}

}  // namespace aiws::gui
