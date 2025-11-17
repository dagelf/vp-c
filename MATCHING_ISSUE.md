# Process Matching Issue - Root Cause Analysis

## Problem
Stopped instances are NOT being matched to running processes, even when the process name and ports match.

## Root Cause

The C++ implementation of `matchAndUpdateInstances()` (src/process.cpp:385-406) is **incomplete** compared to the Go version.

### Current C++ Implementation (Incomplete)
```cpp
bool matchAndUpdateInstances(std::shared_ptr<State> state) {
    // Step 1: Update CPU time and check if processes are still running
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
```

**What it does:**
1. ✓ Updates CPU time for running instances
2. ✓ Marks instances as stopped if PID is no longer running

**What it's MISSING:**
3. ✗ Does NOT discover running processes
4. ✗ Does NOT match stopped instances to running processes
5. ✗ Does NOT check process names
6. ✗ Does NOT verify ports

### Go Implementation (Complete)
```go
func MatchAndUpdateInstances(state *State) error {
    // Step 1: Check if existing PIDs are still running and update CPU time
    for _, inst := range state.Instances {
        if inst.Status == "running" {
            if IsProcessRunning(inst.PID) {
                if procInfo, err := ReadProcessInfo(inst.PID); err == nil {
                    inst.CPUTime = procInfo.CPUTime
                }
            } else {
                inst.Status = "stopped"
                inst.PID = 0
                inst.CPUTime = 0
            }
        }
    }

    // Step 2: Discover all processes (not just those with ports)
    processes, err := DiscoverProcesses(state, false)
    if err != nil {
        return fmt.Errorf("failed to discover processes: %w", err)
    }

    // Track which PIDs have been matched
    matchedPIDs := make(map[int]bool)

    // Step 3: For each stopped instance, try to find matching process
    for _, inst := range state.Instances {
        if inst.Status != "stopped" {
            continue
        }

        // Extract expected process name
        expectedName := extractProcessName(inst.Command)
        if expectedName == "" {
            continue
        }

        // Try to find matching process
        for _, proc := range processes {
            pid := proc["pid"].(int)

            // Skip if already matched
            if matchedPIDs[pid] {
                continue
            }

            procInfo, _ := ReadProcessInfo(pid)

            // Check if process name matches
            if procInfo.Name != expectedName {
                continue
            }

            // If instance has ports, verify they match
            portsMatch := true
            for resType, resValue := range inst.Resources {
                if resType == "tcpport" || resType == "port" {
                    expectedPort, _ := strconv.Atoi(resValue)
                    if expectedPort > 0 {
                        hasPort := false
                        for _, port := range procInfo.Ports {
                            if port == expectedPort {
                                hasPort = true
                                break
                            }
                        }
                        if !hasPort {
                            portsMatch = false
                            break
                        }
                    }
                }
            }

            if portsMatch {
                // Match found! Update instance
                inst.PID = pid
                inst.Status = "running"
                inst.Started = time.Now().Unix()
                inst.CPUTime = procInfo.CPUTime
                matchedPIDs[pid] = true
                break
            }
        }
    }

    state.Save()
    return nil
}
```

## Missing Functionality

The C++ version needs to implement:

1. **Process Discovery** - Scan /proc to find all running processes
   - Already have `discoverProcesses()` stub at line 378
   - Needs to return list of running processes with their info

2. **Stopped Instance Matching** - For each stopped instance:
   - Extract expected process name from command
   - Find running processes with matching name
   - Verify ports match (if instance has port resources)
   - Update instance with new PID and mark as running

3. **Duplicate Prevention** - Track matched PIDs to prevent multiple instances claiming the same process

## Impact

Without this matching logic:
- Stopped instances NEVER recover when process restarts
- Manual intervention required to reconnect instances
- Process discovery is essentially broken
- The "monitor" feature doesn't work properly

## Example Scenario

**What SHOULD happen:**
1. User starts instance "web-server" (PID 1234, port 3000)
2. Instance is tracked as running
3. Process dies (PID 1234 no longer exists)
4. Instance marked as stopped (PID = 0)
5. User restarts the server manually (new PID 5678, still port 3000)
6. `matchAndUpdateInstances()` discovers the new process
7. Matches by name ("node") and port (3000)
8. Updates instance: PID = 5678, status = "running"

**What ACTUALLY happens:**
1. User starts instance "web-server" (PID 1234, port 3000)
2. Instance is tracked as running
3. Process dies (PID 1234 no longer exists)
4. Instance marked as stopped (PID = 0)
5. User restarts the server manually (new PID 5678, still port 3000)
6. `matchAndUpdateInstances()` does nothing
7. Instance stays stopped forever
8. State file shows stopped even though process is running

## Fix Required

Implement the full matching logic in C++:
- Complete `discoverProcesses()` function
- Add process name extraction and comparison
- Add port verification logic
- Add PID tracking to prevent duplicates
- Update stopped instances when matches are found

## Test Case

See `TEST(StoppedInstanceMatchesRunningProcess)` in src/test_main.cpp - this test currently passes only because it doesn't verify matching actually worked.
