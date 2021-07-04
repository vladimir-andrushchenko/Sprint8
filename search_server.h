#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <execution>

#include "document.h"

enum class Policy {
    parallel, sequential
};

class SearchServer {
public:
    SearchServer() = default;
    
    template <typename StringCollection>
    explicit SearchServer(const StringCollection& stop_words);
    
    explicit SearchServer(const std::string& stop_words);

    explicit SearchServer(const std::string_view stop_words);
    
public:
    void SetStopWords(std::string_view text);
    
    bool AddDocument(int document_id, const std::string& document,
                     DocumentStatus status, const std::vector<int>& ratings);
    
    int GetDocumentCount() const;
    
    template<typename Predicate>
    std::vector<Document> FindTopDocuments(const std::string& raw_query, Predicate predicate) const;
    
    std::vector<Document> FindTopDocuments(const std::string& raw_query,
                                           const DocumentStatus& desired_status = DocumentStatus::ACTUAL) const;
    
    std::tuple<std::vector<std::string>, DocumentStatus> MatchDocument(const std::string& raw_query, int document_id, Policy policy = Policy::sequential) const;

    std::tuple<std::vector<std::string>, DocumentStatus> MatchDocument(std::execution::parallel_policy, const std::string& raw_query, int document_id) const;

    std::tuple<std::vector<std::string>, DocumentStatus> MatchDocument(std::execution::sequenced_policy, const std::string& raw_query, int document_id) const;
    
    std::set<int>::const_iterator begin() const;
    
    std::set<int>::const_iterator end() const;
    
    const std::map<std::string, double>& GetWordFrequencies(int document_id) const;
    
    void RemoveDocument(int document_id, Policy policy = Policy::sequential);

    void RemoveDocument(std::execution::sequenced_policy p, const int document_id);

    void RemoveDocument(std::execution::parallel_policy p, int document_id);
    
private:
    struct DocumentData {
        int rating = 0;
        DocumentStatus status = DocumentStatus::ACTUAL;
        std::map<std::string, double> word_frequencies;
    };
    
    struct Query {
        std::set<std::string> plus_words;
        std::set<std::string> minus_words;

        Query& operator+=(Query&& other) {
            for (const auto& other_plus_word : other.plus_words) {
                plus_words.insert(std::move(other_plus_word));
            }

            for (const auto& other_minus_word : other.minus_words) {
                minus_words.insert(std::move(other_minus_word));
            }

            return *this;
        }
    };
    
    struct QueryWord {
        std::string data;
        bool is_minus = false;
        bool is_stop = false;
    };
    
private:
    static constexpr int kMaxResultDocumentCount = 5;
    static constexpr double kAccuracy = 1e-6;
    
private:
    std::vector<std::string> SplitIntoWordsNoStop(const std::string& text) const;
    
    static int ComputeAverageRating(const std::vector<int>& ratings);
    
    bool IsStopWord(const std::string& word) const;
    
    [[nodiscard]] bool ParseQueryWord(std::string text, QueryWord& result) const;
    
    [[nodiscard]] bool ParseQuery(const std::string& text, Query& result) const;
    
    // Existence required
    double ComputeWordInverseDocumentFrequency(const std::string& word) const;
    
    std::vector<Document> FindAllDocuments(const Query& query) const;
    
    template<typename StringType>
    static bool IsValidWord(const StringType& word) {
        return std::none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
        });
    }
    
private:
    std::set<std::string> stop_words_;
    
    std::map<std::string, std::map<int, double>> word_to_document_id_to_term_frequency_;
    
    std::map<int, DocumentData> document_id_to_document_data_;
    
    std::set<int> document_ids_;
};

template <typename StringCollection>
SearchServer::SearchServer(const StringCollection& stop_words) {
    using namespace std::literals;
    
    for (const auto& stop_word : stop_words) {
        if (!IsValidWord(stop_word)) {
            throw std::invalid_argument("stop word contains unaccaptable symbol"s);
        }
        
        stop_words_.insert(stop_word);
    }
}

template<typename Predicate>
std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query, Predicate predicate) const {
     Query query;
    if (!ParseQuery(raw_query, query)) {
        throw std::invalid_argument("invalid request");
    };
    
    std::vector<Document> matched_documents = FindAllDocuments(query);
    
    std::vector<Document> filtered_documents;
    for (const Document& document : matched_documents) {
        const auto document_status = document_id_to_document_data_.at(document.id).status;
        const auto document_rating = document_id_to_document_data_.at(document.id).rating;
        
        if (predicate(document.id, document_status, document_rating)) {
            filtered_documents.push_back(document);
        }
    }
    
    std::sort(filtered_documents.begin(), filtered_documents.end(),
              [](const Document& left, const Document& right) {
        if (std::abs(left.relevance - right.relevance) < kAccuracy) {
            return left.rating > right.rating;
        } else {
            return left.relevance > right.relevance;
        }
    });
    
    if (static_cast<int>(filtered_documents.size()) > kMaxResultDocumentCount) {
        filtered_documents.resize(static_cast<size_t>(kMaxResultDocumentCount));
    }
    
    return filtered_documents;
} // FindTopDocuments

namespace search_server_helpers {

void PrintMatchDocumentResult(int document_id, const std::vector<std::string>& words, DocumentStatus status);

void AddDocument(SearchServer& search_server, int document_id, const std::string& document, DocumentStatus status,
                 const std::vector<int>& ratings);

void FindTopDocuments(const SearchServer& search_server, const std::string& raw_query);

void MatchDocuments(const SearchServer& search_server, const std::string& query);

SearchServer CreateSearchServer(const std::string& stop_words);

} // namespace search_server_helpers
