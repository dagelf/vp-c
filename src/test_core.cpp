#include "test.hpp"
#include "state.hpp"
#include "resource.hpp"
#include "procutil.hpp"
#include "process.hpp"
#include "types.hpp"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fstream>
#include <cstdlib>

using namespace vp;

// Force linker to include this file
extern "C" void register_core_tests() {}

TEST(State_LoadAndSave) {
    // Create a temp state file
    std::string tempFile = "/tmp/test-vp-state.json";
    std::remove(tempFile.c_str());

    // Set environment variable to use temp file
    setenv("HOME", "/tmp/test-home", 1);
    system("mkdir -p /tmp/test-home/.vibeprocess");

    auto state = State::load();
    ASSERT_TRUE(state != nullptr, "State should load successfully");

    // Add some data
    auto inst = std::make_shared<Instance>();
    inst->name = "test-instance";
    inst->command = "sleep 100";
    inst->status = "running";
    inst->pid = 12345;
    state->instances["test-instance"] = inst;

    // Save state
    bool saved = state->save();
    ASSERT_TRUE(saved, "State should save successfully");

    // Load it back
    auto state2 = State::load();
    ASSERT_TRUE(state2->instances.count("test-instance") > 0,
                "Loaded state should contain saved instance");
    ASSERT_EQUALS(std::string("test-instance"), state2->instances["test-instance"]->name,
                  "Instance name should match");

    // Cleanup
    system("rm -rf /tmp/test-home");
}

TEST(State_DefaultResourceTypes) {
    auto state = State::load();

    // Check default resource types exist
    ASSERT_TRUE(state->types.count("tcpport") > 0, "tcpport resource type should exist");
    ASSERT_TRUE(state->types.count("vncport") > 0, "vncport resource type should exist");
    ASSERT_TRUE(state->types.count("dbfile") > 0, "dbfile resource type should exist");
    ASSERT_TRUE(state->types.count("workdir") > 0, "workdir resource type should exist");

    // Check counter type
    auto tcpport = state->types["tcpport"];
    ASSERT_TRUE(tcpport->counter, "tcpport should be a counter type");
    ASSERT_EQUALS(3000, tcpport->start, "tcpport counter should start at 3000");
    ASSERT_EQUALS(9999, tcpport->end, "tcpport counter should end at 9999");
}

TEST(Resource_AllocateCounter_TCPPort) {
    auto state = State::load();

    std::string value = allocateResource(state, "tcpport", "");
    ASSERT_TRUE(!value.empty(), "Allocated port should not be empty");

    int port = std::stoi(value);
    ASSERT_TRUE(port >= 3000 && port <= 9999, "Allocated port should be in range");
}

TEST(Resource_AllocateExplicit_DBFile) {
    auto state = State::load();

    // Create a test file
    std::string testFile = "/tmp/test-db.sqlite";
    std::ofstream(testFile).close();

    std::string value = allocateResource(state, "dbfile", testFile);
    ASSERT_TRUE(!value.empty(), "Should allocate dbfile resource with explicit path");
    ASSERT_EQUALS(testFile, value, "Allocated value should match requested path");

    // Cleanup
    std::remove(testFile.c_str());
}

TEST(Resource_CheckAvailability) {
    auto state = State::load();

    // Get tcpport resource type
    auto tcpport = state->types["tcpport"];

    // Port 1 should be unavailable (system port, likely in use)
    bool available1 = checkResource(*tcpport, "1");
    ASSERT_TRUE(!available1, "Port 1 should be unavailable");

    // Very high port should be available
    bool available2 = checkResource(*tcpport, "55555");
    ASSERT_TRUE(available2, "Port 55555 should be available");
}

TEST(ProcUtil_ExtractProcessName) {
    std::string cmdline1 = "/usr/bin/python3 /home/user/script.py arg1 arg2";
    std::string name1 = extractProcessName(cmdline1);
    ASSERT_EQUALS(std::string("python3"), name1, "Should extract process name from full path");

    std::string cmdline2 = "nginx: master process";
    std::string name2 = extractProcessName(cmdline2);
    ASSERT_EQUALS(std::string("nginx"), name2, "Should extract process name from nginx format");

    std::string cmdline3 = "sleep 100";
    std::string name3 = extractProcessName(cmdline3);
    ASSERT_EQUALS(std::string("sleep"), name3, "Should extract simple command name");
}

TEST(ProcUtil_ReadProcessInfo) {
    // Test with current process (known to exist)
    pid_t myPid = getpid();
    auto info = readProcessInfo(myPid);

    ASSERT_TRUE(info != nullptr, "Should return process info");
    ASSERT_TRUE(info->pid == myPid, "PID should match");
    ASSERT_TRUE(!info->name.empty(), "Process name should not be empty");
    ASSERT_TRUE(info->cpu_time >= 0, "CPU time should be non-negative");
}

TEST(ProcUtil_IsProcessRunning) {
    // Current process should be running
    pid_t myPid = getpid();
    bool running = isProcessRunning(myPid);
    ASSERT_TRUE(running, "Current process should be running");

    // PID 999999 should not exist
    bool notRunning = isProcessRunning(999999);
    ASSERT_TRUE(!notRunning, "Non-existent PID should not be running");
}

TEST(ProcUtil_GetParentChain) {
    pid_t myPid = getpid();
    auto chain = getParentChain(myPid);

    ASSERT_TRUE(!chain.empty(), "Parent chain should not be empty");
    ASSERT_TRUE(chain[0].pid == myPid, "First element should be current PID");

    // Should eventually reach init (PID 1) or systemd
    bool hasInit = false;
    for (const auto& procInfo : chain) {
        if (procInfo.pid == 1) {
            hasInit = true;
            break;
        }
    }
    ASSERT_TRUE(hasInit, "Parent chain should eventually reach init (PID 1)");
}

TEST(ProcUtil_BuildPortToProcessMap) {
    auto portMap = buildPortToProcessMap();
    ASSERT_TRUE(!portMap.empty(), "Port to process map should not be empty");

    // System usually has some listening ports
    bool foundPort = false;
    for (const auto& kv : portMap) {
        if (kv.first > 0 && !kv.second.empty()) {
            foundPort = true;
            break;
        }
    }
    ASSERT_TRUE(foundPort, "Should find at least one port->PIDs mapping");
}

TEST(Process_StartAndStop) {
    auto state = State::load();

    // Create a simple template
    Template tmpl;
    tmpl.id = "test-sleep";
    tmpl.label = "Test Sleep";
    tmpl.command = "sleep 1000";
    tmpl.resources = {};

    std::map<std::string, std::string> vars;

    // Start process
    auto inst = startProcess(state, tmpl, "test-sleep-instance", vars);
    ASSERT_TRUE(inst != nullptr, "Process should start successfully");
    ASSERT_TRUE(inst->pid > 0, "Process should have valid PID");
    ASSERT_EQUALS(std::string("running"), inst->status, "Process should be running");

    // Verify it's actually running
    sleep(1);
    bool running = isProcessRunning(inst->pid);
    ASSERT_TRUE(running, "Process should still be running");

    // Stop process
    bool stopped = stopProcess(state, inst);
    ASSERT_TRUE(stopped, "Process should stop successfully");
    ASSERT_EQUALS(std::string("stopped"), inst->status, "Process status should be stopped");

    // Verify it's actually stopped
    sleep(1);
    running = isProcessRunning(inst->pid);
    ASSERT_TRUE(!running, "Process should be stopped");
}

TEST(Process_DiscoverExisting) {
    auto state = State::load();

    // Start a process outside of vp
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execlp("sleep", "sleep", "60", nullptr);
        _exit(1);
    }

    // Parent process
    sleep(1); // Give it time to start

    // Discover it using procutil
    auto discovered = discoverProcess(pid);
    ASSERT_TRUE(discovered != nullptr, "Should discover running process");
    ASSERT_EQUALS(pid, discovered->pid, "Discovered PID should match");
    ASSERT_CONTAINS(discovered->cmdline, "sleep", "Command should contain 'sleep'");

    // Cleanup - kill the process
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
}

TEST(Process_MonitorMode) {
    auto state = State::load();

    // Start a process outside of vp
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execlp("sleep", "sleep", "60", nullptr);
        _exit(1);
    }

    // Parent process
    sleep(1); // Give it time to start

    // Monitor it
    auto monitored = monitorProcess(state, pid, "monitored-sleep");
    ASSERT_TRUE(monitored != nullptr, "Should monitor running process");
    ASSERT_EQUALS(std::string("monitored-sleep"), monitored->name, "Name should match");
    ASSERT_EQUALS(pid, monitored->pid, "PID should match");
    ASSERT_TRUE(!monitored->managed, "Monitored process should not be managed");

    // Cleanup - kill the process
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
}
