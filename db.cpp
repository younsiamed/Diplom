#include "db.h"
#include <sstream>
#include <stdexcept>
#include <pqxx/pqxx>

Database::Database(const Config& cfg) : conn(
    "dbname=" + cfg.db_name + " user=" + cfg.db_user + " password=" + cfg.db_password +
    " host=" + cfg.db_host + " port=" + cfg.db_port) {
    if (!conn.is_open()) throw std::runtime_error("DB connection failed");
}

void Database::create_tables() {
    pqxx::work txn(conn);
    txn.exec("CREATE TABLE IF NOT EXISTS documents (id SERIAL PRIMARY KEY, url TEXT UNIQUE);");
    txn.exec("CREATE TABLE IF NOT EXISTS words (id SERIAL PRIMARY KEY, word TEXT UNIQUE);");
    txn.exec("CREATE TABLE IF NOT EXISTS word_doc (word_id INT, doc_id INT, frequency INT, PRIMARY KEY(word_id, doc_id));");
    txn.commit();
}

int Database::get_or_insert_doc(const std::string& url) {
    pqxx::work txn(conn);
    pqxx::result res = txn.exec("SELECT id FROM documents WHERE url = " + txn.quote(url));
    if (!res.empty()) return res[0][0].as<int>();
    res = txn.exec("INSERT INTO documents (url) VALUES (" + txn.quote(url) + ") RETURNING id");
    txn.commit();
    return res[0][0].as<int>();
}

int Database::get_or_insert_word(const std::string& word) {
    pqxx::work txn(conn);
    pqxx::result res = txn.exec("SELECT id FROM words WHERE word = " + txn.quote(word));
    if (!res.empty()) return res[0][0].as<int>();
    res = txn.exec("INSERT INTO words (word) VALUES (" + txn.quote(word) + ") RETURNING id");
    txn.commit();
    return res[0][0].as<int>();
}

void Database::insert_frequency(int word_id, int doc_id, int freq) {
    pqxx::work txn(conn);
    txn.exec("INSERT INTO word_doc (word_id, doc_id, frequency) VALUES (" +
             std::to_string(word_id) + ", " + std::to_string(doc_id) + ", " + 
             std::to_string(freq) + ") ON CONFLICT (word_id, doc_id) DO UPDATE SET frequency = EXCLUDED.frequency");
    txn.commit();
}

std::vector<std::pair<std::string, int>> Database::search(const std::vector<std::string>& query_words) {
    if (query_words.empty() || query_words.size() > 4) return {};
    pqxx::work txn(conn);
    
    std::stringstream ss;
    ss << "SELECT d.url, SUM(wd.frequency) as rel "
       << "FROM documents d "
       << "JOIN word_doc wd ON d.id = wd.doc_id "
       << "JOIN words w ON w.id = wd.word_id "
       << "WHERE w.word IN (";
    
    for (size_t i = 0; i < query_words.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << txn.quote(query_words[i]);
    }
    
    ss << ") GROUP BY d.id "
       << "HAVING COUNT(DISTINCT w.word) = " << query_words.size()
       << " ORDER BY rel DESC LIMIT 10;";
    
    pqxx::result res = txn.exec(ss.str());
    std::vector<std::pair<std::string, int>> results;
    for (auto row : res) {
        results.emplace_back(row[0].as<std::string>(), row[1].as<int>());
    }
    return results;
}