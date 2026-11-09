
#include "aiws/workspace.hpp"

#include <utility>

namespace aiws {

// Add a document to the workspace.
// Move the document into the vector to avoid an unnecessary copy.
void Workspace::add_document(Document document) {
    documents_.push_back(std::move(document));
}

// Provide read-only access to the stored documents.
// Returning a const reference avoids copying the entire vector.
const std::vector<Document>& Workspace::documents() const noexcept {
    return documents_;
}

// Remove all documents from the workspace, such as during an application reset.
void Workspace::clear() noexcept {
    documents_.clear();
}

}  // namespace aiws
