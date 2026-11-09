
#include "aiws/processing_core.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace aiws {

// Store the backend processing state and strategy objects internally.
// This keeps implementation details separate from ProcessingCore's public interface.
struct ProcessingCore::Impl {
    std::vector<Chunk> chunks;
    CorpusIndex index;

    // Own the three processing strategies through polymorphic interfaces.
    // unique_ptr ensures exclusive ownership and automatic cleanup.
    std::unique_ptr<ChunkingStrategy> chunking;
    std::unique_ptr<RetrievalStrategy> retrieval;
    std::unique_ptr<ContextStrategy> context;

    // Transfer ownership of the supplied strategies into the implementation.
    Impl(std::unique_ptr<ChunkingStrategy> c,
         std::unique_ptr<RetrievalStrategy> r,
         std::unique_ptr<ContextStrategy> ctx)
        : chunking(std::move(c)),
          retrieval(std::move(r)),
          context(std::move(ctx)) {}
};

// Construct ProcessingCore with the default M1 processing strategies.
ProcessingCore::ProcessingCore()
    : impl_(std::make_unique<Impl>(
          std::make_unique<Chunker>(
              ChunkingPolicy{kMaxChunkTokens,
                             kChunkOverlap,
                             kParagraphPreferenceWindow}),
          std::make_unique<RetrievalEngine>(),
          std::make_unique<ContextBuilder>())) {}

// Allow custom strategy implementations to be supplied at construction.
ProcessingCore::ProcessingCore(
    std::unique_ptr<ChunkingStrategy> chunking,
    std::unique_ptr<RetrievalStrategy> retrieval,
    std::unique_ptr<ContextStrategy> context) {

    // Reject missing strategies because all three are required for processing.
    if (!chunking || !retrieval || !context) {
        throw std::invalid_argument("strategy must not be null");
    }

    // Move the supplied strategies into ProcessingCore's internal state.
    impl_ = std::make_unique<Impl>(
        std::move(chunking),
        std::move(retrieval),
        std::move(context));
}

// Use default special members for cleanup and efficient move operations.
ProcessingCore::~ProcessingCore() = default;
ProcessingCore::ProcessingCore(ProcessingCore&&) noexcept = default;
ProcessingCore& ProcessingCore::operator=(ProcessingCore&&) noexcept = default;

// Delegate text normalization to TextProcessor.
std::string ProcessingCore::normalize(const std::string& text) {
    return TextProcessor::normalize(text);
}

void ProcessingCore::rebuild(const Workspace& workspace) {

    // Track document IDs to reject duplicates in the workspace.
    std::unordered_set<std::string> document_ids;

    // Build a new chunk collection before replacing the current one.
    std::vector<Chunk> next_chunks;

    // Process documents in workspace order to preserve document ordering.
    for (std::size_t order = 0; order < workspace.documents().size(); ++order) {
        const auto& doc = workspace.documents()[order];

        // Duplicate document IDs would make document identification ambiguous.
        if (!document_ids.insert(doc.id()).second) {
            throw std::invalid_argument("duplicate document id: " + doc.id());
        }

        // Use the selected chunking strategy through its virtual interface.
        auto produced = impl_->chunking->chunk(doc, order);

        // Move the generated chunks into the new collection.
        next_chunks.insert(next_chunks.end(),
                           std::make_move_iterator(produced.begin()),
                           std::make_move_iterator(produced.end()));
    }

    // Construct the new inverted index using the generated chunks.
    CorpusIndex next_index(next_chunks);

    // Replace the previous processing state only after construction succeeds.
    impl_->chunks = std::move(next_chunks);
    impl_->index = std::move(next_index);
}

// Provide read-only access to the generated chunks without copying the vector.
const std::vector<Chunk>& ProcessingCore::chunks() const noexcept {
    return impl_->chunks;
}

// Return the number of chunks currently stored in the processing core.
std::size_t ProcessingCore::chunk_count() const noexcept {
    return impl_->chunks.size();
}

// Calculate the number of indexed chunks containing a specified term.
std::size_t ProcessingCore::document_frequency(
    const std::string& term) const {

    // Normalize the input into terms and require exactly one searchable term.
    const auto terms = TextProcessor::terms(term);

    if (terms.empty()) return 0;

    if (terms.size() != 1) {
        throw std::invalid_argument("term must normalize to one token");
    }

    // Delegate the frequency lookup to the inverted index.
    return impl_->index.document_frequency(terms.front());
}

// Calculate how often a term occurs within a specific chunk.
std::size_t ProcessingCore::term_frequency(
    const std::string& term,
    const std::string& chunk_id) const {

    // Reject input that normalizes into multiple terms.
    const auto terms = TextProcessor::terms(term);

    if (terms.empty()) return 0;

    if (terms.size() != 1) {
        throw std::invalid_argument("term must normalize to one token");
    }

    // Use the index to look up the term frequency for the selected chunk.
    return impl_->index.term_frequency(terms.front(), chunk_id);
}

// Retrieve relevant chunks using the configured retrieval strategy.
std::vector<SearchResult> ProcessingCore::search(
    const std::string& query, int k) const {

    return impl_->retrieval->search(
        query, k, impl_->chunks, impl_->index);
}

// Build context from search results while respecting the token budget.
std::vector<ContextItem> ProcessingCore::build_context(
    const std::string& query,
    int k,
    std::size_t token_budget) const {

    // Reject negative result counts.
    if (k < 0) {
        throw std::invalid_argument("k must be non-negative");
    }

    // An empty budget cannot produce any context.
    if (token_budget == 0) return {};

    // First retrieve relevant chunks, then pass them to the context strategy.
    return impl_->context->build(search(query, k), token_budget);
}

}  // namespace aiws
