#include "utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include <iostream>

std::string remove_html_tags(const std::string& html) {
    std::regex tag_regex("<[^>]*>");
    return std::regex_replace(html, tag_regex, "");
}

std::string clean_text(const std::string& text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || std::isspace(static_cast<unsigned char>(c))) {
            cleaned += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return cleaned;
}

std::map<std::string, int> count_word_frequency(const std::string& text) {
    std::map<std::string, int> freq;
    std::stringstream ss(text);
    std::string word;
    while (ss >> word) {
        if (word.length() >= 3 && word.length() <= 32) {
            freq[word]++;
        }
    }
    return freq;
}

std::vector<std::string> extract_links(const std::string& html, const std::string& base_url) {
    std::vector<std::string> urls;
    
    if (html.empty()) return urls;
    
    htmlDocPtr doc = htmlReadMemory(html.c_str(), (int)html.size(), base_url.c_str(), "UTF-8",
                                    HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | 
                                    HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
    
    if (!doc) {
        std::cerr << "Failed to parse HTML" << std::endl;
        return urls;
    }
    
    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    if (!context) {
        xmlFreeDoc(doc);
        return urls;
    }
    
    xmlXPathObjectPtr result = xmlXPathEvalExpression((const xmlChar*)"//a/@href", context);
    
    if (result && result->nodesetval) {
        xmlNodeSetPtr nodes = result->nodesetval;
        
        for (int i = 0; i < nodes->nodeNr; ++i) {
            xmlNodePtr node = nodes->nodeTab[i];
            xmlChar* href = xmlNodeGetContent(node);
            
            if (href) {
                std::string link((char*)href);
                xmlFree(href);
                
                if (link.empty() || link[0] == '#' || 
                    link.find("javascript:") == 0 || 
                    link.find("mailto:") == 0 ||
                    link.find("tel:") == 0) {
                    continue;
                }
                
                if (link.find("://") == std::string::npos) {
                    if (link[0] == '/') {
                        size_t protocol_end = base_url.find("://");
                        if (protocol_end != std::string::npos) {
                            std::string domain = base_url.substr(0, base_url.find('/', protocol_end + 3));
                            link = domain + link;
                        } else {
                            link = base_url + link;
                        }
                    } else {
                        std::string base = base_url;
                        if (base.back() != '/') base += '/';
                        link = base + link;
                    }
                }
                
                if (link.find("http://") == 0 || link.find("https://") == 0) {
                    urls.push_back(link);
                }
            }
        }
        xmlXPathFreeObject(result);
    }
    
    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);
    
    return urls;
}