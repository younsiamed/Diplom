#include "config.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

std::map<std::string, std::string> parse_ini(const std::string& filename) {
    std::map<std::string, std::string> config;
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("Cannot open config file");
    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            if (!value.empty() && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }
            
            config[key] = value;
        }
    }
    return config;
}

Config::Config(const std::map<std::string, std::string>& m) {
    db_host = m.at("db_host");
    db_port = m.at("db_port");
    db_name = m.at("db_name");
    db_user = m.at("db_user");
    db_password = m.at("db_password");
    start_page = m.at("start_page");
    recursion_depth = std::stoi(m.at("recursion_depth"));
    server_port = m.at("server_port");
}