#ifndef VP_PROCUTIL_HPP
#define VP_PROCUTIL_HPP

#include "types.hpp"
#include <vector>
#include <map>
#include <memory>

namespace vp {

// Shell names for common shells
extern const std::map<std::string, bool> SHELL_NAMES;

// Build a map of all listening ports to PIDs
std::map<int, std::vector<int>> buildPortToProcessMap();

// Read process information from /proc/[pid]
std::shared_ptr<ProcessInfo> readProcessInfo(int pid);

// Get parent chain for a process
std::vector<ProcessInfo> getParentChain(int pid);

// Find launch script in parent chain
std::shared_ptr<ProcessInfo> findLaunchScript(const std::vector<ProcessInfo>& chain);

// Check if a process name is a known shell
bool isShell(const std::string& name);

// Get ports for a specific process
std::vector<int> getPortsForProcess(int pid);

// Get processes listening on a specific port
std::vector<int> getProcessesListeningOnPort(int port);

// Discover a process and its launch context
std::shared_ptr<ProcessInfo> discoverProcess(int pid);

// Discover process on a port
std::shared_ptr<ProcessInfo> discoverProcessOnPort(int port);

// Check if a process is a kernel thread
bool isKernelThread(int pid, const std::string& cmdline);

} // namespace vp

#endif // VP_PROCUTIL_HPP
