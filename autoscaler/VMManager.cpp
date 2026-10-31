#include "VMManager.hpp"
#include <stdexcept>
void VMManager::put(const Backend& b) {
    std::lock_guard lock(mutex_);
    auto it = backends_.find(b.name);
    if (it != backends_.end() && it->second.inFlight) throw std::logic_error("Replacing busy backend");
    backends_[b.name] = b; ++version_;
}
void VMManager::erase(const std::string& name) {
    std::lock_guard lock(mutex_);
    auto it = backends_.find(name);
    if (it != backends_.end() && it->second.inFlight) throw std::logic_error("Erasing busy backend");
    backends_.erase(name); ++version_;
}
void VMManager::state(const std::string& name, const std::string& value) {
    std::lock_guard lock(mutex_);
    auto& b = backends_.at(name);
    if (b.state != value) { b.state = value; ++version_; }
}
std::vector<Backend> VMManager::snapshot() const {
    std::lock_guard lock(mutex_); std::vector<Backend> out;
    for (const auto& [_,b] : backends_) out.push_back(b);
    return out;
}
std::optional<Backend> VMManager::acquire() {
    std::lock_guard lock(mutex_); std::vector<Backend*> ready;
    for (auto& [_,b] : backends_) if (b.state == "ready") ready.push_back(&b);
    if (ready.empty()) return {};
    auto* selected = ready[next_++ % ready.size()]; ++selected->inFlight; return *selected;
}
void VMManager::release(const std::string& name) {
    std::lock_guard lock(mutex_);
    auto& b = backends_.at(name);
    if (!b.inFlight) std::terminate();
    --b.inFlight; drained_.notify_all();
}
bool VMManager::drain(const std::string& name, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    auto& b = backends_.at(name); b.state = "draining"; ++version_;
    return drained_.wait_for(lock, timeout, [&] { return backends_.at(name).inFlight == 0; });
}
nlohmann::json VMManager::membership() const {
    std::lock_guard lock(mutex_);
    auto ready = nlohmann::json::array(); auto all = nlohmann::json::array();
    for (const auto& [_,b] : backends_) {
        nlohmann::json item = {{"name",b.name},{"ip",b.ip},{"state",b.state},{"in_flight",b.inFlight}};
        all.push_back(item); if (b.state == "ready") ready.push_back(item);
    }
    return {{"version", version_},{"replicas", ready},{"backends",all}};
}
