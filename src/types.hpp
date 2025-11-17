#ifndef VP_TYPES_HPP
#define VP_TYPES_HPP

#include <string>
#include <map>
#include <vector>
#include <ctime>
#include "json.hpp"

namespace vp {

using json = nlohmann::json;

// Resource represents an allocated resource
struct Resource {
    std::string type;   // tcpport|vncport|gpu|license|whatever
    std::string value;  // "3000" or "/path" or "0"
    std::string owner;  // Instance name
};

// JSON serialization for Resource
inline void to_json(json& j, const Resource& r) {
    j = json{{"type", r.type}, {"value", r.value}, {"owner", r.owner}};
}

inline void from_json(const json& j, Resource& r) {
    j.at("type").get_to(r.type);
    j.at("value").get_to(r.value);
    j.at("owner").get_to(r.owner);
}

// ResourceType defines a type of resource with validation
struct ResourceType {
    std::string name;    // Resource type name
    std::string check;   // Shell command to check availability
    bool counter;        // Is this auto-incrementing?
    int start;           // Counter start value
    int end;             // Counter end value
};

// JSON serialization for ResourceType
inline void to_json(json& j, const ResourceType& rt) {
    j = json{
        {"name", rt.name},
        {"check", rt.check},
        {"counter", rt.counter},
        {"start", rt.start},
        {"end", rt.end}
    };
}

inline void from_json(const json& j, ResourceType& rt) {
    j.at("name").get_to(rt.name);
    j.at("check").get_to(rt.check);
    j.at("counter").get_to(rt.counter);
    j.at("start").get_to(rt.start);
    j.at("end").get_to(rt.end);
}

// Template defines how to start a process
struct Template {
    std::string id;                          // Unique template ID
    std::string label;                       // Human-readable label
    std::string command;                     // Template with ${var} and %counter
    std::vector<std::string> resources;      // Resource types this needs
    std::map<std::string, std::string> vars; // Default variables
    std::string action;                      // Action to execute (URL or command)
};

// JSON serialization for Template
inline void to_json(json& j, const Template& t) {
    j = json{
        {"id", t.id},
        {"label", t.label},
        {"command", t.command},
        {"resources", t.resources},
        {"vars", t.vars}
    };
    if (!t.action.empty()) {
        j["action"] = t.action;
    }
}

inline void from_json(const json& j, Template& t) {
    j.at("id").get_to(t.id);
    j.at("label").get_to(t.label);
    j.at("command").get_to(t.command);
    j.at("resources").get_to(t.resources);
    j.at("vars").get_to(t.vars);
    if (j.contains("action")) {
        j.at("action").get_to(t.action);
    }
}

// Instance represents a running or stopped process instance
struct Instance {
    std::string name;                        // User-provided name
    std::string template_name;               // Template ID
    std::string command;                     // Final interpolated command
    int pid;                                 // Process ID
    std::string status;                      // stopped|starting|running|stopping|error
    std::map<std::string, std::string> resources; // resource_type -> value
    time_t started;                          // Unix timestamp
    std::string cwd;                         // Working directory
    bool managed;                            // true=can stop/restart, false=monitor only
    double cpu_time;                         // CPU time in seconds
    std::string error;                       // Error message if status=error
    std::string action;                      // Action to execute (URL or command)
};

// JSON serialization for Instance
inline void to_json(json& j, const Instance& i) {
    j = json{
        {"name", i.name},
        {"template", i.template_name},
        {"command", i.command},
        {"pid", i.pid},
        {"status", i.status},
        {"resources", i.resources},
        {"started", i.started},
        {"managed", i.managed}
    };
    if (!i.cwd.empty()) j["cwd"] = i.cwd;
    if (i.cpu_time > 0) j["cputime"] = i.cpu_time;
    if (!i.error.empty()) j["error"] = i.error;
    if (!i.action.empty()) j["action"] = i.action;
}

inline void from_json(const json& j, Instance& i) {
    j.at("name").get_to(i.name);
    j.at("template").get_to(i.template_name);
    j.at("command").get_to(i.command);
    j.at("pid").get_to(i.pid);
    j.at("status").get_to(i.status);
    j.at("resources").get_to(i.resources);
    j.at("started").get_to(i.started);
    j.at("managed").get_to(i.managed);

    if (j.contains("cwd")) j.at("cwd").get_to(i.cwd);
    if (j.contains("cputime")) j.at("cputime").get_to(i.cpu_time);
    if (j.contains("error")) j.at("error").get_to(i.error);
    if (j.contains("action")) j.at("action").get_to(i.action);
}

// ProcessInfo contains detailed information about a discovered process
struct ProcessInfo {
    int pid;
    int ppid;                                // Parent process ID
    std::string name;                        // Process name
    std::string cmdline;                     // Full command line
    std::string exe;                         // Executable path
    std::string cwd;                         // Working directory
    std::map<std::string, std::string> environ; // Environment variables
    std::vector<int> ports;                  // TCP ports this process listens on
    double cpu_time;                         // CPU time in seconds
};

} // namespace vp

#endif // VP_TYPES_HPP
