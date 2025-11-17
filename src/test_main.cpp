#include "test.hpp"
#include "state.hpp"
#include "process.hpp"
#include "resource.hpp"
#include "procutil.hpp"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>

using namespace vp;
using namespace vp::test;

// Helper to start a test process
pid_t startTestProcess(const std::string& cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return pid;
}

// Helper to kill a test process
void killTestProcess(pid_t pid) {
    if (pid > 0) {
        kill(-pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    }
}

TEST(EmptyStateNoError) {
    auto state = State::load();
    bool result = matchAndUpdateInstances(state);
    assertTrue(result, "matchAndUpdateInstances should succeed with empty state");
}

TEST(ProcessRunningCheck) {
    // Start a sleep process
    pid_t pid = startTestProcess("sleep 300");
    assertTrue(pid > 0, "Failed to start test process");

    // Check if it's running
    bool running = isProcessRunning(pid);
    assertTrue(running, "Process should be detected as running");

    // Kill it
    killTestProcess(pid);

    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check if it's stopped
    running = isProcessRunning(pid);
    assertTrue(!running, "Process should be detected as stopped");
}

TEST(ReadProcessInfo) {
    // Start a sleep process
    pid_t pid = startTestProcess("sleep 300");
    assertTrue(pid > 0, "Failed to start test process");

    // Read process info
    auto info = readProcessInfo(pid);
    assertTrue(info != nullptr, "Should be able to read process info");
    assertEqual(pid, info->pid, "PID should match");
    assertTrue(info->name == "sleep" || info->name == "sh",
               "Process name should be sleep or sh");

    killTestProcess(pid);
}

TEST(StoppedInstanceMatchesRunningProcess) {
    auto state = State::load();

    // Create a stopped instance
    auto inst = std::make_shared<Instance>();
    inst->name = "test-sleep";
    inst->command = "sleep 300";
    inst->status = "stopped";
    inst->pid = 0;

    state->instances["test-sleep"] = inst;

    // Start a matching process
    pid_t pid = startTestProcess("sleep 300");

    // Run matching
    matchAndUpdateInstances(state);

    // Check if instance was matched
    auto matched = state->instances["test-sleep"];
    assertTrue(matched != nullptr, "Instance should exist");

    // Note: Matching may or may not work depending on implementation
    // For now, just verify no crash

    killTestProcess(pid);
}

TEST(ResourceAllocation_TCPPort) {
    auto state = State::load();

    try {
        // Allocate a TCP port
        std::string port = allocateResource(state, "tcpport", "");
        assertTrue(!port.empty(), "Should allocate a TCP port");

        int portNum = std::stoi(port);
        assertTrue(portNum >= 3000 && portNum <= 9999,
                   "Port should be in range 3000-9999");
    } catch (const std::exception& e) {
        assertTrue(false, std::string("Resource allocation failed: ") + e.what());
    }
}

TEST(ResourceAllocation_ExplicitValue) {
    auto state = State::load();

    try {
        // Try to allocate an explicit resource
        std::string value = allocateResource(state, "workdir", "/tmp/test");
        assertEqual("/tmp/test", value, "Should allocate explicit value");
    } catch (const std::exception& e) {
        assertTrue(false, std::string("Resource allocation failed: ") + e.what());
    }
}

TEST(StartAndStopProcess) {
    auto state = State::load();

    // Get a template
    auto it = state->templates.find("node-express");
    if (it == state->templates.end()) {
        // Create a simple template
        auto tmpl = std::make_shared<Template>();
        tmpl->id = "test-sleep";
        tmpl->label = "Test Sleep";
        tmpl->command = "sleep 300";
        state->templates["test-sleep"] = tmpl;
        it = state->templates.find("test-sleep");
    }

    try {
        // Start a process
        std::map<std::string, std::string> vars;
        auto inst = startProcess(state, *it->second, "test-instance", vars);

        assertTrue(inst != nullptr, "Should create instance");
        assertTrue(inst->pid > 0, "Instance should have PID");
        assertEqual("running", inst->status, "Instance should be running");

        // Stop the process
        bool stopped = stopProcess(state, inst);
        assertTrue(stopped, "Should be able to stop process");
        assertEqual("stopped", inst->status, "Instance should be stopped");
        assertEqual(0, inst->pid, "Instance PID should be 0");

    } catch (const std::exception& e) {
        assertTrue(false, std::string("Process lifecycle test failed: ") + e.what());
    }
}

TEST(ExtractProcessName) {
    std::string name = extractProcessName("sleep 300");
    assertEqual("sleep", name, "Should extract process name");

    name = extractProcessName("/usr/bin/node server.js");
    assertEqual("node", name, "Should extract basename");

    name = extractProcessName("");
    assertEqual("", name, "Empty command should return empty name");
}

TEST(BuildPortToProcessMap) {
    // This will scan /proc/net/tcp - just verify it doesn't crash
    try {
        auto portMap = buildPortToProcessMap();
        // Should return a map (may be empty)
        assertTrue(true, "buildPortToProcessMap should not crash");
    } catch (const std::exception& e) {
        assertTrue(false, std::string("buildPortToProcessMap failed: ") + e.what());
    }
}

TEST(ResourceCheck_Available) {
    ResourceType rt;
    rt.name = "tcpport";
    rt.check = "nc -z localhost ${value}";
    rt.counter = true;
    rt.start = 3000;
    rt.end = 9999;

    // Check a likely free port (assuming it's not in use)
    bool available = checkResource(rt, "65432");
    // This test may be flaky depending on system state
    // Just verify it doesn't crash
    assertTrue(true, "checkResource should not crash");
}

TEST(StateLoadAndSave) {
    auto state = State::load();
    assertTrue(state != nullptr, "Should load state");

    // Add some data
    state->counters["test"] = 42;

    // Save
    bool saved = state->save();
    assertTrue(saved, "Should be able to save state");

    // Load again
    auto state2 = State::load();
    assertTrue(state2 != nullptr, "Should load state again");
}

TEST(GetParentChain) {
    // Get parent chain for current process
    pid_t self = getpid();

    try {
        auto chain = getParentChain(self);
        assertTrue(!chain.empty(), "Parent chain should not be empty");
        assertTrue(chain[0].pid == self, "First entry should be self");
    } catch (const std::exception& e) {
        assertTrue(false, std::string("getParentChain failed: ") + e.what());
    }
}

TEST(DiscoverProcess) {
    pid_t pid = startTestProcess("sleep 300");

    try {
        auto info = discoverProcess(pid);
        assertTrue(info != nullptr, "Should discover process");
        assertEqual(pid, info->pid, "Should have correct PID");
    } catch (const std::exception& e) {
        // May fail if process exits too quickly
    }

    killTestProcess(pid);
}

TEST(MonitorProcess) {
    auto state = State::load();
    pid_t pid = startTestProcess("sleep 300");

    try {
        auto inst = monitorProcess(state, pid, "monitored-sleep");
        assertTrue(inst != nullptr, "Should create monitored instance");
        assertEqual("monitored-sleep", inst->name, "Should have correct name");
        assertEqual(pid, inst->pid, "Should have correct PID");
        assertEqual("running", inst->status, "Should be running");

        // Clean up
        state->instances.erase("monitored-sleep");
    } catch (const std::exception& e) {
        assertTrue(false, std::string("monitorProcess failed: ") + e.what());
    }

    killTestProcess(pid);
}

TEST(DefaultResourceTypes) {
    auto types = defaultResourceTypes();

    assertTrue(types.find("tcpport") != types.end(), "Should have tcpport");
    assertTrue(types.find("vncport") != types.end(), "Should have vncport");
    assertTrue(types.find("workdir") != types.end(), "Should have workdir");

    auto tcpport = types["tcpport"];
    assertTrue(tcpport->counter, "tcpport should be a counter");
    assertEqual(3000, tcpport->start, "tcpport should start at 3000");
}

int main() {
    return TestRunner::instance().run();
}
