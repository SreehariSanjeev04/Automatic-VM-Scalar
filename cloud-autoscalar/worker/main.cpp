#include "httplib.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace {
constexpr int maxInputDepth = 100;

struct Tree {
    std::unique_ptr<Tree> left;
    std::unique_ptr<Tree> right;

    explicit Tree(int depth) {
        if (depth > 0) {
            left = std::make_unique<Tree>(depth - 1);
            right = std::make_unique<Tree>(depth - 1);
        }
    }

    std::uint64_t check() const {
        return left ? 1 + left->check() + right->check() : 1;
    }
};

void work(const httplib::Request& request, httplib::Response& response) {
    const char* parameter = request.path == "/binary-trees" ? "n" : "value";
    const std::string input = request.get_param_value(parameter);
    int depth = 0;
    auto parsed = std::from_chars(input.data(), input.data() + input.size(), depth);
    if (input.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != input.data() + input.size() || depth < 0 || depth > maxInputDepth) {
        response.status = 400;
        response.set_content("{\"error\":\"Expected an integer from 0 to " +
                             std::to_string(maxInputDepth) + "\"}",
                             "application/json");
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    std::uint64_t checksum = 0;
    try {
        // Allocate the complete requested tree and retain it for traversal.
        Tree tree(depth);
        checksum = tree.check();
    } catch (const std::bad_alloc&) {
        response.status = 503;
        response.set_content("{\"error\":\"Insufficient memory for requested tree\"}",
                             "application/json");
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    response.set_content("{\"status\":\"success\",\"depth\":" +
        std::to_string(depth) + ",\"tree_depth\":" + std::to_string(depth) +
        ",\"repetitions\":1" +
        ",\"checksum\":" + std::to_string(checksum) +
        ",\"elapsed_us\":" + std::to_string(elapsed) + "}", "application/json");
}
}

int main() {
    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(8); };
    server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("{\"status\":\"ok\"}", "application/json");
    });
    server.Get("/binary-trees", work);
    server.Get("/work", work);
    std::cout << "Worker listening on 0.0.0.0:8080" << std::endl;
    if (!server.listen("0.0.0.0", 8080)) {
        std::cerr << "Failed to listen on port 8080\n";
        return 1;
    }
}
