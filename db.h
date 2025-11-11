#ifndef DB_H
#define DB_H

#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <map>
#include "config.h"

class Database {
private:
    pqxx::connection conn;
public:
    Database(const Config& cfg);
    void create_tables();
    bool doc_exists(const std::string& url);
    int get_or_insert_doc(const std::string& url);
    int get_or_insert_word(const std::string& word);
    void insert_frequency(int word_id, int doc_id, int freq);
    std::vector<std::pair<std::string, int>> search(const std::vector<std::string>& words);
};

#endif