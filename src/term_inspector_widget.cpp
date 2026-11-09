#include "aiws/gui/term_inspector_widget.hpp"

#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace aiws::gui {

//creates term input, inspect button, statistic label,
//signal connections
TermInspectorWidget::TermInspectorWidget(QWidget* parent)
    : QWidget(parent) {

    auto* mainLayout = new QVBoxLayout(this);

    auto* group = new QGroupBox("Term Inspector", this);
    auto* layout = new QVBoxLayout(group);

    // Term input
    auto* inputLayout = new QHBoxLayout();

    term_edit_ = new QLineEdit(group);
    inspect_button_ = new QPushButton("Inspect", group);
    inspect_button_->setEnabled(false);

    inputLayout->addWidget(new QLabel("Term:", group));
    inputLayout->addWidget(term_edit_, 1);
    inputLayout->addWidget(inspect_button_);

    layout->addLayout(inputLayout);

    // Statistics
    document_frequency_value_ = new QLabel(group);
    chunk_frequency_value_ = new QLabel(group);

    layout->addWidget(new QLabel("Document Frequency (DF):", group));
    layout->addWidget(document_frequency_value_);

    layout->addWidget(new QLabel("Selected Chunk TF:", group));
    layout->addWidget(chunk_frequency_value_);

    mainLayout->addWidget(group);

    connect(inspect_button_, &QPushButton::clicked,
            this, &TermInspectorWidget::request_inspection);
}

//enables/disables inspect button
void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

//Stores the currently selected chunk ID and clears the previous
//chunk TF when the selection changes.
void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    if (selected_chunk_id_ != chunk_id) {
        selected_chunk_id_ = chunk_id;
        chunk_frequency_value_->clear();
    }
}

//displays the calculated DF and, when available, TF values
void TermInspectorWidget::set_statistics(
    std::size_t document_frequency,
    bool has_chunk_frequency,
    std::size_t chunk_frequency) {

    document_frequency_value_->setText(
        QString::number(static_cast<qulonglong>(document_frequency)));

    if (has_chunk_frequency) {
        chunk_frequency_value_->setText(
            QString::number(static_cast<qulonglong>(chunk_frequency)));
    } else {
        chunk_frequency_value_->clear();
    }
}

//clears the displayed DF and TF values
void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->clear();
    chunk_frequency_value_->clear();
}

//clears the term, selected chunk,
//and statistics, then disables Inspect
void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

//validates the term and emits a signal requesting term inspection
void TermInspectorWidget::request_inspection() {
    if (!inspect_button_->isEnabled()) {
        return;
    }

    QString term = term_edit_->text().trimmed();

    if (term.isEmpty()) {
        return;
    }

    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui