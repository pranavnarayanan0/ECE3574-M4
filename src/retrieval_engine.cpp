
#include "aiws/retrieval_engine.hpp"

#include "aiws/text_processor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace aiws {

// Round scores to a consistent precision to reduce floating-point
// differences and make ranking results more predictable.
double RetrievalEngine::canonical_score(double value) {
    constexpr double scale = 1'000'000'000'000.0;
    return std::round(value * scale) / scale;
}

std::vector<SearchResult> RetrievalEngine::search(
    const std::string& query,
    int k,
    const std::vector<Chunk>& chunks,
    const CorpusIndex& index) const {

    // Reject negative result counts, since k represents how many results to return.
    if (k < 0)
        throw std::invalid_argument("k must be non-negative");

    // No results are possible if k is zero or the corpus has no chunks.
    if (k == 0 || chunks.empty())
        return {};

    // Convert the search query into normalized searchable terms.
    const auto raw_terms = TextProcessor::terms(query);

    if (raw_terms.empty())
        return {};

    // Remove repeated query terms so each distinct term contributes once.
    // Keep the original term order in the vector.
    std::vector<std::string> terms;
    std::unordered_set<std::string> seen;

    for (const auto& term : raw_terms) {
        if (seen.insert(term).second)
            terms.push_back(term);
    }

    // Accumulate each matching chunk's score and number of matched query terms.
    struct Accum {
        double base{};
        std::size_t matched{};
    };

    // Key: chunk index, Value: accumulated scoring information.
    std::unordered_map<std::size_t, Accum> accum;

    const double n = static_cast<double>(chunks.size());

    // Calculate relevance contributions for every distinct query term.
    for (const auto& term : terms) {

        // Retrieve the chunks containing this term from the inverted index.
        const auto* postings = index.postings(term);

        // Terms absent from the corpus contribute nothing to the results.
        if (!postings)
            continue;

        // DF is the number of chunks containing the term.
        // IDF gives more weight to terms that appear in fewer chunks.
        const double df = static_cast<double>(postings->size());
        const double idf = std::log((n + 1.0) / (df + 1.0)) + 1.0;

        // Add this term's contribution to every chunk that contains it.
        for (const auto& posting : *postings) {

            // Logarithmic TF rewards repetition without increasing linearly.
            const double tf =
                1.0 + std::log(static_cast<double>(posting.frequency));

            auto& a = accum[posting.chunk_index];

            // Accumulate TF-IDF contributions from matching query terms.
            a.base += tf * idf;

            // Count how many distinct query terms matched this chunk.
            ++a.matched;
        }
    }

    // Convert accumulated scores into SearchResult objects.
    std::vector<SearchResult> results;
    results.reserve(accum.size());

    for (const auto& [chunk_index, a] : accum) {
        const Chunk& c = chunks[chunk_index];

        // Give a small relevance bonus to chunks matching more query terms.
        const double coverage = 1.0 + 0.10 *
            (static_cast<double>(a.matched) /
             static_cast<double>(terms.size()));

        // Preserve source information and use a rounded final score.
        results.push_back(SearchResult{
            c.id,
            c.document_id,
            c.sequence,
            c.text,
            canonical_score(a.base * coverage),
            a.matched
        });
    }

    // Rank results by score from highest to lowest.
    std::sort(results.begin(), results.end(),
        [&](const SearchResult& a, const SearchResult& b) {

            if (a.score != b.score)
                return a.score > b.score;

            // Break score ties using original document order.
            const Chunk& ca =
                chunks.at(index.chunk_index(a.chunk_id));

            const Chunk& cb =
                chunks.at(index.chunk_index(b.chunk_id));

            if (ca.document_order != cb.document_order)
                return ca.document_order < cb.document_order;

            // Within the same document, prefer earlier chunks.
            return ca.sequence < cb.sequence;
        });

    // Return at most k results, keeping the highest-ranked matches.
    if (static_cast<std::size_t>(k) < results.size())
        results.resize(static_cast<std::size_t>(k));

    return results;
}

}  // namespace aiws
