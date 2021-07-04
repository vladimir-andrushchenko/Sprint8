#include <cassert>
#include <cmath>
#include <algorithm>
#include <execution>
#include <utility>

#include "search_server.h"
#include "string_processing.h"

#include "log_duration.h"

using namespace std::literals;

std::set<int>::const_iterator SearchServer::begin() const {
    return document_ids_.begin();
}

std::set<int>::const_iterator SearchServer::end() const {
    return document_ids_.end();
}

const std::map<std::string, double>& SearchServer::GetWordFrequencies(int document_id) const {
    const static std::map<std::string, double> empty_map;
    
    if (document_id_to_document_data_.count(document_id) > 0) {
        return document_id_to_document_data_.at(document_id).word_frequencies;
    }
    
    return empty_map;
}

void SearchServer::RemoveDocument(int document_id, Policy policy) {
    if (document_id_to_document_data_.count(document_id) == 0) {
        return;
    }

    // get list of words that are in this doc
    const auto words_and_frequencies = GetWordFrequencies(document_id);

    // initialize linear container that will contain inner maps where id point to frequency
    std::vector<std::map<int, double>> id_to_frequency;
    id_to_frequency.reserve(words_and_frequencies.size());

    // populate this inner container
    for (const auto& [word, term_frequency] : words_and_frequencies) {
        id_to_frequency.push_back(std::move(word_to_document_id_to_term_frequency_.at(word)));
    }

    // change inner maps
    if (policy == Policy::parallel) {
        std::for_each(std::execution::par, id_to_frequency.begin(), id_to_frequency.end(), [document_id](std::map<int, double>& element){
            element.erase(document_id);
        });

    } else {
        std::for_each(std::execution::seq, id_to_frequency.begin(), id_to_frequency.end(), [document_id](std::map<int, double>& element){
            element.erase(document_id);
        }); 
    }

    // and put them back
    auto it = id_to_frequency.begin();
    for (const auto& [word, term_frequency] : words_and_frequencies) {
        word_to_document_id_to_term_frequency_.at(word) = std::move(*(it++));
        
        if (word_to_document_id_to_term_frequency_.at(word).empty()) {
            word_to_document_id_to_term_frequency_.erase(word);
        }
    }

// not parallel
    document_id_to_document_data_.erase(document_id);
    
    document_ids_.erase(document_id);
}

void SearchServer::RemoveDocument(std::execution::sequenced_policy p, int document_id) {
    RemoveDocument(document_id, Policy::sequential);
}

void SearchServer::RemoveDocument(std::execution::parallel_policy p, int document_id) {
    RemoveDocument(document_id, Policy::parallel);
}

SearchServer::SearchServer(const std::string& stop_words) {
    if (!IsValidWord(stop_words)) {
        throw std::invalid_argument("stop word contains unaccaptable symbol"s);
    }
    
    SetStopWords(stop_words);
}

SearchServer::SearchServer(const std::string_view stop_words) {
    if (!IsValidWord(stop_words)) {
        throw std::invalid_argument("stop word contains unaccaptable symbol"s);
    }
    
    SetStopWords(stop_words);
}

void SearchServer::SetStopWords(std::string_view text) {
    for (const std::string_view word : string_processing::SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw std::invalid_argument("Bad stop word");
        }
        
        stop_words_.emplace(word);
    }
} // SetStopWords

bool SearchServer::AddDocument(int document_id, const std::string& document,
                               DocumentStatus status, const std::vector<int>& ratings) {
    if (document_id < 0) {
        throw std::invalid_argument("negative ids are not allowed"s);
    }
    
    if (document_id_to_document_data_.count(document_id) > 0) {
        throw std::invalid_argument("repeating ids are not allowed"s);
    }
    
    if (!IsValidWord(document)) {
        throw std::invalid_argument("word in document contains unaccaptable symbol"s);
    }
    
    const std::vector<std::string> words = SplitIntoWordsNoStop(document);
    
    const double inverse_word_count = 1.0 / static_cast<double>(words.size());
    
    std::map<std::string, double> word_frequencies;
    
    for (const std::string& word : words) {
        word_to_document_id_to_term_frequency_[word][document_id] += inverse_word_count;
        word_frequencies[word] += inverse_word_count;
    }
    
    document_ids_.insert(document_id);
    
    document_id_to_document_data_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status, word_frequencies});
    
    return true;
} // AddDocument

