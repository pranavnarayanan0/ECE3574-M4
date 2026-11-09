
#include "aiws/text_processor.hpp"

#include <algorithm>

namespace aiws {
namespace {

// Check whether a character is an ASCII letter or digit.
// Other characters act as separators between tokens.
bool is_ascii_alnum(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

// Convert uppercase ASCII letters to lowercase for consistent matching.
char lower_ascii(unsigned char c) {
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c - 'A' + 'a');

    return static_cast<char>(c);
}

// Check whether the text between two tokens contains a blank line.
// A blank line is treated as a paragraph boundary during chunking.
bool blank_line_between(const std::string& text, std::size_t a, std::size_t b) {
    bool first_newline = false;

    for (std::size_t i = a; i < b;) {
        const char ch = text[i];

        // Recognize Unix (\n), Windows (\r\n), and older Mac (\r) line endings.
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && i + 1 < b && text[i + 1] == '\n') ++i;

            // A second newline separated only by whitespace indicates a blank line.
            if (first_newline)
                return true;

            first_newline = true;
            ++i;
            continue;
        }

        // Non-whitespace characters between newlines reset the blank-line check.
        if (first_newline && ch != ' ' && ch != '\t')
            first_newline = false;

        ++i;
    }

    return false;
}

}  // namespace

std::vector<TokenInfo> TextProcessor::tokenize(const std::string& text) {
    std::vector<TokenInfo> result;
    std::string current;
    std::size_t token_begin = 0;

    // Store a completed token with its original character positions.
    // The lambda is reused whenever a token ends.
    auto finish = [&](std::size_t end) {
        if (!current.empty()) {
            result.push_back(TokenInfo{current, token_begin, end, 0});
            current.clear();
        }
    };

    // Scan the input one character at a time to identify tokens.
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (is_ascii_alnum(c)) {
            // Record the starting position of each new token.
            if (current.empty())
                token_begin = i;

            // Normalize characters to lowercase as tokens are created.
            current.push_back(lower_ascii(c));
        } else {
            // Punctuation and whitespace end the current token.
            finish(i);
        }
    }

    // Store the final token if the text does not end with a separator.
    finish(text.size());

    // Assign paragraph numbers so the chunker can prefer paragraph boundaries.
    std::size_t paragraph = 0;

    for (std::size_t i = 0; i < result.size(); ++i) {
        if (i > 0 && blank_line_between(text, result[i - 1].end, result[i].begin)) {
            ++paragraph;
        }

        result[i].paragraph = paragraph;
    }

    return result;
}

// Extract only the normalized token strings, without their metadata.
std::vector<std::string> TextProcessor::terms(const std::string& text) {
    const auto tokens = tokenize(text);
    std::vector<std::string> result;

    // Reserve capacity to avoid repeated vector reallocations.
    result.reserve(tokens.size());

    for (const auto& token : tokens)
        result.push_back(token.token);

    return result;
}

// Normalize text into lowercase tokens separated by single spaces.
std::string TextProcessor::normalize(const std::string& text) {
    const auto tokens = tokenize(text);
    return join(tokens, 0, tokens.size());
}

// Combine TokenInfo objects into text using the requested token range.
std::string TextProcessor::join(const std::vector<TokenInfo>& tokens,
                                std::size_t begin,
                                std::size_t end) {

    // Clamp the end index to avoid accessing beyond the vector.
    end = std::min(end, tokens.size());

    std::string result;

    for (std::size_t i = begin; i < end; ++i) {
        // Separate tokens with spaces without adding a leading space.
        if (!result.empty())
            result.push_back(' ');

        result += tokens[i].token;
    }

    return result;
}

// Overload for joining a vector of strings instead of TokenInfo objects.
std::string TextProcessor::join(const std::vector<std::string>& tokens,
                                std::size_t begin,
                                std::size_t end) {

    end = std::min(end, tokens.size());

    std::string result;

    for (std::size_t i = begin; i < end; ++i) {
        if (!result.empty())
            result.push_back(' ');

        result += tokens[i];
    }

    return result;
}

}  // namespace aiws
