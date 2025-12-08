#include <iostream>
#include <set>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <thread>
#include <chrono>
#include <atomic>
#include <future>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/err.h>
#include <libxml/parser.h>
#include <zlib.h>
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
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    template<class F>
    void enqueue(F&& task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) return;
            tasks.emplace(std::forward<F>(task));
        }
        condition.notify_one();
    }
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop = false;
};

// Функция для распаковки gzip
std::string decompress_gzip(const std::string& compressed) {
    if (compressed.size() <= 4) return compressed;
    
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        return compressed;
    }
    
    zs.next_in = (Bytef*)compressed.data();
    zs.avail_in = (uInt)compressed.size();
    
    int ret;
    char outbuffer[32768];
    std::string outstring;
    
    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        
        ret = inflate(&zs, 0);
        
        if (outstring.size() < zs.total_out) {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);
    
    inflateEnd(&zs);
    
    if (ret != Z_STREAM_END) {
        std::cerr << "Gzip decompression error: " << ret << std::endl;
        return compressed;
    }
    
    return outstring;
}

// Получение домена из URL
std::string get_domain(const std::string& url) {
    size_t start = url.find("://");
    if (start == std::string::npos) return "";
    
    start += 3;
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.length();
    
    return url.substr(start, end - start);
}

std::string download_page(const std::string& url) {
    try {
        size_t pos = url.find("://");
        if (pos == std::string::npos) {
            throw std::runtime_error("Invalid URL: missing protocol");
        }
        
        std::string protocol = url.substr(0, pos);
        std::string rest = url.substr(pos + 3);
        
        size_t path_pos = rest.find('/');
        std::string host = (path_pos == std::string::npos) ? rest : rest.substr(0, path_pos);
        std::string target = (path_pos == std::string::npos) ? "/" : rest.substr(path_pos);
        
        std::string port = (protocol == "https") ? "443" : "80";
        
        size_t colon_pos = host.find(':');
        if (colon_pos != std::string::npos) {
            port = host.substr(colon_pos + 1);
            host = host.substr(0, colon_pos);
        }
        
        net::io_context ioc;
        
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        req.set(http::field::accept, "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
        req.set(http::field::connection, "close");
        
        if (protocol == "https") {
            ssl::context ctx{ssl::context::tls_client};
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_none);
            
            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
            
            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                beast::error_code ec{static_cast<int>(::ERR_get_error()),
                                    net::error::get_ssl_category()};
                throw beast::system_error{ec};
            }
            
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(host, port);
            
            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
            
            beast::get_lowest_layer(stream).connect(results);
            stream.handshake(ssl::stream_base::client);
            
            http::write(stream, req);
            
            beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            http::read(stream, buffer, res);
            
            std::string body = beast::buffers_to_string(res.body().data());
            
            auto encoding = res.find(http::field::content_encoding);
            if (encoding != res.end()) {
                std::string encoding_str = std::string(encoding->value());
                if (encoding_str.find("gzip") != std::string::npos || 
                    encoding_str.find("deflate") != std::string::npos) {
                    body = decompress_gzip(body);
                }
            }
            
            beast::error_code ec;
            stream.shutdown(ec);
            
            if (ec && ec != net::error::eof && 
                ec != beast::errc::not_connected && 
                !ec.message().empty()) {
                std::cerr << "SSL shutdown warning: " << ec.message() << std::endl;
            }
            
            return body;
        } else {
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(host, port);
            
            beast::tcp_stream stream(ioc);
            stream.expires_after(std::chrono::seconds(10));
            
            stream.connect(results);
            http::write(stream, req);
            
            beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            http::read(stream, buffer, res);
            
            std::string body = beast::buffers_to_string(res.body().data());
            
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            
            if (ec && ec != net::error::eof) {
                std::cerr << "HTTP shutdown warning: " << ec.message() << std::endl;
            }
            
            return body;
        }
    } catch (const std::exception& e) {
        std::cerr << "Download error for " << url << ": " << e.what() << std::endl;
        return "";
    }
}