int SearchServer::GetDocumentCount() const {
    return static_cast<int>(document_id_to_document_data_.size());
} // GetDocumentCount



std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query,
                                                     const DocumentStatus& desired_status) const {
    const auto predicate = [desired_status](int , DocumentStatus document_status, int ) {
        return document_status == desired_status;
    };
    
    return FindTopDocuments(raw_query, predicate);
} // FindTopDocuments with status as a second argument

std::tuple<std::vector<std::string>, DocumentStatus> SearchServer::MatchDocument(std::execution::parallel_policy, const std::string& raw_query, int document_id) const {
    return MatchDocument(raw_query, document_id, Policy::parallel);
}

std::tuple<std::vector<std::string>, DocumentStatus> SearchServer::MatchDocument(std::execution::sequenced_policy, const std::string& raw_query, int document_id) const {
    return MatchDocument(raw_query, document_id, Policy::sequential);
}

std::tuple<std::vector<std::string>, DocumentStatus> SearchServer::MatchDocument(
                                                                                 const std::string& raw_query, 
                                                                                 int document_id,
                                                                                 Policy policy) const {
     Query query;
    if (!ParseQuery(raw_query, query)) {
        throw std::invalid_argument("invalid request");
    }
    
    std::vector<std::string> matched_words;
    for (const std::string& word : query.plus_words) {
        if (word_to_document_id_to_term_frequency_.count(word) == 0) {
            continue;
        }
        
        if (word_to_document_id_to_term_frequency_.at(word).count(document_id)) {
            matched_words.push_back(word);
        }
    }
    
    for (const std::string& word : query.minus_words) {
        if (word_to_document_id_to_term_frequency_.count(word) == 0) {
            continue;
        }
        
        if (word_to_document_id_to_term_frequency_.at(word).count(document_id)) {
            matched_words.clear();
            break;
        }
    }
    
    return std::tuple<std::vector<std::string>, DocumentStatus>{matched_words, document_id_to_document_data_.at(document_id).status};
} // MatchDocument

std::vector<std::string> SearchServer::SplitIntoWordsNoStop(const std::string& text) const {
    std::vector<std::string> words;
    for (const std::string& word : string_processing::SplitIntoWords(text)) {
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    
    return words;
} // SplitIntoWordsNoStop

int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    int rating_sum = 0;
    
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    
    return rating_sum / static_cast<int>(ratings.size());
} // ComputeAverageRating

bool SearchServer::IsStopWord(const std::string& word) const {
    return stop_words_.count(word) > 0;
} // IsStopWord

[[nodiscard]] bool SearchServer::ParseQueryWord(std::string text, QueryWord& result) const {
    result = {};

    if (text.empty()) {
        return false;
    }
    bool is_minus = false;
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }
    if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        return false;
    }

    result = QueryWord{ text, is_minus, IsStopWord(text) };
    return true;
}

[[nodiscard]] bool SearchServer::ParseQuery(const std::string& text, Query& result) const {

    result = {};
    for (const std::string& word : string_processing::SplitIntoWords(text)) {
        QueryWord query_word;
        if (!ParseQueryWord(word, query_word)) {
            return false;
        }
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.insert(query_word.data);
            }
            else {
                result.plus_words.insert(query_word.data);
            }
        }
    }
    return true;
}

/*
SearchServer::Query SearchServer::ParseQuery(const std::string& text, Policy policy) const {
    auto words = string_processing::SplitIntoWords(text);

    const auto transform_word_in_query = [&](const std::string& word){
        auto query_word = ParseQueryWord(word);

        Query query;
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                query.minus_words.insert(query_word.data);
            } else {
                query.plus_words.insert(query_word.data);
            }
        }

        return query;
    };

    const auto combine_queries = [](Query first, Query second){
        return first += std::move(second);
    };

    if (policy == Policy::parallel) {
        return std::transform_reduce(std::execution::par, words.begin(), words.end(), Query{}, combine_queries, transform_word_in_query);
    } else {
        return std::transform_reduce(std::execution::seq, words.begin(), words.end(), Query{}, combine_queries, transform_word_in_query);
    }
} // ParseQuery
*/

