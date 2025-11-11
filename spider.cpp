#include <iostream>
#include <set>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <thread>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/err.h>
#include "config.h"
#include "db.h"
#include "utils.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class ThreadPool {
public:
    ThreadPool(size_t threads = std::thread::hardware_concurrency()) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }
    ~ThreadPool() {
        { std::unique_lock<std::mutex> lock(queue_mutex); stop = true; }
        condition.notify_all();
        for (auto& worker : workers) worker.join();
    }
    void enqueue(std::function<void()> task) {
        { std::unique_lock<std::mutex> lock(queue_mutex); tasks.push(std::move(task)); }
        condition.notify_one();
    }
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop = false;
};

std::string download_page(const std::string& url, int redirect_count = 0) {
    if (redirect_count > 5) throw std::runtime_error("Too many redirects");

    size_t pos = url.find("://");
    if (pos == std::string::npos) throw std::runtime_error("Invalid URL");
    std::string protocol = url.substr(0, pos);
    std::string rest = url.substr(pos + 3);
    pos = rest.find('/');
    std::string host = rest.substr(0, pos);
    std::string target = (pos == std::string::npos) ? "/" : rest.substr(pos);
    std::string port = (protocol == "https") ? "443" : "80";

    net::io_context ioc;
    tcp::resolver resolver(ioc);
    auto const results = resolver.resolve(host, port);

    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    beast::flat_buffer buffer;
    http::response<http::dynamic_body> res;

    if (protocol == "https") {
        ssl::context ctx{ssl::context::tlsv12_client};
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            throw beast::system_error(
                beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                "SSL_set_tlsext_host_name");
        }
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);
        http::write(stream, req);
        http::read(stream, buffer, res);
        beast::error_code ec;
        stream.shutdown(ec);
        if (ec == net::error::eof) ec = {};
        if (ec) throw beast::system_error{ec};
    } else if (protocol == "http") {
        beast::tcp_stream stream(ioc);
        stream.connect(results);
        http::write(stream, req);
        http::read(stream, buffer, res);
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        if (ec && ec != net::error::eof) throw beast::system_error{ec};
    } else {
        throw std::runtime_error("Unsupported protocol");
    }

    if (res.result() == http::status::moved_permanently || res.result() == http::status::found ||
        res.result() == http::status::see_other || res.result() == http::status::temporary_redirect ||
        res.result() == http::status::permanent_redirect) {
        auto location_it = res.find(http::field::location);
        if (location_it != res.end()) {
            std::string new_url = std::string(location_it->value());
            if (new_url[0] == '/') new_url = protocol + "://" + host + new_url;
            return download_page(new_url, redirect_count + 1);
        } else {
            throw std::runtime_error("Redirect without Location header");
        }
    }

    return beast::buffers_to_string(res.body().data());
}

int main(int argc, char** argv) {
    try {
        auto config_map = parse_ini("config.ini");
        Config cfg(config_map);
        Database db(cfg);  // Основной DB для create_tables
        db.create_tables();

        ThreadPool pool(4);

        std::function<void(const std::string&, int)> process_url;
        process_url = [&](const std::string& url, int depth) {
            Database local_db(cfg);  // Локальный DB для потока

            if (depth > cfg.recursion_depth) return;
            if (local_db.doc_exists(url)) return;

            std::string html = download_page(url);
            std::string text = clean_text(remove_html_tags(html));
            auto freq = count_word_frequency(text);
            int doc_id = local_db.get_or_insert_doc(url);

            for (const auto& [word, count] : freq) {
                int word_id = local_db.get_or_insert_word(word);
                local_db.insert_frequency(word_id, doc_id, count);
            }

            if (depth < cfg.recursion_depth) {
                auto links = extract_links(html, url.substr(0, url.find('/', url.find("://") + 3)));
                for (const auto& link : links) {
                    pool.enqueue([=] { process_url(link, depth + 1); });
                }
            }
        };

        pool.enqueue([&] { process_url(cfg.start_page, 1); });
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}