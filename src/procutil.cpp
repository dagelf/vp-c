#include "procutil.hpp"
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#include <iostream>

namespace vp {

const std::map<std::string, bool> SHELL_NAMES = {
    {"sh", true}, {"bash", true}, {"zsh", true}, {"fish", true},
    {"dash", true}, {"ksh", true}, {"tcsh", true}, {"csh", true}
};

std::map<int, std::vector<int>> buildPortToProcessMap() {
    std::map<int, std::vector<int>> portToPIDs;
    std::map<std::string, int> inodeToPort;

    // Parse /proc/net/tcp and /proc/net/tcp6
    const char* tcpFiles[] = {"/proc/net/tcp", "/proc/net/tcp6"};

    for (const char* tcpFile : tcpFiles) {
        std::ifstream file(tcpFile);
        if (!file.is_open()) continue;

        std::string line;
        std::getline(file, line); // Skip header

        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::vector<std::string> fields;
            std::string field;
            while (iss >> field) {
                fields.push_back(field);
            }

            if (fields.size() < 10) continue;

            // Field 3 is connection state (0A = LISTEN)
            if (fields[3] != "0A") continue;

            // Parse port from local_address (IP:PORT in hex)
            std::string localAddr = fields[1];
            size_t colonPos = localAddr.find(':');
            if (colonPos == std::string::npos) continue;

            std::string portHex = localAddr.substr(colonPos + 1);
            int portNum = std::stoi(portHex, nullptr, 16);

            // Store inode -> port mapping
            std::string inode = fields[9];
            inodeToPort[inode] = portNum;
        }
    }

    // Scan /proc to find PIDs for each inode
    DIR* procDir = opendir("/proc");
    if (!procDir) return portToPIDs;

    struct dirent* entry;
    while ((entry = readdir(procDir)) != nullptr) {
        // Check if entry is a PID (numeric)
        if (!isdigit(entry->d_name[0])) continue;

        int pid = atoi(entry->d_name);

        // Read all FDs for this PID
        std::string fdDir = std::string("/proc/") + entry->d_name + "/fd";
        DIR* fdDirPtr = opendir(fdDir.c_str());
        if (!fdDirPtr) continue;

        struct dirent* fdEntry;
        while ((fdEntry = readdir(fdDirPtr)) != nullptr) {
            if (fdEntry->d_name[0] == '.') continue;

            std::string fdPath = fdDir + "/" + fdEntry->d_name;
            char link[256];
            ssize_t len = readlink(fdPath.c_str(), link, sizeof(link) - 1);
            if (len == -1) continue;
            link[len] = '\0';

            // Check if it's a socket
            std::string linkStr(link);
            if (linkStr.find("socket:[") != 0) continue;

            // Extract inode
            std::string inode = linkStr.substr(8);
            inode = inode.substr(0, inode.length() - 1);

            // Check if this inode corresponds to a listening port
            auto it = inodeToPort.find(inode);
            if (it != inodeToPort.end()) {
                portToPIDs[it->second].push_back(pid);
            }
        }
        closedir(fdDirPtr);
    }
    closedir(procDir);

    return portToPIDs;
}

bool isKernelThread(int pid, const std::string& cmdline) {
    if (pid == 2) return true;

    if (cmdline.empty() || cmdline.find_first_not_of(" \t\n\r") == std::string::npos) {
        std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream file(statPath);
        if (!file.is_open()) return false;

        std::string line;
        std::getline(file, line);

        size_t lastParen = line.rfind(')');
        if (lastParen == std::string::npos) return false;

        std::istringstream iss(line.substr(lastParen + 1));
        std::string state;
        int ppid;
        iss >> state >> ppid;

        if (ppid == 2 || ppid == 0) return true;
    }

    return false;
}

std::shared_ptr<ProcessInfo> readProcessInfo(int pid) {
    auto info = std::make_shared<ProcessInfo>();
    info->pid = pid;

    std::string procDir = "/proc/" + std::to_string(pid);

    // Check if process exists
    struct stat st;
    if (stat(procDir.c_str(), &st) != 0) {
        return nullptr;
    }

    // Read stat file
    std::string statPath = procDir + "/stat";
    std::ifstream statFile(statPath);
    if (!statFile.is_open()) return nullptr;

    std::string statLine;
    std::getline(statFile, statLine);

    // Parse stat file
    size_t lastParen = statLine.rfind(')');
    if (lastParen == std::string::npos) return nullptr;

    // Extract name from (name)
    size_t firstParen = statLine.find('(');
    if (firstParen != std::string::npos && lastParen > firstParen) {
        info->name = statLine.substr(firstParen + 1, lastParen - firstParen - 1);
    }

    // Parse fields after name
    std::istringstream iss(statLine.substr(lastParen + 1));
    std::string state;
    iss >> state >> info->ppid;

    // Skip to utime and stime (fields 14 and 15, now at positions 11 and 12)
    std::vector<std::string> fields;
    std::string field;
    while (iss >> field) {
        fields.push_back(field);
    }

    if (fields.size() >= 13) {
        long utime = std::stol(fields[11]);
        long stime = std::stol(fields[12]);
        info->cpu_time = static_cast<double>(utime + stime) / 100.0;
    }

    // Read cmdline
    std::string cmdlinePath = procDir + "/cmdline";
    std::ifstream cmdlineFile(cmdlinePath);
    if (cmdlineFile.is_open()) {
        std::string cmdline;
        std::getline(cmdlineFile, cmdline, '\0');

        // Replace null bytes with spaces
        for (char& c : cmdline) {
            if (c == '\0') c = ' ';
        }

        // Trim trailing whitespace
        cmdline.erase(cmdline.find_last_not_of(" \t\n\r") + 1);
        info->cmdline = cmdline;
    }

    if (!isKernelThread(pid, info->cmdline)) {
        // Read exe
        std::string exePath = procDir + "/exe";
        char exe[PATH_MAX];
        ssize_t len = readlink(exePath.c_str(), exe, sizeof(exe) - 1);
        if (len != -1) {
            exe[len] = '\0';
            info->exe = exe;
        }

        // Read cwd
        std::string cwdPath = procDir + "/cwd";
        char cwd[PATH_MAX];
        len = readlink(cwdPath.c_str(), cwd, sizeof(cwd) - 1);
        if (len != -1) {
            cwd[len] = '\0';
            info->cwd = cwd;
        }

        // Read environ
        std::string environPath = procDir + "/environ";
        std::ifstream environFile(environPath);
        if (environFile.is_open()) {
            std::string environData((std::istreambuf_iterator<char>(environFile)),
                                    std::istreambuf_iterator<char>());

            size_t pos = 0;
            while (pos < environData.size()) {
                size_t nextNull = environData.find('\0', pos);
                if (nextNull == std::string::npos) break;

                std::string pair = environData.substr(pos, nextNull - pos);
                size_t eqPos = pair.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = pair.substr(0, eqPos);
                    std::string value = pair.substr(eqPos + 1);
                    info->environ[key] = value;
                }

                pos = nextNull + 1;
            }
        }

        // Get ports
        info->ports = getPortsForProcess(pid);
    }

    return info;
}

std::vector<int> getPortsForProcess(int pid) {
    std::vector<int> result;
    auto portMap = buildPortToProcessMap();

    for (const auto& entry : portMap) {
        int port = entry.first;
        const auto& pids = entry.second;

        for (int p : pids) {
            if (p == pid) {
                result.push_back(port);
                break;
            }
        }
    }

    return result;
}

std::vector<int> getProcessesListeningOnPort(int port) {
    auto portMap = buildPortToProcessMap();
    auto it = portMap.find(port);

    if (it != portMap.end()) {
        return it->second;
    }

    return {};
}

std::vector<ProcessInfo> getParentChain(int pid) {
    std::vector<ProcessInfo> chain;
    int currentPID = pid;
    std::map<int, bool> seen;

    while (currentPID > 0 && !seen[currentPID]) {
        seen[currentPID] = true;

        auto info = readProcessInfo(currentPID);
        if (!info) break;

        chain.push_back(*info);

        if (currentPID == 1 || info->ppid == 0) break;

        currentPID = info->ppid;

        if (chain.size() > 100) break; // Safety limit
    }

    return chain;
}

std::shared_ptr<ProcessInfo> findLaunchScript(const std::vector<ProcessInfo>& chain) {
    for (size_t i = 0; i < chain.size(); i++) {
        if (i + 1 < chain.size()) {
            const ProcessInfo& parent = chain[i + 1];
            if (isShell(parent.name) || isShell(parent.exe.substr(parent.exe.find_last_of('/') + 1))) {
                return std::make_shared<ProcessInfo>(chain[i]);
            }
        }
    }

    // Fallback
    for (int i = chain.size() - 1; i >= 0; i--) {
        if (chain[i].pid != 1 && chain[i].name != "systemd") {
            return std::make_shared<ProcessInfo>(chain[i]);
        }
    }

    return nullptr;
}

bool isShell(const std::string& name) {
    return SHELL_NAMES.find(name) != SHELL_NAMES.end();
}

std::shared_ptr<ProcessInfo> discoverProcess(int pid) {
    auto chain = getParentChain(pid);
    if (chain.empty()) {
        return nullptr;
    }

    return std::make_shared<ProcessInfo>(chain[0]);
}

std::shared_ptr<ProcessInfo> discoverProcessOnPort(int port) {
    auto pids = getProcessesListeningOnPort(port);
    if (pids.empty()) {
        return nullptr;
    }

    return discoverProcess(pids[0]);
}

} // namespace vp
