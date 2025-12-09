#ifndef VP_PROCESS_HPP
#define VP_PROCESS_HPP

#include "types.hpp"
#include "state.hpp"
#include <memory>
#include <vector>
#include <map>

namespace vp {

// Start a process from a template
std::shared_ptr<Instance> startProcess(
    std::shared_ptr<State> state,
    const Template& tmpl,
    const std::string& name,
    const std::map<std::string, std::string>& vars
);

// Stop a running process
bool stopProcess(std::shared_ptr<State> state, std::shared_ptr<Instance> inst);

// Restart a stopped process
bool restartProcess(std::shared_ptr<State> state, std::shared_ptr<Instance> inst);

// Monitor an existing process (add as monitored instance)
std::shared_ptr<Instance> monitorProcess(std::shared_ptr<State> state, int pid, const std::string& name);

// Check if a process is running
bool isProcessRunning(int pid);

// Discover and import a process by PID
std::shared_ptr<Instance> discoverAndImportProcess(std::shared_ptr<State> state, int pid, const std::string& name);

// Discover and import a process on a port
std::shared_ptr<Instance> discoverAndImportProcessOnPort(std::shared_ptr<State> state, int port, const std::string& name);

// Discover running processes
// includePorts: if true, also scan for listening ports (expensive)
// portsOnly: if true, only return processes that have listening ports
// maxAgeSeconds: maximum age of cached results to use (0 = force refresh)
std::vector<std::map<std::string, std::string>> discoverProcesses(std::shared_ptr<State> state, bool includePorts, bool portsOnly, int maxAgeSeconds = 2);

// Match stopped instances with running processes and update status
// preDiscovered: optional pointer to already discovered processes to use
bool matchAndUpdateInstances(std::shared_ptr<State> state, const std::vector<std::map<std::string, std::string>>* preDiscovered = nullptr);

// Execute an action command
bool executeAction(const std::string& action);

// Extract process name from command
std::string extractProcessName(const std::string& command);

// Check if we can manage a process
bool canManageProcess(int pid);

// Generate a unique instance ID (name-timestamp or name-timestamp-N if collision)
std::string generateInstanceId(std::shared_ptr<State> state, const std::string& name);

} // namespace vp

#endif // VP_PROCESS_HPP
