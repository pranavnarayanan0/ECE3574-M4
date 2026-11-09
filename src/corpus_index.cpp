
#include "aiws/corpus_index.hpp"

#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <unordered_map>

namespace aiws {

// Construct the index by building lookup tables from the provided chunks.
CorpusIndex::CorpusIndex(const std::vector<Chunk>& chunks) {
    build(chunks);
}

void CorpusIndex::build(const std::vector<Chunk>& chunks) {

    // Build temporary maps so the existing index is not replaced
    // until the new index has been constructed successfully.
    std::unordered_map<std::string, std::vector<Posting>> next_postings;
    std::unordered_map<std::string, std::size_t> next_lookup;

    // Process each chunk and record its position in the chunk collection.
    for (std::size_t i = 0; i < chunks.size(); ++i) {

        // Map each unique chunk ID to its index.
        // Reject duplicate IDs to avoid ambiguous chunk lookups.
        if (!next_lookup.emplace(chunks[i].id, i).second) {
            throw std::invalid_argument("duplicate chunk id: " + chunks[i].id);
        }

        // Count how many times each term occurs in the current chunk.
        std::unordered_map<std::string, std::size_t> frequency;

        for (const auto& term : TextProcessor::terms(chunks[i].text)) {
            ++frequency[term];
        }

        // Build the inverted index: each term maps to a list of chunks
        // containing that term, along with its frequency in each chunk.
        for (const auto& [term, count] : frequency) {
            next_postings[term].push_back(Posting{i, count});
        }
    }

    // Replace the old index only after the new maps are successfully built.
    postings_ = std::move(next_postings);
    chunk_by_id_ = std::move(next_lookup);
}

// Return the number of indexed chunks containing the specified term.
std::size_t CorpusIndex::document_frequency(
    const std::string& normalized_term) const noexcept {

    const auto it = postings_.find(normalized_term);

    // A missing term has document frequency zero.
    return it == postings_.end() ? 0 : it->second.size();
}

// Return how many times a term appears in a specific chunk.
std::size_t CorpusIndex::term_frequency(
    const std::string& normalized_term,
    const std::string& chunk_id) const noexcept {

    // Find the chunk's numeric index using its unique ID.
    const auto lookup = chunk_by_id_.find(chunk_id);

    // An unknown chunk cannot have a recorded term frequency.
    if (lookup == chunk_by_id_.end()) return 0;

    // Find the list of chunks containing the requested term.
    const auto it = postings_.find(normalized_term);

    if (it == postings_.end()) return 0;

    // Search the postings for the specified chunk and return its term count.
    for (const auto& posting : it->second) {
        if (posting.chunk_index == lookup->second) return posting.frequency;
    }

    // The term is not present in the requested chunk.
    return 0;
}

// Return the posting list for a term, or nullptr if the term is absent.
const std::vector<CorpusIndex::Posting>* CorpusIndex::postings(
    const std::string& normalized_term) const noexcept {

    const auto it = postings_.find(normalized_term);
    return it == postings_.end() ? nullptr : &it->second;
}

// Retrieve a chunk using its ID, with bounds checking for safe access.
const Chunk* CorpusIndex::find_chunk(
    const std::vector<Chunk>& chunks,
    const std::string& chunk_id) const noexcept {

    const auto it = chunk_by_id_.find(chunk_id);

    // Reject missing IDs or indexes outside the supplied chunk vector.
    if (it == chunk_by_id_.end() || it->second >= chunks.size()) {
        return nullptr;
    }

    return &chunks[it->second];
}

// Convert a chunk ID into its numeric position in the indexed collection.
std::size_t CorpusIndex::chunk_index(
    const std::string& chunk_id) const {

    const auto it = chunk_by_id_.find(chunk_id);

    // Unlike find_chunk(), this function reports an unknown ID
    // by throwing an exception instead of returning a null pointer.
    if (it == chunk_by_id_.end()) {
        throw std::out_of_range("unknown chunk id");
    }

    return it->second;
}

}  // namespace aiws
