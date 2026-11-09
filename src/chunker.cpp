
#include "aiws/chunker.hpp"

#include "aiws/text_processor.hpp"

#include <algorithm>
#include <stdexcept>

namespace aiws {

// Validate the chunking policy to prevent invalid chunk sizes or overlap.
Chunker::Chunker(ChunkingPolicy policy) : policy_(policy) {
    if (policy_.max_tokens == 0 || policy_.overlap >= policy_.max_tokens ||
        policy_.paragraph_window > policy_.max_tokens) {
        throw std::invalid_argument("invalid chunking policy");
    }
}

std::vector<Chunk> Chunker::chunk(const Document& document,
                                  std::size_t document_order) const {

    // Convert the document into tokens while preserving source positions.
    const auto tokens = TextProcessor::tokenize(document.text());

    std::vector<Chunk> chunks;
    std::size_t start = 0;
    std::size_t sequence = 0;

    // Continue until all tokens have been included in a chunk.
    while (start < tokens.size()) {
        std::size_t end = tokens.size();

        // If the remaining text exceeds the token limit, find a suitable boundary.
        if (tokens.size() - start > policy_.max_tokens) {
            const std::size_t hard_end = start + policy_.max_tokens;
            const std::size_t earliest = hard_end - policy_.paragraph_window;
            std::size_t preferred = hard_end;
            bool found = false;

            // Prefer a paragraph boundary near the token limit
            // so chunks avoid splitting paragraphs when possible.
            for (std::size_t boundary = earliest; boundary <= hard_end; ++boundary) {
                if (boundary > start && boundary < tokens.size() &&
                    tokens[boundary - 1].paragraph != tokens[boundary].paragraph) {
                    preferred = boundary;
                    found = true;
                }
            }

            // Fall back to the maximum token boundary if none was found.
            end = found ? preferred : hard_end;
        }

        // Store chunk metadata, text, and positions in the original document.
        Chunk c;
        c.document_id = document.id();
        c.document_order = document_order;
        c.sequence = sequence;
        c.id = document.id() + "#" + std::to_string(sequence);
        c.text = TextProcessor::join(tokens, start, end);
        c.token_count = end - start;
        c.source_begin = tokens[start].begin;
        c.source_end = tokens[end - 1].end;

        chunks.push_back(std::move(c));

        // Stop once the final token has been included.
        if (end == tokens.size()) break;

        // Move forward while retaining overlapping tokens for context continuity.
        // The max() ensures that the starting position always advances.
        start = std::max(start + 1, end - policy_.overlap);
        ++sequence;
    }

    return chunks;
}

}  // namespace aiws
