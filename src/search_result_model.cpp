#include "aiws/gui/search_result_model.hpp"
#include <QString>
#include <utility>

namespace aiws::gui {

//Initializes table model
SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

//returns # search results, which is number of rows in table
int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(results_.size());
}

//returns # columns in table, which is fixed at 5
int SearchResultModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return 5;
}

//determines what info is displayed in each cell
//based on row, column
QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) {
        return {};
    }

    const SearchResult* result = result_at(index.row());

    if (result == nullptr) {
        return {};
    }

    switch (index.column()) {
        case 0:
            return index.row() + 1;
        case 1:
            return QString::fromStdString(result->document_id);
        case 2:
            return static_cast<qulonglong>(result->chunk_sequence);
        case 3:
            return result->score;
        case 4:
            return QString::fromStdString(result->text);
        default:
            return {};
    }
}

//defines 5 column headings
QVariant SearchResultModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const {
    if (role != Qt::DisplayRole ||
        orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
        case 0: return QString("Rank");
        case 1: return QString("Document");
        case 2: return QString("Chunk");
        case 3: return QString("Score");
        case 4: return QString("Text");
        default: return {};
    }
}

//replaces existing search result and notifies QT to refresh table
void SearchResultModel::set_results(std::vector<SearchResult> results) {
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

//removes search results and refreshes table
void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

//retreives search result for a given row, or nullptr if row invalid
const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    if (row < 0 || static_cast<std::size_t>(row) >= results_.size()) {
        return nullptr;
    }

    return &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui
