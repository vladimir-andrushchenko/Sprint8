#pragma once

#include <vector>
#include <string>
#include <string_view>

namespace string_processing {

std::vector<std::string> SplitIntoWords(const std::string& text);

std::vector<std::string_view> SplitIntoWords(std::string_view text);

}


