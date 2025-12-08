#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <map>
#include <vector>
#include <regex>

std::string remove_html_tags(const std::string& html);
std::string clean_text(const std::string& text);
std::map<std::string, int> count_word_frequency(const std::string& text);
std::vector<std::string> extract_links(const std::string& html, const std::string& base_url);
std::string get_base_url(const std::string& url);

#endif