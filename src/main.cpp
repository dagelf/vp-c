#include "state.hpp"
#include "process.hpp"
#include "resource.hpp"
#include "api.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>

using namespace vp;

std::shared_ptr<State> state;

void listInstances() {
    // Run discovery
    matchAndUpdateInstances(state);

    if (state->instances.empty()) {
        std::cout << "No instances running\n";
        return;
    }

    // Header
    std::cout << std::left
              << std::setw(20) << "NAME"
              << std::setw(10) << "STATUS"
              << std::setw(8) << "PID"
              << std::setw(12) << "CPU TIME"
              << std::setw(40) << "COMMAND"
              << "RESOURCES\n";

    // Instances
    for (const auto& kv : state->instances) {
        const auto& inst = kv.second;

        std::string cpuTimeStr = "-";
        if (inst->cpu_time > 0) {
            if (inst->cpu_time < 60) {
                cpuTimeStr = std::to_string((int)(inst->cpu_time * 100) / 100.0) + "s";
            } else if (inst->cpu_time < 3600) {
                int minutes = (int)inst->cpu_time / 60;
                int secs = (int)inst->cpu_time % 60;
                cpuTimeStr = std::to_string(minutes) + "m " + std::to_string(secs) + "s";
            } else {
                int hours = (int)inst->cpu_time / 3600;
                int minutes = ((int)inst->cpu_time / 60) % 60;
                cpuTimeStr = std::to_string(hours) + "h " + std::to_string(minutes) + "m";
            }
        }

        std::string resources;
        for (const auto& res : inst->resources) {
            resources += res.first + "=" + res.second + " ";
        }

        std::string command = inst->command;
        if (command.length() > 40) {
            command = command.substr(0, 37) + "...";
        }

        std::cout << std::left
                  << std::setw(20) << inst->name
                  << std::setw(10) << inst->status
                  << std::setw(8) << inst->pid
                  << std::setw(12) << cpuTimeStr
                  << std::setw(40) << command
                  << resources << "\n";
    }
}

std::map<std::string, std::string> parseVars(const std::vector<std::string>& args) {
    std::map<std::string, std::string> vars;

    for (const auto& arg : args) {
        if (arg.substr(0, 2) == "--") {
            size_t eqPos = arg.find('=');
            if (eqPos != std::string::npos) {
                std::string key = arg.substr(2, eqPos - 2);
                std::string value = arg.substr(eqPos + 1);
                vars[key] = value;
            } else {
                std::string key = arg.substr(2);
                vars[key] = "true";
            }
        }
    }

    return vars;
}

void handleStart(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: vp start <template> <name> [--key=value...]\n";
        exit(1);
    }

    matchAndUpdateInstances(state);

    std::string templateID = args[0];
    std::string name = args[1];

    std::vector<std::string> varArgs(args.begin() + 2, args.end());
    auto vars = parseVars(varArgs);

    auto it = state->templates.find(templateID);
    if (it == state->templates.end()) {
        std::cerr << "Template not found: " << templateID << "\n";
        std::cerr << "Available templates:\n";
        for (const auto& kv : state->templates) {
            std::cerr << "  " << kv.first << " - " << kv.second->label << "\n";
        }
        exit(1);
    }

    try {
        auto inst = startProcess(state, *it->second, name, vars);
        std::cout << "Started " << inst->name << " (PID " << inst->pid << ")\n";
        std::cout << "Command: " << inst->command << "\n";
        std::cout << "Resources:\n";
        for (const auto& kv : inst->resources) {
            std::cout << "  " << kv.first << " = " << kv.second << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        exit(1);
    }
}

void handleStop(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: vp stop <name>\n";
        exit(1);
    }

    matchAndUpdateInstances(state);

    std::string name = args[0];
    auto it = state->instances.find(name);

    if (it == state->instances.end()) {
        std::cerr << "Instance not found: " << name << "\n";
        exit(1);
    }

    if (!stopProcess(state, it->second)) {
        std::cerr << "Error stopping process\n";
        exit(1);
    }

    state->releaseResources(name);
    state->save();

    std::cout << "Stopped " << name << "\n";
}

void handleRestart(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: vp restart <name>\n";
        exit(1);
    }

    matchAndUpdateInstances(state);

    std::string name = args[0];
    auto it = state->instances.find(name);

    if (it == state->instances.end()) {
        std::cerr << "Instance not found: " << name << "\n";
        exit(1);
    }

    if (!restartProcess(state, it->second)) {
        std::cerr << "Error restarting process\n";
        exit(1);
    }

    std::cout << "Restarted " << it->second->name << " (PID " << it->second->pid << ")\n";
}

void handleDelete(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: vp delete <name>\n";
        exit(1);
    }

    matchAndUpdateInstances(state);

    std::string name = args[0];
    auto it = state->instances.find(name);

    if (it == state->instances.end()) {
        std::cerr << "Instance not found: " << name << "\n";
        exit(1);
    }

    if (it->second->status == "running") {
        stopProcess(state, it->second);
    }

    state->releaseResources(name);
    state->instances.erase(name);
    state->save();

    std::cout << "Deleted " << name << "\n";
}

void handleServe(const std::vector<std::string>& args) {
    std::string port = "8080";
    if (!args.empty()) {
        port = args[0];
    }

    std::cout << "Running discovery to match existing processes...\n";
    matchAndUpdateInstances(state);

    std::cout << "Starting web UI on http://localhost:" << port << "\n";

    if (!serveHTTP(":" + port, state)) {
        std::cerr << "Error starting server\n";
        exit(1);
    }
}

void printUsage() {
    std::cerr << "Usage: vp <command> [args...]\n";
    std::cerr << "Commands:\n";
    std::cerr << "  start <template> <name> [--key=value...]  - Start a new process\n";
    std::cerr << "  stop <name>                                - Stop a running process\n";
    std::cerr << "  restart <name>                             - Restart a stopped process\n";
    std::cerr << "  delete <name>                              - Delete a process instance\n";
    std::cerr << "  ps                                         - List all instances\n";
    std::cerr << "  serve [port]                               - Start web UI (default: 8080)\n";
}

int main(int argc, char* argv[]) {
    state = State::load();

    if (argc < 2) {
        listInstances();
        return 0;
    }

    std::string cmd = argv[1];
    std::vector<std::string> args;

    for (int i = 2; i < argc; i++) {
        args.push_back(argv[i]);
    }

    if (cmd == "start") {
        handleStart(args);
    } else if (cmd == "stop") {
        handleStop(args);
    } else if (cmd == "restart") {
        handleRestart(args);
    } else if (cmd == "delete") {
        handleDelete(args);
    } else if (cmd == "ps") {
        listInstances();
    } else if (cmd == "serve") {
        handleServe(args);
    } else {
        std::cerr << "Unknown command: " << cmd << "\n";
        printUsage();
        return 1;
    }

    return 0;
}
