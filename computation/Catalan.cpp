#include "httplib.h"
#include <iostream>
#include <string>
#include <chrono>

long long countBinaryTreesNaive(int n) {
    if (n <= 0) return 1;

    long long total = 0;
    for (int i = 0; i < n; ++i) {
        // Splitting n-1 remaining nodes between left (i) and right (n - 1 - i) subtrees
        total += countBinaryTreesNaive(i) * countBinaryTreesNaive(n - 1 - i);
    }
    return total;
}

int main() {
    httplib::Server svr;

    svr.Get("/binary-trees", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("n")) {
            std::cout << "[LOG] [ERROR] Request received without query parameter 'n'" << std::endl;
            res.status = 400;
            res.set_content("Missing parameter 'n'", "text/plain");
            return;
        }

        int n = std::stoi(req.get_param_value("n"));
        std::cout << "[LOG] Request received | Input n = " << n << std::endl;
        std::cout << "[LOG] Starting calculation..." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();
        
        long long result = countBinaryTreesNaive(n);
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;

        std::cout << "[LOG] Calculation complete | Output = " << result 
                  << " | Execution Time: " << duration.count() << " ms" << std::endl;

        res.set_content(std::to_string(result), "text/plain");
    });

    std::cout << "[LOG] Server running on http://localhost:8080..." << std::endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}
