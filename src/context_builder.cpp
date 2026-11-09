
#include "aiws/context_builder.hpp"

#include "aiws/text_processor.hpp"

#include <algorithm>
#include <unordered_set>

namespace aiws {

std::vector<ContextItem> ContextBuilder::build(
    const std::vector<SearchResult>& ranked,
    std::size_t token_budget) const {

    // Store the selected context items and track the remaining token budget.
    std::vector<ContextItem> context;
    std::size_t remaining = token_budget;

    // Track chunk IDs to avoid including duplicate chunks.
    std::unordered_set<std::string> used;

    // Process search results in ranked order, prioritizing higher-ranked chunks.
    for (const auto& result : ranked) {

        // Stop when no token budget remains.
        if (remaining == 0) break;

        // Skip chunks that have already been included in the context.
        if (!used.insert(result.chunk_id).second) continue;

        // Tokenize the result and determine how much fits in the budget.
        const auto terms = TextProcessor::terms(result.text);
        const std::size_t take = std::min(remaining, terms.size());

        // Mark the item as truncated if only part of its text fits.
        const bool truncated = take < terms.size();

        // Store the selected text along with its source and ranking information.
        context.push_back(ContextItem{
            result.chunk_id,
            result.document_id,
            result.chunk_sequence,
            TextProcessor::join(terms, 0, take),
            take,
            result.score,
            truncated
        });

        // Subtract the tokens used by this item from the available budget.
        remaining -= take;

        // Stop after including a truncated result.
        if (truncated) break;
    }

    return context;
}

}  // namespace aiws
