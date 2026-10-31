#include "httplib.h"
#include "Catalan.hpp"
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
int main() {
    httplib::Server server;
    unsigned threads = 8;
    if (const char* value = std::getenv("WORKER_THREADS")) {
        auto end = value + std::char_traits<char>::length(value);
        auto parsed = std::from_chars(value, end, threads);
        if (parsed.ec != std::errc{} || parsed.ptr != end || threads == 0 || threads > 128) return 1;
    }
    server.new_task_queue = [threads] { return new httplib::ThreadPool(threads); };
    server.Get("/health", [](const auto&, auto& response) { response.set_content("{\"status\":\"ok\",\"workload\":\"catalan\"}", "application/json"); });
    server.Get("/binary-trees", [](const httplib::Request& request, httplib::Response& response) {
        auto value = request.get_param_value("n"); int n = -1;
        auto parsed = std::from_chars(value.data(), value.data()+value.size(), n);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || n < 0 || n > 100) {
            response.status = 400; response.set_content("Expected integer n from 0 to 100", "text/plain"); return;
        }
        try {
            auto start = std::chrono::steady_clock::now();
            auto result = countBinaryTreesNaive(n).str();
            std::cout << "n=" << n << " elapsed_us=" << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count() << std::endl;
            response.set_content(result, "text/plain");
        } catch (const std::bad_alloc&) {
            response.status = 503; response.set_content("Insufficient memory", "text/plain");
        }
    });
    std::cout << "Catalan server listening on 0.0.0.0:8080" << std::endl;
    return server.listen("0.0.0.0", 8080) ? 0 : 1;
}
