#include "aiws/gui/context_widget.hpp"

#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QStringList>

namespace aiws::gui {

//creates context GUI, token budget slider, build button,
//context display, signal connections
ContextWidget::ContextWidget(QWidget* parent)
    : QWidget(parent) {

    auto* mainLayout = new QVBoxLayout(this);

    auto* group = new QGroupBox("Context", this);
    auto* layout = new QVBoxLayout(group);

    // Token budget controls
    auto* budgetLayout = new QHBoxLayout();

    context_budget_slider_ =
        new QSlider(Qt::Horizontal, group);

    context_budget_slider_->setRange(50, 1000);
    context_budget_slider_->setSingleStep(50);
    context_budget_slider_->setPageStep(50);
    context_budget_slider_->setTickInterval(50);
    context_budget_slider_->setValue(300);

    budget_value_label_ = new QLabel("300", group);

    budgetLayout->addWidget(
        new QLabel("Token Budget:", group));

    budgetLayout->addWidget(context_budget_slider_, 1);
    budgetLayout->addWidget(budget_value_label_);

    layout->addLayout(budgetLayout);

    // Build Context button
    build_context_button_ =
        new QPushButton("Build Context", group);

    build_context_button_->setEnabled(false);

    layout->addWidget(build_context_button_);

    // Context output
    context_text_ = new QPlainTextEdit(group);
    context_text_->setReadOnly(true);

    layout->addWidget(context_text_, 1);

    mainLayout->addWidget(group);

    // Signal/slot connections
    connect(context_budget_slider_, &QSlider::valueChanged,
            this, &ContextWidget::budget_changed);

    connect(build_context_button_, &QPushButton::clicked,
            this, &ContextWidget::request_context);
}

//returns current token budget selected by user
int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

//enables/disables build context button based on current state
void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

//formats/displays generated context items in GUI,
//with document/chunk info
void ContextWidget::set_context(
    const std::vector<ContextItem>& items) {

    QStringList sections;

    for (const auto& item : items) {
        QString section;

        section += "Document: " +
            QString::fromStdString(item.document_id) + "\n";

        section += "Chunk: " +
            QString::number(
                static_cast<qulonglong>(item.chunk_sequence)) + "\n";

        section += "Tokens: " +
            QString::number(
                static_cast<qulonglong>(item.token_count)) + "\n";

        section += "Score: " +
            QString::number(item.score) + "\n";

        section += "Truncated: " +
            QString(item.truncated ? "Yes" : "No") + "\n";

        section += "Text:\n" +
            QString::fromStdString(item.text);

        sections.append(section);
    }

    context_text_->setPlainText(
        sections.join("\n\n--------------------\n\n"));
}

//Removes Previously displayed context from GUI
void ContextWidget::clear_context() {
    context_text_->clear();
}

//restores default token budget of 300, clears context display,
//disables build button
void ContextWidget::reset() {
    context_budget_slider_->setValue(300);
    budget_value_label_->setText("300");

    clear_context();
    set_build_context_enabled(false);
}

//emits request signal to build context with current token budget
//if button enabled
void ContextWidget::request_context() {
    if (!build_context_button_->isEnabled()) {
        return;
    }

    emit build_context_requested(token_budget());
}

//Rounds slider value to nearest multiple of 50,
//updates displayed value
void ContextWidget::budget_changed(int value) {
    // Snap slider to multiples of 50.
    int snapped = ((value + 25) / 50) * 50;

    if (snapped != value) {
        context_budget_slider_->setValue(snapped);
        return;
    }

    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui