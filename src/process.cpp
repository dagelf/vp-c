#include "process.hpp"
#include "resource.hpp"
#include "procutil.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <cstring>
#include <sstream>
#include <regex>
#include <thread>
#include <chrono>
#include <iostream>
#include <dirent.h>
#include <fstream>

namespace vp {

std::shared_ptr<Instance> startProcess(
    std::shared_ptr<State> state,
    const Template& tmpl,
    const std::string& name,
    const std::map<std::string, std::string>& vars
) {
    // Check if instance already exists
    if (state->instances.find(name) != state->instances.end()) {
        throw std::runtime_error("instance " + name + " already exists");
    }

    auto inst = std::make_shared<Instance>();
    inst->name = name;
    inst->template_name = tmpl.id;
    inst->status = "starting";
    inst->pid = 0;

    // Merge template defaults with provided vars
    std::map<std::string, std::string> finalVars = tmpl.vars;
    for (const auto& kv : vars) {
        finalVars[kv.first] = kv.second;
    }

    // Phase 1: Allocate resources
    for (const auto& rtype : tmpl.resources) {
        try {
            std::string reqValue = (finalVars.find(rtype) != finalVars.end()) ? finalVars[rtype] : "";
            std::string value = allocateResource(state, rtype, reqValue);
            inst->resources[rtype] = value;
            state->claimResource(rtype, value, name);
            finalVars[rtype] = value;
        } catch (const std::exception& e) {
            state->releaseResources(name);
            inst->status = "error";
            inst->error = std::string("resource allocation failed: ") + e.what();
            throw;
        }
    }

    // Phase 2: Interpolate command
    std::string cmd = tmpl.command;

    // Replace ${var} syntax
    for (const auto& kv : finalVars) {
        std::string placeholder = "${" + kv.first + "}";
        size_t pos = 0;
        while ((pos = cmd.find(placeholder, pos)) != std::string::npos) {
            cmd.replace(pos, placeholder.length(), kv.second);
            pos += kv.second.length();
        }
    }

    // Handle %counter syntax
    std::regex counterRe("%([a-zA-Z_][a-zA-Z0-9_]*)");
    std::smatch match;
    while (std::regex_search(cmd, match, counterRe)) {
        std::string counter = match[1].str();

        try {
            std::string value = allocateResource(state, counter, "");
            cmd = std::regex_replace(cmd, std::regex("%" + counter), value, std::regex_constants::format_first_only);
            inst->resources[counter] = value;
            state->claimResource(counter, value, name);
        } catch (const std::exception& e) {
            state->releaseResources(name);
            inst->status = "error";
            inst->error = std::string("counter allocation failed: ") + e.what();
            throw;
        }
    }

    inst->command = cmd;

    // Interpolate action if present
    if (!tmpl.action.empty()) {
        std::string action = tmpl.action;
        for (const auto& kv : finalVars) {
            std::string placeholder = "${" + kv.first + "}";
            size_t pos = 0;
            while ((pos = action.find(placeholder, pos)) != std::string::npos) {
                action.replace(pos, placeholder.length(), kv.second);
                pos += kv.second.length();
            }
        }
        for (const auto& kv : inst->resources) {
            std::string placeholder = "${" + kv.first + "}";
            size_t pos = 0;
            while ((pos = action.find(placeholder, pos)) != std::string::npos) {
                action.replace(pos, placeholder.length(), kv.second);
                pos += kv.second.length();
            }
        }
        inst->action = action;
    }

    // Interpolate launchers if present
    if (!tmpl.launchers.empty()) {
        for (const auto& launcher : tmpl.launchers) {
            std::string cmd = launcher.second;
            // Replace vars
            for (const auto& kv : finalVars) {
                std::string placeholder = "${" + kv.first + "}";
                size_t pos = 0;
                while ((pos = cmd.find(placeholder, pos)) != std::string::npos) {
                    cmd.replace(pos, placeholder.length(), kv.second);
                    pos += kv.second.length();
                }
            }
            // Replace resources
            for (const auto& kv : inst->resources) {
                std::string placeholder = "${" + kv.first + "}";
                size_t pos = 0;
                while ((pos = cmd.find(placeholder, pos)) != std::string::npos) {
                    cmd.replace(pos, placeholder.length(), kv.second);
                    pos += kv.second.length();
                }
            }
            inst->launchers[launcher.first] = cmd;
        }
    }

    // Phase 3: Start process
    pid_t pid = fork();

    if (pid == -1) {
        state->releaseResources(name);
        inst->status = "error";
        inst->error = "failed to fork process";
        throw std::runtime_error("failed to fork process");
    }

    if (pid == 0) {
        // Child process
        setpgid(0, 0); // Create new process group

        // Set working directory if specified
        auto it = inst->resources.find("workdir");
        if (it != inst->resources.end() && !it->second.empty()) {
            if (chdir(it->second.c_str()) != 0) {
                _exit(126); // chdir failed
            }
        }

        // Execute command using shell
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127); // If exec fails
    }

