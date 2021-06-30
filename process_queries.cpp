#include <execution>

#include "search_server.h"

std::vector<std::vector<Document>> ProcessQueries(const SearchServer& search_server,
                                                  const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> output(queries.size());

    const auto func = [&search_server](const std::string& query) {
        return search_server.FindTopDocuments(query);
    };

    std::transform(std::execution::par, queries.begin(), queries.end(), output.begin(), func);

    return output;
}