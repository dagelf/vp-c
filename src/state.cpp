#include "state.hpp"
#include "resource.hpp"
#include <fstream>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <pwd.h>
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

    try {
        json j;
        file >> j;

        // Load instances
        if (j.contains("instances") && j["instances"].is_object()) {
            for (auto& [key, value] : j["instances"].items()) {
                auto inst = std::make_shared<Instance>();
                *inst = value.get<Instance>();
                state->instances[key] = inst;
            }
        }

        // Load templates
        if (j.contains("templates") && j["templates"].is_object()) {
            for (auto& [key, value] : j["templates"].items()) {
                auto tmpl = std::make_shared<Template>();
                *tmpl = value.get<Template>();
                state->templates[key] = tmpl;
            }
        }

        // Load resources
        if (j.contains("resources") && j["resources"].is_object()) {
            for (auto& [key, value] : j["resources"].items()) {
                auto res = std::make_shared<Resource>();
                *res = value.get<Resource>();
                state->resources[key] = res;
            }
        }

        // Load counters
        if (j.contains("counters") && j["counters"].is_object()) {
            state->counters = j["counters"].get<std::map<std::string, int>>();
        }

        // Load types
        if (j.contains("types") && j["types"].is_object()) {
            for (auto& [key, value] : j["types"].items()) {
                auto rt = std::make_shared<ResourceType>();
                *rt = value.get<ResourceType>();
                state->types[key] = rt;
            }
        }

        // Load remotes_allowed
        if (j.contains("remotes_allowed") && j["remotes_allowed"].is_object()) {
            state->remotesAllowed = j["remotes_allowed"].get<std::map<std::string, bool>>();
        }

        // Load api_key
        if (j.contains("api_key") && j["api_key"].is_string()) {
            state->apiKey = j["api_key"].get<std::string>();
        }

    } catch (const std::exception& e) {
        std::cerr << "Error parsing state file: " << e.what() << std::endl;
        // Return default state on parse error
    }

    return state;
}

bool State::save() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string stateDir = getStateDir();

    // Create directory if it doesn't exist
    mkdir(stateDir.c_str(), 0755);

    std::string stateFile = getStateFilePath();

    try {
        json j;

        // Serialize instances
        json instances_json = json::object();
        for (const auto& [key, value] : instances) {
            instances_json[key] = *value;
        }
        j["instances"] = instances_json;

        // Serialize templates
        json templates_json = json::object();
        for (const auto& [key, value] : templates) {
            templates_json[key] = *value;
        }
        j["templates"] = templates_json;

        // Serialize resources
        json resources_json = json::object();
        for (const auto& [key, value] : resources) {
            resources_json[key] = *value;
        }
        j["resources"] = resources_json;

        // Serialize counters
        j["counters"] = counters;

        // Serialize types
        json types_json = json::object();
        for (const auto& [key, value] : types) {
            types_json[key] = *value;
        }
        j["types"] = types_json;

        // Serialize remotes_allowed
        j["remotes_allowed"] = remotesAllowed;

    // Serialize api_key
    if (!apiKey.empty()) {
        j["api_key"] = apiKey;
    }

        // Write to file
        std::ofstream file(stateFile);
        if (!file.is_open()) {
            return false;
        }

        file << j.dump(2);  // Pretty print with 2-space indent
        file.close();

        chmod(stateFile.c_str(), 0600);

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error saving state: " << e.what() << std::endl;
        return false;
    }
}

std::string State::toJson() const {
    json j;

    // Serialize instances
    json instances_json = json::object();
    for (const auto& [key, value] : instances) {
        instances_json[key] = *value;
    }
    j["instances"] = instances_json;

    // Serialize templates
    json templates_json = json::object();
    for (const auto& [key, value] : templates) {
        templates_json[key] = *value;
    }
    j["templates"] = templates_json;

    // Serialize resources
    json resources_json = json::object();
    for (const auto& [key, value] : resources) {
        resources_json[key] = *value;
    }
    j["resources"] = resources_json;

    // Serialize counters
    j["counters"] = counters;

    // Serialize types
    json types_json = json::object();
    for (const auto& [key, value] : types) {
        types_json[key] = *value;
    }
    j["types"] = types_json;

    // Serialize remotes_allowed
    j["remotes_allowed"] = remotesAllowed;

    // Serialize api_key
    if (!apiKey.empty()) {
        j["api_key"] = apiKey;
    }

    return j.dump(2);
}

bool State::fromJson(const std::string& json_str) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        json j = json::parse(json_str);

        // Load instances
        if (j.contains("instances") && j["instances"].is_object()) {
            instances.clear();
            for (auto& [key, value] : j["instances"].items()) {
                auto inst = std::make_shared<Instance>();
                *inst = value.get<Instance>();
                instances[key] = inst;
            }
        }

        // Load templates
        if (j.contains("templates") && j["templates"].is_object()) {
            templates.clear();
            for (auto& [key, value] : j["templates"].items()) {
                auto tmpl = std::make_shared<Template>();
                *tmpl = value.get<Template>();
                templates[key] = tmpl;
            }
        }

        // Load resources
        if (j.contains("resources") && j["resources"].is_object()) {
            resources.clear();
            for (auto& [key, value] : j["resources"].items()) {
                auto res = std::make_shared<Resource>();
                *res = value.get<Resource>();
                resources[key] = res;
            }
        }

        // Load counters
        if (j.contains("counters") && j["counters"].is_object()) {
            counters = j["counters"].get<std::map<std::string, int>>();
        }

        // Load types
        if (j.contains("types") && j["types"].is_object()) {
            types.clear();
            for (auto& [key, value] : j["types"].items()) {
                auto rt = std::make_shared<ResourceType>();
                *rt = value.get<ResourceType>();
                types[key] = rt;
            }
        }

        // Load remotes_allowed
        if (j.contains("remotes_allowed") && j["remotes_allowed"].is_object()) {
            remotesAllowed = j["remotes_allowed"].get<std::map<std::string, bool>>();
        }

        // Load api_key
        if (j.contains("api_key") && j["api_key"].is_string()) {
            apiKey = j["api_key"].get<std::string>();
        } else {
            apiKey.clear();
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing JSON: " << e.what() << std::endl;
        return false;
    }
}

bool State::addTemplateFromJsonString(const std::string& contents, std::string& error) {
    try {
        json j = json::parse(contents);
        auto tmpl = std::make_shared<Template>();
        *tmpl = j.get<Template>();
        templates[tmpl->id] = tmpl;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool State::deleteTemplate(const std::string& id) {
    auto it = templates.find(id);
    if (it == templates.end()) {
        return false;
    }
    templates.erase(it);
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
