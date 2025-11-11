#include "utils.h"
#include <algorithm>
#include <cctype>
#include <pugixml.hpp>
#include <sstream>

std::string remove_html_tags(const std::string& html) {
    std::regex tag_regex("<[^>]*>");
    return std::regex_replace(html, tag_regex, "");
}

std::string clean_text(const std::string& text) {
    std::string cleaned;
    for (unsigned char c : text) {
        if (std::isalnum(c) || std::isspace(c)) {
            cleaned += std::tolower(c);
        }
    }
    return cleaned;
}

std::map<std::string, int> count_word_frequency(const std::string& text) {
    std::map<std::string, int> freq;
    std::stringstream ss(text);
    std::string word;
    while (ss >> word) {
        if (word.length() >= 3 && word.length() <= 32) freq[word]++;
    }
    return freq;
}

std::vector<std::string> extract_links(const std::string& html, const std::string& base_url) {
    pugi::xml_document doc;
    doc.load_string(html.c_str());
    pugi::xpath_node_set links = doc.select_nodes("//a/@href");
    std::vector<std::string> urls;
    for (auto node : links) {
        std::string href = node.attribute().value();
        if (href.empty() || href[0] == '#') continue;
        if (href[0] == '/') href = base_url + href;
        urls.push_back(href);
    }
    return urls;
}