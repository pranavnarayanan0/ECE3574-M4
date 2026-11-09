
#include "aiws/document.hpp"

#include <utility>

namespace aiws {

// Initialize a document with its unique ID, title, and text.
// Use move semantics to transfer the strings into member variables
// without making unnecessary copies.
Document::Document(std::string id, std::string title, std::string text)
    : id_(std::move(id)),
      title_(std::move(title)),
      text_(std::move(text)) {}

// Return read-only references to the document's stored information.
// Returning references avoids copying potentially large strings.
const std::string& Document::id() const noexcept {
    return id_;
}

const std::string& Document::title() const noexcept {
    return title_;
}

const std::string& Document::text() const noexcept {
    return text_;
}

}  // namespace aiws
