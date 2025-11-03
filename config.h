#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <map>

std::map<std::string, std::string> parse_ini(const std::string& filename);

struct Config {
    std::string db_host;
    std::string db_port;
    std::string db_name;
    std::string db_user;
    std::string db_password;
    std::string start_page;
    int recursion_depth;
    std::string server_port;
    
    Config(const std::map<std::string, std::string>& m);
};

#endif