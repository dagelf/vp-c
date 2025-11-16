#include "state.hpp"
#include "resource.hpp"
#include "json_simple.hpp"
#include <fstream>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <pwd.h>
#include <sstream>
#include <iostream>

namespace vp {

State::State() : inotify_fd_(-1), watch_fd_(-1) {
    loadDefaultTemplates();
    loadDefaultResourceTypes();
}

State::~State() {
    if (watch_fd_ != -1) {
        inotify_rm_watch(inotify_fd_, watch_fd_);
    }
    if (inotify_fd_ != -1) {
        close(inotify_fd_);
    }
}

std::string State::getStateDir() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        } else {
            home = "/tmp";
        }
    }
    return std::string(home) + "/.vibeprocess";
}

std::string State::getStateFilePath() {
    return getStateDir() + "/state.json";
}

std::shared_ptr<State> State::load() {
    auto state = std::make_shared<State>();

    std::string stateFile = getStateFilePath();
    std::ifstream file(stateFile);

    if (!file.is_open()) {
        // Return defaults if file doesn't exist
        return state;
    }

    std::string json((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

    // For simplicity, we'll skip JSON parsing in this minimal implementation
    // In production, use nlohmann/json or similar
    // state->fromJson(json);

    return state;
}

bool State::save() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string stateDir = getStateDir();

    // Create directory if it doesn't exist
    mkdir(stateDir.c_str(), 0755);

    std::string stateFile = getStateFilePath();

    // Generate JSON (simplified - use proper library in production)
    std::string json = toJson();

    std::ofstream file(stateFile);
    if (!file.is_open()) {
        return false;
    }

    file << json;
    file.close();

    chmod(stateFile.c_str(), 0600);

    return true;
}

std::string State::toJson() const {
    // Simplified JSON serialization
    // In production, use nlohmann/json or similar
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"instances\": {},\n";
    oss << "  \"templates\": {},\n";
    oss << "  \"resources\": {},\n";
    oss << "  \"counters\": " << json::toJson(counters) << ",\n";
    oss << "  \"types\": {},\n";
    oss << "  \"remotes_allowed\": " << json::toJson(remotesAllowed) << "\n";
    oss << "}\n";
    return oss.str();
}

bool State::fromJson(const std::string& json) {
    // Simplified JSON parsing
    // In production, use nlohmann/json or similar
    (void)json; // Suppress unused warning
    return true;
}

void State::claimResource(const std::string& rtype, const std::string& value, const std::string& owner) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string key = rtype + ":" + value;
    auto res = std::make_shared<Resource>();
    res->type = rtype;
    res->value = value;
    res->owner = owner;
    resources[key] = res;
}

void State::releaseResources(const std::string& owner) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = resources.begin();
    while (it != resources.end()) {
        if (it->second->owner == owner) {
            it = resources.erase(it);
        } else {
            ++it;
        }
    }
}

void State::loadDefaultTemplates() {
    auto postgres = std::make_shared<Template>();
    postgres->id = "postgres";
    postgres->label = "PostgreSQL Database";
    postgres->command = "postgres -D ${datadir} -p ${tcpport}";
    postgres->resources = {"tcpport", "datadir"};
    postgres->vars["datadir"] = "/tmp/pgdata";
    templates["postgres"] = postgres;

    auto nodeExpress = std::make_shared<Template>();
    nodeExpress->id = "node-express";
    nodeExpress->label = "Node.js Express Server";
    nodeExpress->command = "node server.js --port ${tcpport}";
    nodeExpress->resources = {"tcpport"};
    templates["node-express"] = nodeExpress;

    auto qemu = std::make_shared<Template>();
    qemu->id = "qemu";
    qemu->label = "QEMU Virtual Machine";
    qemu->command = "qemu-system-x86_64 -vnc :${vncport} -serial tcp::${serialport},server,nowait ${args}";
    qemu->resources = {"vncport", "serialport"};
    qemu->vars["args"] = "-m 2G";
    templates["qemu"] = qemu;
}

void State::loadDefaultResourceTypes() {
    types = defaultResourceTypes();
}

bool State::watchConfig() {
    inotify_fd_ = inotify_init();
    if (inotify_fd_ == -1) {
        return false;
    }

    std::string stateFile = getStateFilePath();
    std::string stateDir = getStateDir();

    // Try to watch the file first
    watch_fd_ = inotify_add_watch(inotify_fd_, stateFile.c_str(), IN_MODIFY | IN_CREATE);

    if (watch_fd_ == -1) {
        // If file doesn't exist, watch the directory
        mkdir(stateDir.c_str(), 0755);
        watch_fd_ = inotify_add_watch(inotify_fd_, stateDir.c_str(), IN_MODIFY | IN_CREATE);

        if (watch_fd_ == -1) {
            close(inotify_fd_);
            inotify_fd_ = -1;
            return false;
        }
    }

    // Start watcher thread
    // TODO: Implement file watching thread
    // For now, just return success

    return true;
}

} // namespace vp
