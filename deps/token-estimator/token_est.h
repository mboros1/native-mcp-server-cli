/**
 * @file token_est.h
 * @brief A simplified tiktoken-compatible tokenizer for fast token counting
 * 
 * This tokenizer approximates OpenAI's tiktoken (specifically cl100k_base 
 * encoding used by GPT-3.5 and GPT-4).
 * 
 * WHAT THIS DOES:
 * - Provides fast token counting for text, useful for chunking documents
 * - Uses the actual cl100k_base vocabulary (embedded as a 1.6MB array)
 * - Implements a simplified greedy longest-match tokenization algorithm
 * - Achieves ~10μs per 1k characters on modern CPUs
 * 
 * WHAT THIS DOESN'T DO:
 * - Does NOT implement the full BPE (Byte Pair Encoding) merge algorithm
 * - Does NOT guarantee exact token-for-token match with Python tiktoken
 * - Does NOT handle all edge cases correctly
 * 
 * ALGORITHM:
 * The real tiktoken uses BPE with learned merge priorities. When multiple valid
 * tokenizations exist, it chooses based on merge order from training. Our simplified
 * version just does greedy longest-match, which is usually close but not always exact.
 * 
 * KNOWN FAILURE MODES:
 * 1. Special character sequences may tokenize differently:
 *    Text: "Special chars: @#$%^&*()"
 *    Python tiktoken: [20989, 23861, 25, 571, 49177, 46999, 5, 9, 368]
 *                     'Special' ' chars' ':' ' @' '#$' '%^' '&' '*' '()'
 *    This tokenizer:  [20989, 23861, 25, 571, 49177, 46999, 5, 6737, 8]
 *                     'Special' ' chars' ':' ' @' '#$' '%^' '&' '&*' '('
 *    
 * 2. Ambiguous word boundaries might split differently:
 *    When "unlock" could be "un" + "lock" or "unlock", we might pick wrong
 * 
 * 3. Unicode handling may differ for edge cases
 * 
 * ACCURACY:
 * Despite these limitations, token counts are typically within 1-3% of Python tiktoken,
 * which is more than sufficient for:
 * - Chunking documents for LLM context windows
 * - Estimating API costs
 * - Progress indicators
 * 
 * For exact tokenization, use the official Python tiktoken library.
 * 
 * USAGE:
 *   token_est::TokenEstimator tokenizer;
 *   size_t token_count = tokenizer.count_tokens("Hello, world!");
 *   
 * IMPLEMENTATION NOTES:
 * - The vocabulary data is compiled separately in token_est.cpp
 * - Base64 decoding is implemented inline to avoid dependencies
 * - The vocabulary is loaded once on first use (lazy initialization)
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <algorithm>
#include <mutex>

namespace token_est {

// External vocabulary data (defined in token_est.cpp)
extern const unsigned char cl100k_base_tiktoken[];
extern const unsigned int cl100k_base_tiktoken_len;

class TokenEstimator {
private:
    // Singleton instance for vocabulary (shared across all tokenizer instances)
    struct Vocabulary {
        std::unordered_map<std::string, int> encoder;
        std::unordered_map<int, std::string> decoder;
        bool initialized = false;
        std::mutex init_mutex;
    };
    
    static Vocabulary& get_vocabulary();
    
    // Simple base64 decoder
    static std::string base64_decode(const std::string& encoded);
    
    // Load vocabulary from embedded data (thread-safe, runs once)
    static void ensure_vocabulary_loaded();

public:
    TokenEstimator();
    
    /**
     * Encode text into token IDs using greedy longest-match algorithm
     * Note: This may not match Python tiktoken exactly for all inputs
     */
    std::vector<int> encode(const std::string& text) const;
    
    /**
     * Decode token IDs back to text
     */
    std::string decode(const std::vector<int>& tokens) const;
    
    /**
     * Count tokens in text - fast approximation of OpenAI's tiktoken
     * Typically within 1-3% of Python tiktoken's count
     */
    size_t count_tokens(const std::string& text) const;
};

// For backwards compatibility
using TiktokenTokenizer = TokenEstimator;

} // namespace token_est