    // Parent process
    inst->pid = pid;
    inst->status = "running";
    inst->started = time(nullptr);
    inst->managed = true;

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        inst->cwd = cwd;
    }

    state->instances[name] = inst;
    state->save();

    // Start reaper thread
    std::thread([state, name, pid]() {
        int status;
        waitpid(pid, &status, 0);

        // Process has exited
        auto it = state->instances.find(name);
        if (it != state->instances.end() && it->second->pid == pid) {
            it->second->status = "stopped";
            it->second->pid = 0;
            state->save();
        }
    }).detach();

    return inst;
}

bool stopProcess(std::shared_ptr<State> state, std::shared_ptr<Instance> inst) {
    if (inst->pid == 0) {
        return false;
    }

    inst->status = "stopping";

    // Kill the entire process group
    int pgid = inst->pid;
    kill(-pgid, SIGTERM);

    // Wait up to 2 seconds for graceful shutdown
    for (int i = 0; i < 20; i++) {
        if (!isProcessRunning(inst->pid)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Force kill if still running
    if (isProcessRunning(inst->pid)) {
        kill(-pgid, SIGKILL);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    inst->status = "stopped";
    inst->pid = 0;
    state->save();

    return true;
}

bool restartProcess(std::shared_ptr<State> state, std::shared_ptr<Instance> inst) {
    if (inst->status != "stopped") {
        return false;
    }

    // Verify resources are still available
    for (const auto& kv : inst->resources) {
        auto it = state->types.find(kv.first);
        if (it == state->types.end()) {
            return false;
        }

        if (!checkResource(*it->second, kv.second)) {
            return false;
        }

        state->claimResource(kv.first, kv.second, inst->name);
    }

    // Start the process
    pid_t pid = fork();

    if (pid == -1) {
        state->releaseResources(inst->name);
        inst->status = "error";
        inst->error = "failed to fork process";
        return false;
    }

    if (pid == 0) {
        // Child process
        setpgid(0, 0);

        execl("/bin/sh", "sh", "-c", inst->command.c_str(), (char*)nullptr);
        _exit(127);
    }

    // Parent process
    inst->pid = pid;
    inst->status = "running";
    inst->started = time(nullptr);
    inst->error = "";
    state->save();

    // Start reaper thread
    std::thread([state, inst, pid]() {
        int status;
        waitpid(pid, &status, 0);

        if (inst->pid == pid) {
            inst->status = "stopped";
            inst->pid = 0;
            state->save();
        }
    }).detach();

    return true;
}

bool isProcessRunning(int pid) {
    return kill(pid, 0) == 0;
}

bool canManageProcess(int pid) {
    return kill(pid, 0) == 0;
}

std::shared_ptr<Instance> monitorProcess(std::shared_ptr<State> state, int pid, const std::string& name) {
    if (state->instances.find(name) != state->instances.end()) {
        throw std::runtime_error("instance " + name + " already exists");
    }

    if (!isProcessRunning(pid)) {
        throw std::runtime_error("process " + std::to_string(pid) + " not running");
    }

    auto procInfo = readProcessInfo(pid);
    if (!procInfo) {
        throw std::runtime_error("cannot read process " + std::to_string(pid));
    }

    auto inst = std::make_shared<Instance>();
    inst->name = name;
    inst->command = procInfo->cmdline;
    inst->pid = pid;
    inst->status = "running";
    inst->cwd = procInfo->cwd;
    inst->managed = canManageProcess(pid);
    inst->started = time(nullptr);

    // Add ports as resources
    for (size_t i = 0; i < procInfo->ports.size(); i++) {
        std::string key = (i == 0) ? "tcpport" : "tcpport" + std::to_string(i);
        std::string value = std::to_string(procInfo->ports[i]);
        inst->resources[key] = value;
        state->claimResource(key, value, name);
    }

    if (!procInfo->cwd.empty()) {
        inst->resources["workdir"] = procInfo->cwd;
    }

    state->instances[name] = inst;
    state->save();

    // Start monitoring thread
    std::thread([state, name, pid]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            if (!isProcessRunning(pid)) {
                auto it = state->instances.find(name);
                if (it != state->instances.end() && it->second->pid == pid) {
                    it->second->status = "stopped";
                    it->second->pid = 0;
                    state->save();
                }
                break;
            }
        }
    }).detach();

    return inst;
}

std::shared_ptr<Instance> discoverAndImportProcess(std::shared_ptr<State> state, int pid, const std::string& name) {
    if (state->instances.find(name) != state->instances.end()) {
        throw std::runtime_error("instance " + name + " already exists");
    }

    auto procInfo = discoverProcess(pid);
    if (!procInfo) {
        throw std::runtime_error("failed to discover process");
    }

    auto inst = std::make_shared<Instance>();
    inst->name = name;
    inst->template_name = "discovered";
    inst->command = procInfo->cmdline;
    inst->pid = pid;
    inst->status = "running";
    inst->started = time(nullptr);
    inst->managed = false;

    state->instances[name] = inst;
    state->save();

    return inst;
}

std::shared_ptr<Instance> discoverAndImportProcessOnPort(std::shared_ptr<State> state, int port, const std::string& name) {
    if (state->instances.find(name) != state->instances.end()) {
        throw std::runtime_error("instance " + name + " already exists");
    }

    auto procInfo = discoverProcessOnPort(port);
    if (!procInfo) {
        throw std::runtime_error("failed to discover process on port " + std::to_string(port));
    }

    auto inst = std::make_shared<Instance>();
    inst->name = name;
    inst->template_name = "discovered";
    inst->command = procInfo->cmdline;
    inst->pid = procInfo->pid;
    inst->status = "running";
    inst->started = time(nullptr);
    inst->managed = false;
    inst->resources["tcpport"] = std::to_string(port);

    state->instances[name] = inst;
    state->save();

    return inst;
}

std::vector<std::map<std::string, std::string>> discoverProcesses(std::shared_ptr<State> state, bool includePorts, bool portsOnly, int maxAgeSeconds) {
    std::lock_guard<std::mutex> lock(state->discoveryMutex);
    time_t now = time(nullptr);

    // Check cache
    bool cacheValid = (now - state->lastDiscoveryTime) < maxAgeSeconds;
    
    // If cache is valid, we might need to upgrade it with ports
    if (cacheValid) {
        if (includePorts && !state->lastDiscoveryHadPorts) {
            // Need to add ports to cached processes
            auto portMap = buildPortToProcessMap();
            
            for (auto& proc : state->lastDiscovery) {
                try {
                    int pid = std::stoi(proc["pid"]);
                    std::vector<std::string> ports;
                    
                    for (const auto& [port, pids] : portMap) {
                        for (int p : pids) {
                            if (p == pid) {
                                ports.push_back(std::to_string(port));
                            }
                        }
                    }
                    
                    if (!ports.empty()) {
                        std::string portsStr;
                        for (size_t i = 0; i < ports.size(); ++i) {
                            if (i > 0) portsStr += ",";
                            portsStr += ports[i];
                        }
                        proc["ports"] = portsStr;
                    }
                } catch (...) {}
            }
            state->lastDiscoveryHadPorts = true;
        }
        
        // Return cached result (filtered if portsOnly)
        if (portsOnly) {
            std::vector<std::map<std::string, std::string>> filtered;
            for (const auto& proc : state->lastDiscovery) {
                if (proc.count("ports") && !proc.at("ports").empty()) {
                    filtered.push_back(proc);
                }
            }
            return filtered;
        }
        return state->lastDiscovery;
    }

    // Cache invalid, perform full scan
    std::vector<std::map<std::string, std::string>> processes;
    std::map<int, std::vector<int>> portMap;

    if (includePorts) {
        portMap = buildPortToProcessMap();
    }

    // Read all PIDs from /proc
    DIR* procDir = opendir("/proc");
    if (!procDir) {
        return processes;
    }

    struct dirent* entry;
    while ((entry = readdir(procDir)) != nullptr) {
        // Check if entry is a PID (numeric)
        int pid = atoi(entry->d_name);
        if (pid <= 0) {
            continue;
        }

        // Skip if already monitored
        bool alreadyMonitored = false;
        for (const auto& [name, inst] : state->instances) {
            if (inst->pid == pid) {
                alreadyMonitored = true;
                break;
            }
        }
        if (alreadyMonitored) {
            continue;
        }

        // Read basic process info WITHOUT ports (avoid repeated port lookups)
        std::string procDir_path = "/proc/" + std::to_string(pid);

        // Read stat file for name and ppid
        std::string statPath = procDir_path + "/stat";
        std::ifstream statFile(statPath);
        if (!statFile.is_open()) continue;

        std::string statLine;
        std::getline(statFile, statLine);

        std::string name, cmdline;
        int ppid = 0;

        // Parse stat file
        size_t lastParen = statLine.rfind(')');
        if (lastParen != std::string::npos) {
            size_t firstParen = statLine.find('(');
            if (firstParen != std::string::npos && lastParen > firstParen) {
                name = statLine.substr(firstParen + 1, lastParen - firstParen - 1);
            }
            std::istringstream iss(statLine.substr(lastParen + 1));
            std::string state;
            iss >> state >> ppid;
        }

        // Read cmdline
        std::string cmdlinePath = procDir_path + "/cmdline";
        std::ifstream cmdlineFile(cmdlinePath);
        if (cmdlineFile.is_open()) {
            std::getline(cmdlineFile, cmdline, '\0');
            for (char& c : cmdline) {
                if (c == '\0') c = ' ';
            }
            cmdline.erase(cmdline.find_last_not_of(" \t\n\r") + 1);
        }

        // Skip kernel threads
        if (isKernelThread(pid, cmdline)) {
            continue;
        }

        std::vector<int> ports;
        if (includePorts) {
            // Look up ports from pre-built map
            for (const auto& [port, pids] : portMap) {
                for (int p : pids) {
                    if (p == pid) {
                        ports.push_back(port);
                        break;
                    }
                }
            }
        }

        // If portsOnly, skip processes not listening on ports
        if (portsOnly && ports.empty()) {
            continue;
        }

        // Read exe and cwd only for non-kernel threads
        std::string exe, cwd;
        std::string exePath = procDir_path + "/exe";
        char exeBuf[PATH_MAX];
        ssize_t len = readlink(exePath.c_str(), exeBuf, sizeof(exeBuf) - 1);
        if (len != -1) {
            exeBuf[len] = '\0';
            exe = exeBuf;
        }

        std::string cwdPath = procDir_path + "/cwd";
        char cwdBuf[PATH_MAX];
        len = readlink(cwdPath.c_str(), cwdBuf, sizeof(cwdBuf) - 1);
        if (len != -1) {
            cwdBuf[len] = '\0';
            cwd = cwdBuf;
        }

        // Build result entry
        std::map<std::string, std::string> procMap;
        procMap["pid"] = std::to_string(pid);
        procMap["ppid"] = std::to_string(ppid);
        procMap["name"] = name;
        procMap["command"] = cmdline;
        procMap["cwd"] = cwd;
        procMap["exe"] = exe;

        // Add ports as comma-separated string
        if (!ports.empty()) {
            std::ostringstream portStream;
            for (size_t i = 0; i < ports.size(); ++i) {
                if (i > 0) portStream << ",";
                portStream << ports[i];
            }
            procMap["ports"] = portStream.str();
        } else {
            procMap["ports"] = "";
        }

        processes.push_back(procMap);
    }

    closedir(procDir);
    // Update cache
    state->lastDiscovery = processes;
    state->lastDiscoveryTime = now;
    state->lastDiscoveryHadPorts = includePorts;

    return processes;
}

bool matchAndUpdateInstances(std::shared_ptr<State> state, const std::vector<std::map<std::string, std::string>>* preDiscovered) {
    // Update CPU time and check if processes are still running
    for (auto& kv : state->instances) {
        auto& inst = kv.second;

        if (inst->status == "running") {
            if (isProcessRunning(inst->pid)) {
                auto procInfo = readProcessInfo(inst->pid);
                if (procInfo) {
                    inst->cpu_time = procInfo->cpu_time;
                }
            } else {
                inst->status = "stopped";
                inst->pid = 0;
                inst->cpu_time = 0;
            }
        }
    }

    // Check if any stopped instances are actually running
    // We do this by discovering all processes (without ports first) and matching
    // If preDiscovered is provided, use it. Otherwise discover (with caching).
    std::vector<std::map<std::string, std::string>> localProcesses;
    const std::vector<std::map<std::string, std::string>>* processesPtr = preDiscovered;
    
    if (!processesPtr) {
        // Use cache if available (2 seconds max age)
        localProcesses = discoverProcesses(state, false, false, 2);
        processesPtr = &localProcesses;
    }
    
    const auto& processes = *processesPtr;

    for (auto& kv : state->instances) {
        auto& inst = kv.second;

        if (inst->status == "stopped") {
            std::string instCmdName = extractProcessName(inst->command);
            std::string instCwd = inst->cwd;
            if (instCwd.empty() && inst->resources.count("workdir")) {
                instCwd = inst->resources["workdir"];
            }

            for (const auto& proc : processes) {
                // Match by command name (executable name)
                std::string procName = proc.at("name");
                std::string procExe = proc.at("exe");
                std::string procCmdName = extractProcessName(procExe);
                if (procCmdName.empty()) procCmdName = procName;

                // Simple match: if process name matches instance command name
                // Note: instCmdName might be "node", procName might be "node"
                if (instCmdName == procName || instCmdName == procCmdName) {
                    // Match by CWD
                    if (!instCwd.empty() && instCwd == proc.at("cwd")) {
                        // Potential match found. Check ports if applicable.
                        bool match = true;
                        int pid = std::stoi(proc.at("pid"));

                        // Check all port resources
                        for (const auto& res : inst->resources) {
                            if (res.first.find("port") != std::string::npos) {
                                try {
                                    int port = std::stoi(res.second);
                                    if (!isProcessListeningOnPort(pid, port)) {
                                        match = false;
                                        break;
                                    }
                                } catch (...) {
                                    // Ignore invalid port values
                                }
                            }
                        }

                        if (match) {
                            inst->status = "running";
                            inst->pid = pid;
                            inst->managed = true; // We found it, so we assume we can manage it (or at least monitor it)
                            break; // Stop looking for this instance
                        }
                    }
                }
            }
        }
    }

    state->save();
    return true;
}

bool executeAction(const std::string& action) {
    if (action.empty()) {
        return false;
    }

    std::string cmd = action + " &";
    return system(cmd.c_str()) == 0;
}

std::string extractProcessName(const std::string& command) {
    if (command.empty()) {
        return "";
    }

    std::istringstream iss(command);
    std::string exe;
    iss >> exe;

    size_t lastSlash = exe.find_last_of('/');
    if (lastSlash != std::string::npos) {
        exe = exe.substr(lastSlash + 1);
    }

    return exe;
}

} // namespace vp
