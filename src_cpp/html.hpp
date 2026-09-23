#pragma once

#include <string>
#include <vector>

namespace kos {

struct ParsedHtml {
    std::string title;
    std::string body_text;
    std::string wiki_text;
    std::string footer_blob;
    std::string last_modified_meta;
    std::string time_datetime;
    std::vector<std::string> hrefs;
    std::vector<std::string> category_texts;
};

ParsedHtml parse_html(const std::string& html);
std::string strip_tags_text(const std::string& html);

}  // namespace kos
