#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
inline double wallSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
class Csv {
    std::mutex mutex_;
    std::ofstream file_;
    bool milliseconds_;
public:
    Csv(const std::string& directory, const std::string& name, const std::string& header, bool milliseconds = false) : milliseconds_(milliseconds) {
        std::filesystem::create_directories(directory);
        const auto path = std::filesystem::path(directory) / name;
        bool empty = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
        file_.open(path, std::ios::app);
        if (!file_) throw std::runtime_error("Cannot open metrics: " + path.string());
        if (empty) file_ << header << '\n';
    }
    void row(const std::vector<std::string>& cells) {
        std::lock_guard lock(mutex_);
        if(milliseconds_) file_ << static_cast<long long>(wallSeconds()*1000);
        else file_ << std::to_string(wallSeconds());
        for (const auto& value : cells) {
            file_ << ",\"";
            for (char c : value) { if (c == '"') file_ << '"'; file_ << c; }
            file_ << '"';
        }
        file_ << '\n'; file_.flush();
    }
};
