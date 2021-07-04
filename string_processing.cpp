#include <sstream>

#include "string_processing.h"

namespace string_processing {

std::vector<std::string> SplitIntoWords(const std::string& text) {
    std::istringstream text_stream(text);
    
    std::vector<std::string> words;
    
    std::string word;
    while (text_stream >> word) {
        words.push_back(word);
    }
    
    return words;
}

std::vector<std::string_view> SplitIntoWords(std::string_view text) {
    std::vector<std::string_view> output;

    while (true) {
        size_t space_index = text.find(' ');
        output.push_back(text.substr(0, space_index));

        if (space_index == text.npos) {
            break;
        } else {
            text.remove_prefix(space_index + 1);
        }
    }

    return output;
}

} // string_processing