// Existence required
double SearchServer::ComputeWordInverseDocumentFrequency(const std::string& word) const {
    assert(word_to_document_id_to_term_frequency_.count(word) != 0);
    
    const size_t number_of_documents_constains_word = word_to_document_id_to_term_frequency_.at(word).size();
    
    assert(number_of_documents_constains_word != 0);
    
    return std::log(static_cast<double>(GetDocumentCount()) / number_of_documents_constains_word);
} // ComputeWordInverseDocumentFrequency

std::vector<Document> SearchServer::FindAllDocuments(const Query& query) const {
    std::map<int, double> document_id_to_relevance;
    
    for (const std::string& word : query.plus_words) {
        if (word_to_document_id_to_term_frequency_.count(word) == 0) {
            continue;
        }
        
        const double inverse_document_frequency = ComputeWordInverseDocumentFrequency(word);
        
        for (const auto &[document_id, term_frequency] : word_to_document_id_to_term_frequency_.at(word)) {
            document_id_to_relevance[document_id] += term_frequency * inverse_document_frequency;
        }
    }
    
    for (const std::string& word : query.minus_words) {
        if (word_to_document_id_to_term_frequency_.count(word) == 0) {
            continue;
        }
        
        for (const auto &[document_id, _] : word_to_document_id_to_term_frequency_.at(word)) {
            document_id_to_relevance.erase(document_id);
        }
    }
    
    std::vector<Document> matched_documents;
    for (const auto &[document_id, relevance] : document_id_to_relevance) {
        matched_documents.push_back({ document_id, relevance,
            document_id_to_document_data_.at(document_id).rating});
    }
    
    return matched_documents;
} // FindAllDocuments

// bool SearchServer::IsValidWord(const std::string& word) {
//     // A valid word must not contain special characters
//     return none_of(word.begin(), word.end(), [](char c) {
//         return c >= '\0' && c < ' ';
//     });
// } // IsValidWord

namespace search_server_helpers {

void PrintMatchDocumentResult(int document_id, const std::vector<std::string>& words, DocumentStatus status) {
    std::cout << "{ "s
    << "document_id = "s << document_id << ", "s
    << "status = "s << static_cast<int>(status) << ", "s
    << "words ="s;
    for (const std::string& word : words) {
        std::cout << ' ' << word;
    }
    std::cout << "}"s << std::endl;
}

void AddDocument(SearchServer& search_server, int document_id, const std::string& document, DocumentStatus status,
                 const std::vector<int>& ratings) {
    try {
        search_server.AddDocument(document_id, document, status, ratings);
    } catch (const std::exception& e) {
        std::cout << "Ошибка добавления документа "s << document_id << ": "s << e.what() << std::endl;
    }
}

void FindTopDocuments(const SearchServer& search_server, const std::string& raw_query) {
    LOG_DURATION("Operation time");
    
    std::cout << "Результаты поиска по запросу: "s << raw_query << std::endl;
    
    try {
        for (const Document& document : search_server.FindTopDocuments(raw_query)) {
            PrintDocument(document);
        }
        
        std::cout << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Ошибка поиска: "s << e.what() << std::endl;
    }
}

void MatchDocuments(const SearchServer& search_server, const std::string& query) {
    LOG_DURATION_STREAM("Operation time", std::cout);
    
    try {
        std::cout << "Матчинг документов по запросу: "s << query << std::endl;
        
        for (const int document_id : search_server) {
            const auto [words, status] = search_server.MatchDocument(query, document_id);
            
            PrintMatchDocumentResult(document_id, words, status);
        }
        
    } catch (const std::exception& e) {
        std::cout << "Ошибка матчинга документов на запрос "s << query << ": "s << e.what() << std::endl;
    }
}

SearchServer CreateSearchServer(const std::string& stop_words) {
    SearchServer search_server;
    
    try {
        search_server = SearchServer(stop_words);
    } catch (const std::invalid_argument& e) {
        std::cout << "Ошибка создания search_server "s << ": "s << e.what() << std::endl;
    }
    
    return search_server;
}

} // namespace search_server_helpers
