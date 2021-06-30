#pragma once

#include <vector>
#include <string>
#include <numeric>

#include "search_server.h"

std::vector<std::vector<Document>> ProcessQueries(
                                                  const SearchServer& search_server,
                                                  const std::vector<std::string>& queries);

std::vector<Document> ProcessQueriesJoined(const SearchServer& search_server,
                                           const std::vector<std::string>& queries) {
        
    const auto documents = ProcessQueries(search_server, queries);

    const auto func = [](std::vector<Document> first, std::vector<Document> second){
        for (const Document document : second) {
            first.push_back(document);
        }
        return first;
    };

    return std::reduce(documents.begin(), documents.end(), std::vector<Document>{}, func);
}