#include <iostream>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "config.h"
#include "db.h"
#include "utils.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

const std::string search_form = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Search System</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        form { margin: 20px 0; }
        input[type="text"] { padding: 10px; width: 300px; font-size: 16px; }
        button { padding: 10px 20px; font-size: 16px; background: #007bff; color: white; border: none; cursor: pointer; }
        button:hover { background: #0056b3; }
        ul { list-style: none; padding: 0; }
        li { margin: 10px 0; padding: 10px; background: #f8f9fa; border-radius: 5px; }
        a { text-decoration: none; color: #007bff; }
        a:hover { text-decoration: underline; }
        .relevance { color: #666; font-size: 14px; margin-left: 10px; }
    </style>
</head>
<body>
    <h1>Search System</h1>
    <form method="POST">
        <input type="text" name="query" placeholder="Enter search query...">
        <button type="submit">Search</button>
    </form>
</body>
</html>
)";

std::string generate_results(const std::vector<std::pair<std::string, int>>& results) {
    if (results.empty()) {
        return "<p>No results found. Try different keywords.</p>";
    }
    
    std::stringstream ss;
    ss << "<h2>Search Results (" << results.size() << " found):</h2>";
    ss << "<ul>";
    for (const auto& [url, rel] : results) {
        ss << "<li>";
        ss << "<a href=\"" << url << "\" target=\"_blank\">" << url << "</a>";
        ss << "<span class=\"relevance\">(relevance: " << rel << ")</span>";
        ss << "</li>";
    }
    ss << "</ul>";
    return ss.str();
}

std::string parse_query(const std::string& body) {
    size_t pos = body.find("query=");
    if (pos == std::string::npos) return "";
    
    std::string query = body.substr(pos + 6);
    
    // Декодируем URL-encoded символы (простейшая версия)
    std::string decoded;
    for (size_t i = 0; i < query.size(); ++i) {
        if (query[i] == '+') {
            decoded += ' ';
        } else if (query[i] == '%' && i + 2 < query.size()) {
            // Простейшее декодирование %20 и подобных
            int hex;
            std::string hex_str = query.substr(i + 1, 2);
            std::istringstream(hex_str) >> std::hex >> hex;
            decoded += static_cast<char>(hex);
            i += 2;
        } else {
            decoded += query[i];
        }
    }
    
    return decoded;
}

void handle_request(tcp::socket& socket, Database& db) {
    try {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);
        
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html; charset=utf-8");
        
        if (req.method() == http::verb::get) {
            res.body() = search_form;
        } else if (req.method() == http::verb::post) {
            std::string query_text = parse_query(req.body());
            std::cout << "Search query: " << query_text << std::endl;
            
            std::string cleaned = clean_text(query_text);
            std::stringstream ss(cleaned);
            std::vector<std::string> words;
            std::string word;
            
            while (ss >> word && words.size() < 4) {
                words.push_back(word);
            }
            
            auto results = db.search(words);
            res.body() = "<!DOCTYPE html><html><head><title>Search Results</title>"
                        "<style>body { font-family: Arial, sans-serif; margin: 40px; } "
                        "ul { list-style: none; padding: 0; } "
                        "li { margin: 10px 0; padding: 10px; background: #f8f9fa; border-radius: 5px; } "
                        "a { text-decoration: none; color: #007bff; } "
                        "a:hover { text-decoration: underline; } "
                        ".relevance { color: #666; font-size: 14px; margin-left: 10px; }</style></head><body>";
            res.body() += "<h1>Search Results</h1>";
            res.body() += "<p>Query: <strong>" + query_text + "</strong></p>";
            res.body() += "<p><a href=\"/\">Back to search</a></p>";
            res.body() += generate_results(results);
            res.body() += "</body></html>";
        } else {
            res.result(http::status::bad_request);
            res.body() = "<!DOCTYPE html><html><body><h1>400 Bad Request</h1><p>Invalid HTTP method.</p></body></html>";
        }
        
        res.prepare_payload();
        http::write(socket, res);
        
        socket.shutdown(tcp::socket::shutdown_send);
        
    } catch (const std::exception& e) {
        std::cerr << "Request handling error: " << e.what() << std::endl;
    }
}

int main(int argc, char** argv) {
    try {
        auto config_map = parse_ini("config.ini");
        Config cfg(config_map);
        Database db(cfg);
        
        net::io_context ioc;
        tcp::acceptor acceptor(ioc, {tcp::v4(), static_cast<unsigned short>(std::stoi(cfg.server_port))});
        
        std::cout << "Search server started on port " << cfg.server_port << std::endl;
        std::cout << "Open browser and navigate to: http://localhost:" << cfg.server_port << std::endl;
        
        for (;;) {
            tcp::socket socket(ioc);
            acceptor.accept(socket);
            handle_request(socket, db);
        }
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}