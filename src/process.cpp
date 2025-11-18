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

std::vector<std::map<std::string, std::string>> discoverProcesses(std::shared_ptr<State> state, bool portsOnly) {
    std::vector<std::map<std::string, std::string>> result;

    // Read all PIDs from /proc
    DIR* procDir = opendir("/proc");
    if (!procDir) {
        return result;
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

        // Read process info
        auto procInfo = readProcessInfo(pid);
        if (!procInfo) {
            continue; // Skip processes we can't read
        }

        // Skip kernel threads
        if (isKernelThread(pid, procInfo->cmdline)) {
            continue;
        }

        // If portsOnly, skip processes not listening on ports
        if (portsOnly && procInfo->ports.empty()) {
            continue;
        }

        // Build result entry
        std::map<std::string, std::string> procMap;
        procMap["pid"] = std::to_string(procInfo->pid);
        procMap["ppid"] = std::to_string(procInfo->ppid);
        procMap["name"] = procInfo->name;
        procMap["command"] = procInfo->cmdline;
        procMap["cwd"] = procInfo->cwd;
        procMap["exe"] = procInfo->exe;

        // Add ports as comma-separated string
        if (!procInfo->ports.empty()) {
            std::ostringstream portStream;
            for (size_t i = 0; i < procInfo->ports.size(); ++i) {
                if (i > 0) portStream << ",";
                portStream << procInfo->ports[i];
            }
            procMap["ports"] = portStream.str();
        } else {
            procMap["ports"] = "";
        }

        result.push_back(procMap);
    }

    closedir(procDir);
    return result;
}

bool matchAndUpdateInstances(std::shared_ptr<State> state) {
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
