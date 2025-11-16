#ifndef VP_TYPES_HPP
#define VP_TYPES_HPP

#include <string>
#include <map>
#include <vector>
#include <ctime>

namespace vp {

// Resource represents an allocated resource
struct Resource {
    std::string type;   // tcpport|vncport|gpu|license|whatever
    std::string value;  // "3000" or "/path" or "0"
    std::string owner;  // Instance name
};

// ResourceType defines a type of resource with validation
struct ResourceType {
    std::string name;    // Resource type name
    std::string check;   // Shell command to check availability
    bool counter;        // Is this auto-incrementing?
    int start;           // Counter start value
    int end;             // Counter end value
};

// Template defines how to start a process
struct Template {
    std::string id;                          // Unique template ID
    std::string label;                       // Human-readable label
    std::string command;                     // Template with ${var} and %counter
    std::vector<std::string> resources;      // Resource types this needs
    std::map<std::string, std::string> vars; // Default variables
    std::string action;                      // Action to execute (URL or command)
};

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
