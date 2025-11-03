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
<head><title>Search</title></head>
<body>
<form method="POST">
<input type="text" name="query">
<button type="submit">Search</button>
</form>
</body>
</html>
)";

std::string generate_results(const std::vector<std::pair<std::string, int>>& results) {
    if (results.empty()) return "<p>No results found.</p>";
    std::stringstream ss;
    ss << "<ul>";
    for (const auto& [url, rel] : results) {
        ss << "<li><a href=\"" << url << "\">" << url << "</a> (relevance: " << rel << ")</li>";
    }
    ss << "</ul>";
    return ss.str();
}

std::string parse_query(const std::string& body) {
    // Простой парсинг POST: query=word1+word2
    size_t pos = body.find("query=");
    if (pos == std::string::npos) return "";
    std::string query = body.substr(pos + 6);
    // Замена + на space
    std::replace(query.begin(), query.end(), '+', ' ');
    return query;
}

int main(int argc, char** argv) {
    try {
        auto config_map = parse_ini("config.ini");
        Config cfg(config_map);
        Database db(cfg);

        net::io_context ioc;
        tcp::acceptor acceptor(ioc, {tcp::v4(), static_cast<unsigned short>(std::stoi(cfg.server_port))});

        for (;;) {
            tcp::socket socket(ioc);
            acceptor.accept(socket);

            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req);

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");

            if (req.method() == http::verb::get) {
                res.body() = search_form;
            } else if (req.method() == http::verb::post) {
                std::string query_text = parse_query(req.body());
                std::string cleaned = clean_text(query_text);
                std::stringstream ss(cleaned);
                std::vector<std::string> words;
                std::string word;
                while (ss >> word && words.size() < 4) words.push_back(word);
                
                auto results = db.search(words);
                res.body() = "<!DOCTYPE html><html><head><title>Results</title></head><body>" + generate_results(results) + "</body></html>";
            } else {
                res.result(http::status::bad_request);
                res.body() = "<p>Bad request</p>";
            }

            res.prepare_payload();
            http::write(socket, res);
            socket.shutdown(tcp::socket::shutdown_send);
        }
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}