int main(int argc, char** argv) {
    try {
        xmlInitParser();
        
        auto config_map = parse_ini("config.ini");
        Config cfg(config_map);
        Database db(cfg);
        db.create_tables();
        
        std::cout << "Starting spider with URL: " << cfg.start_page << std::endl;
        std::cout << "Recursion depth: " << cfg.recursion_depth << std::endl;
        
        ThreadPool pool(2);
        
        std::set<std::string> visited;
        std::mutex visited_mutex;
        std::set<std::string> domains_allowed;
        
        std::string start_domain = get_domain(cfg.start_page);
        domains_allowed.insert(start_domain);
        std::cout << "Allowed domain: " << start_domain << std::endl;
        
        std::atomic<int> processed_count{0};
        std::atomic<int> error_count{0};
        std::atomic<int> tasks_in_progress{0};
        std::promise<void> all_done;
        std::future<void> all_done_future = all_done.get_future();
        
        std::function<void(const std::string&, int)> process_url;
        process_url = [&](const std::string& url, int depth) {
            tasks_in_progress++;
            
            auto task_cleanup = [&]() {
                if (--tasks_in_progress == 0) {
                    all_done.set_value();
                }
            };
            
            if (depth > cfg.recursion_depth) {
                task_cleanup();
                return;
            }
            
            {
                std::lock_guard<std::mutex> lock(visited_mutex);
                if (visited.count(url)) {
                    task_cleanup();
                    return;
                }
                visited.insert(url);
            }
            
            std::string domain = get_domain(url);
            if (domains_allowed.count(domain) == 0) {
                std::cout << "Skipping URL from different domain: " << url << std::endl;
                task_cleanup();
                return;
            }
            
            try {
                std::cout << "Processing [" << depth << "]: " << url << std::endl;
                
                std::string html = download_page(url);
                
                if (!html.empty() && html.size() > 100) {
                    std::string text = remove_html_tags(html);
                    std::string cleaned_text = clean_text(text);
                    auto freq = count_word_frequency(cleaned_text);
                    
                    if (!freq.empty()) {
                        int doc_id = db.get_or_insert_doc(url);
                        std::cout << "Saving " << freq.size() << " words for document " << doc_id << std::endl;
                        
                        for (const auto& [word, count] : freq) {
                            int word_id = db.get_or_insert_word(word);
                            db.insert_frequency(word_id, doc_id, count);
                        }
                        
                        processed_count++;
                        std::cout << "Processed " << processed_count << " pages. Words: " << freq.size() << std::endl;
                    } else {
                        std::cout << "No words found in " << url << std::endl;
                    }
                    
                    if (depth < cfg.recursion_depth) {
                        auto links = extract_links(html, url);
                        
                        if (!links.empty()) {
                            std::cout << "Found " << links.size() << " links on " << url << std::endl;
                            
                            int link_limit = 5;
                            for (const auto& link : links) {
                                if (link_limit-- <= 0) break;
                                
                                std::string link_domain = get_domain(link);
                                if (domains_allowed.count(link_domain) > 0) {
                                    pool.enqueue([=, &process_url] { 
                                        process_url(link, depth + 1); 
                                    });
                                }
                            }
                        }
                    }
                } else {
                    error_count++;
                    std::cerr << "Failed to download or empty HTML: " << url << std::endl;
                }
            } catch (const std::exception& e) {
                error_count++;
                std::cerr << "Error processing " << url << ": " << e.what() << std::endl;
            }
            
            task_cleanup();
        };
        
        pool.enqueue([&] { 
            process_url(cfg.start_page, 1); 
        });
        
        all_done_future.wait();
        
        std::cout << "\n=== Spider finished ===" << std::endl;
        std::cout << "Total pages processed: " << processed_count << std::endl;
        std::cout << "Errors: " << error_count << std::endl;
        std::cout << "Unique URLs visited: " << visited.size() << std::endl;
        
        xmlCleanupParser();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        xmlCleanupParser();
        return 1;
    }
    
    return 0;
}