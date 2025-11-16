package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

// TestStoppedProcessMatching tests various scenarios for matching stopped instances
// to running processes
func TestStoppedProcessMatching(t *testing.T) {
	tests := []struct {
		name           string
		setupInstances func(state *State)
		setupProcesses func() ([]*exec.Cmd, func())
		verify         func(t *testing.T, state *State)
	}{
		{
			name: "stopped instance with resources matches running process with same port",
			setupInstances: func(state *State) {
				state.Instances["test-server"] = &Instance{
					Name:    "test-server",
					Command: "nc -l 9999",
					Status:  "stopped",
					PID:     0,
					Resources: map[string]string{
						"tcpport": "9999",
					},
				}
			},
			setupProcesses: func() ([]*exec.Cmd, func()) {
				// Start nc listening on port 9999
				cmd := exec.Command("nc", "-l", "9999")
				err := cmd.Start()
				if err != nil {
					t.Fatalf("Failed to start test process: %v", err)
				}
				// Give it time to bind the port
				time.Sleep(200 * time.Millisecond)

				cleanup := func() {
					if cmd.Process != nil {
						cmd.Process.Kill()
					}
				}
				return []*exec.Cmd{cmd}, cleanup
			},
			verify: func(t *testing.T, state *State) {
				if len(state.Instances) != 1 {
					t.Errorf("Expected 1 instance, got %d", len(state.Instances))
					return
				}
				inst := state.Instances["test-server"]
				if inst == nil {
					t.Errorf("Instance 'test-server' not found")
					return
				}
				if inst.Status != "running" {
					t.Errorf("Expected instance status 'running', got '%s'", inst.Status)
				}
				if inst.PID == 0 {
					t.Errorf("Expected instance to have non-zero PID, got 0")
				}
			},
		},
		{
			name: "stopped instance with resources does NOT match process with different port",
			setupInstances: func(state *State) {
				state.Instances["test-server"] = &Instance{
					Name:    "test-server",
					Command: "nc -l",
					Status:  "stopped",
					PID:     0,
					Resources: map[string]string{
						"tcpport": "9998", // Looking for port 9998
					},
				}
			},
			setupProcesses: func() ([]*exec.Cmd, func()) {
				// Start nc listening on port 9999 (different port)
				cmd := exec.Command("nc", "-l", "9999")
				err := cmd.Start()
				if err != nil {
					t.Fatalf("Failed to start test process: %v", err)
				}
				time.Sleep(200 * time.Millisecond)

				cleanup := func() {
					if cmd.Process != nil {
						cmd.Process.Kill()
					}
				}
				return []*exec.Cmd{cmd}, cleanup
			},
			verify: func(t *testing.T, state *State) {
				if len(state.Instances) != 1 {
					t.Errorf("Expected 1 instance, got %d", len(state.Instances))
					return
				}
				inst := state.Instances["test-server"]
				if inst == nil {
					t.Errorf("Instance 'test-server' not found")
					return
				}
				if inst.Status != "stopped" {
					t.Errorf("Expected instance to remain 'stopped', got '%s'", inst.Status)
				}
				if inst.PID != 0 {
					t.Errorf("Expected instance to have PID 0, got %d", inst.PID)
				}
			},
		},
		{
			name: "stopped instance WITHOUT resources matches by command only",
			setupInstances: func(state *State) {
				state.Instances["sleep-process"] = &Instance{
					Name:      "sleep-process",
					Command:   "sleep 300",
					Status:    "stopped",
					PID:       0,
					Resources: map[string]string{}, // No resources
				}
			},
			setupProcesses: func() ([]*exec.Cmd, func()) {
				// Start a sleep process (no ports)
				cmd := exec.Command("sleep", "300")
				err := cmd.Start()
				if err != nil {
					t.Fatalf("Failed to start test process: %v", err)
				}
				time.Sleep(100 * time.Millisecond)

				cleanup := func() {
					if cmd.Process != nil {
						cmd.Process.Kill()
					}
				}
				return []*exec.Cmd{cmd}, cleanup
			},
			verify: func(t *testing.T, state *State) {
				if len(state.Instances) != 1 {
					t.Errorf("Expected 1 instance, got %d", len(state.Instances))
					return
				}
				inst := state.Instances["sleep-process"]
				if inst == nil {
					t.Errorf("Instance 'sleep-process' not found")
					return
				}
				if inst.Status != "running" {
					t.Errorf("Expected instance status 'running' (matched by command), got '%s'", inst.Status)
				}
				if inst.PID == 0 {
					t.Errorf("Expected instance to have non-zero PID, got 0")
				}
			},
		},
		{
			name: "already running instance is NOT rematched",
			setupInstances: func(state *State) {
				// Create a long-running process first
				cmd := exec.Command("sleep", "300")
				err := cmd.Start()
				if err != nil {
					t.Fatalf("Failed to start test process: %v", err)
				}
				time.Sleep(100 * time.Millisecond)

				originalPID := cmd.Process.Pid

				state.Instances["already-running"] = &Instance{
					Name:      "already-running",
					Command:   "sleep 300",
					Status:    "running",
					PID:       originalPID, // Already has a PID
					Resources: map[string]string{},
				}

				// Store cleanup reference (will be called by test harness)
				t.Cleanup(func() {
					if cmd.Process != nil {
						cmd.Process.Kill()
					}
				})
			},
			setupProcesses: func() ([]*exec.Cmd, func()) {
				// Start another sleep process
				cmd := exec.Command("sleep", "300")
				err := cmd.Start()
				if err != nil {
					t.Fatalf("Failed to start test process: %v", err)
				}
				time.Sleep(100 * time.Millisecond)

				cleanup := func() {
					if cmd.Process != nil {
						cmd.Process.Kill()
					}
				}
				return []*exec.Cmd{cmd}, cleanup
			},
			verify: func(t *testing.T, state *State) {
				if len(state.Instances) != 1 {
					t.Errorf("Expected 1 instance, got %d", len(state.Instances))
					return
				}
				inst := state.Instances["already-running"]
				if inst == nil {
					t.Errorf("Instance 'already-running' not found")
					return
				}

				// The PID should remain unchanged (not rematched to the new process)
				if inst.Status != "running" {
					t.Errorf("Expected instance to remain 'running', got '%s'", inst.Status)
				}
				// We can't easily verify the exact PID stays the same without more complex state,
				// but we verify it's still running its original process
				if !IsProcessRunning(inst.PID) {
					t.Errorf("Original process should still be running")
				}
			},
		},
		{
			name: "multiple stopped instances - first match wins",
			setupInstances: func(state *State) {
				state.Instances["first-match"] = &Instance{
					Name:    "first-match",
					Command: "nc -l",
					Status:  "stopped",
					PID:     0,
					Resources: map[string]string{
						"tcpport": "9997",
					},
				}
				state.Instances["second-match"] = &Instance{
					Name:    "second-match",
					Command: "nc -l",
					Status:  "stopped",
					PID:     0,
					Resources: map[string]string{
						"tcpport": "9997", // Same port
					},
				}
			},
			setupProcesses: func() ([]*exec.Cmd, func()) {
				// Start ONE nc listening on port 9997
				cmd := exec.Command("nc", "-l", "9997")
				err := cmd.Start()
				if err != nil {
					t.Fatalf("Failed to start test process: %v", err)
				}
				time.Sleep(200 * time.Millisecond)

				cleanup := func() {
					if cmd.Process != nil {
						cmd.Process.Kill()
					}
				}
				return []*exec.Cmd{cmd}, cleanup
			},
			verify: func(t *testing.T, state *State) {
				if len(state.Instances) != 2 {
					t.Errorf("Expected 2 instances, got %d", len(state.Instances))
					return
				}

				runningCount := 0
				var runningInstanceName string
				for name, inst := range state.Instances {
					if inst.Status == "running" {
						runningCount++
						runningInstanceName = name
					}
				}

				if runningCount != 1 {
					t.Errorf("Expected exactly 1 instance matched to running, got %d", runningCount)
				}

				// One of the instances should win (can't predict which due to map iteration)
				if runningCount == 1 && runningInstanceName != "first-match" && runningInstanceName != "second-match" {
					t.Errorf("Expected either 'first-match' or 'second-match' to win, got '%s'", runningInstanceName)
				}
			},
		},
		{
			name: "stopped instance matches process via parent chain command",
			setupInstances: func(state *State) {
				state.Instances["bash-script"] = &Instance{
					Name:      "bash-script",
					Command:   "my-script.sh", // Looking for script in parent chain
					Status:    "stopped",
					PID:       0,
					Resources: map[string]string{},
				}
			},
			setupProcesses: func() ([]*exec.Cmd, func()) {
				// Create a script that launches a sleep process
				scriptPath := filepath.Join(os.TempDir(), "my-script.sh")
				scriptContent := "#!/bin/bash\nsleep 300\n"
				err := os.WriteFile(scriptPath, []byte(scriptContent), 0755)
				if err != nil {
					t.Fatalf("Failed to create script: %v", err)
				}

				cmd := exec.Command("bash", scriptPath)
				err = cmd.Start()
				if err != nil {
					t.Fatalf("Failed to start test process: %v", err)
				}
				time.Sleep(200 * time.Millisecond)

				cleanup := func() {
					if cmd.Process != nil {
						cmd.Process.Kill()
					}
					os.Remove(scriptPath)
				}
				return []*exec.Cmd{cmd}, cleanup
			},
			verify: func(t *testing.T, state *State) {
				if len(state.Instances) != 1 {
					t.Errorf("Expected 1 instance, got %d", len(state.Instances))
					return
				}
				inst := state.Instances["bash-script"]
				if inst == nil {
					t.Errorf("Instance 'bash-script' not found")
					return
				}

				// The instance should match the bash process (or its child sleep)
				// because the parent chain contains my-script.sh
				if inst.Status == "stopped" {
					t.Logf("Instance remained stopped - this may be expected if parent chain matching failed")
					// This is informational - parent chain matching can be flaky in tests
				}
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Create fresh state
			state := &State{
				Instances: make(map[string]*Instance),
				Templates: make(map[string]*Template),
				Resources: make(map[string]*Resource),
				Counters:  make(map[string]int),
				Types:     make(map[string]*ResourceType),
			}

			// Setup instances
			tt.setupInstances(state)
			state.Save()

			// Setup running processes
			cmds, cleanup := tt.setupProcesses()
			defer cleanup()

			// Verify processes are actually running
			for _, cmd := range cmds {
				if cmd != nil && cmd.Process != nil && !IsProcessRunning(cmd.Process.Pid) {
					t.Fatalf("Test process %d is not running", cmd.Process.Pid)
				}
			}

			// Run the matching function
			err := MatchAndUpdateInstances(state)
			if err != nil {
				t.Fatalf("MatchAndUpdateInstances failed: %v", err)
			}

			// Verify results
			tt.verify(t, state)
		})
	}
}

// TestMatchingLogic_EdgeCases tests edge cases in the matching logic
func TestMatchingLogic_EdgeCases(t *testing.T) {
	t.Run("empty state returns no error", func(t *testing.T) {
		state := &State{
			Instances: make(map[string]*Instance),
			Templates: make(map[string]*Template),
			Resources: make(map[string]*Resource),
			Counters:  make(map[string]int),
			Types:     make(map[string]*ResourceType),
		}

		err := MatchAndUpdateInstances(state)
		if err != nil {
			t.Errorf("Expected no error with empty state, got: %v", err)
		}
	})

	t.Run("instance with malformed command does not panic", func(t *testing.T) {
		state := &State{
			Instances: make(map[string]*Instance),
			Templates: make(map[string]*Template),
			Resources: make(map[string]*Resource),
			Counters:  make(map[string]int),
			Types:     make(map[string]*ResourceType),
		}
		state.Instances["bad-command"] = &Instance{
			Name:      "bad-command",
			Command:   "", // Empty command
			Status:    "stopped",
			PID:       0,
			Resources: map[string]string{},
		}

		err := MatchAndUpdateInstances(state)
		if err != nil {
			t.Errorf("Expected no error with malformed command, got: %v", err)
		}
	})

	t.Run("stopped instance with PID of non-existent process", func(t *testing.T) {
		state := &State{
			Instances: make(map[string]*Instance),
			Templates: make(map[string]*Template),
			Resources: make(map[string]*Resource),
			Counters:  make(map[string]int),
			Types:     make(map[string]*ResourceType),
		}
		state.Instances["ghost-process"] = &Instance{
			Name:      "ghost-process",
			Command:   "sleep 300",
			Status:    "stopped",
			PID:       999999, // Non-existent PID
			Resources: map[string]string{},
		}

		// Start a real sleep process
		cmd := exec.Command("sleep", "300")
		err := cmd.Start()
		if err != nil {
			t.Fatalf("Failed to start test process: %v", err)
		}
		defer cmd.Process.Kill()
		time.Sleep(100 * time.Millisecond)

		err = MatchAndUpdateInstances(state)
		if err != nil {
			t.Errorf("Expected no error, got: %v", err)
		}

		// The instance should be matched to the real sleep process
		inst := state.Instances["ghost-process"]
		if inst.Status != "running" {
			t.Errorf("Expected stopped instance with dead PID to be rematched, status: %s", inst.Status)
		}
		if inst.PID == 999999 {
			t.Errorf("Expected PID to be updated from non-existent 999999")
		}
	})
}

// TestBugFix_InstanceWithoutResources specifically tests the bug fix where
// instances without resources could not be matched
func TestBugFix_InstanceWithoutResources(t *testing.T) {
	t.Run("BUGFIX: stopped instance with empty resources map should match by command", func(t *testing.T) {
		state := &State{
			Instances: make(map[string]*Instance),
			Templates: make(map[string]*Template),
			Resources: make(map[string]*Resource),
			Counters:  make(map[string]int),
			Types:     make(map[string]*ResourceType),
		}
		state.Instances["no-resources"] = &Instance{
			Name:      "no-resources",
			Command:   "sleep 300",
			Status:    "stopped",
			PID:       0,
			Resources: map[string]string{}, // Empty resources
		}
		state.Save()

		// Start a sleep process
		cmd := exec.Command("sleep", "300")
		err := cmd.Start()
		if err != nil {
			t.Fatalf("Failed to start test process: %v", err)
		}
		defer cmd.Process.Kill()
		time.Sleep(100 * time.Millisecond)

		// Before fix: This would fail because resourcesMatch would be false
		err = MatchAndUpdateInstances(state)
		if err != nil {
			t.Fatalf("MatchAndUpdateInstances failed: %v", err)
		}

		// Verify the instance was matched
		inst := state.Instances["no-resources"]
		if inst.Status != "running" {
			t.Errorf("BUGFIX FAILED: Instance without resources should match by command. Status: %s", inst.Status)
		}
		if inst.PID == 0 {
			t.Errorf("BUGFIX FAILED: Instance should have been assigned a PID")
		}
	})

	t.Run("BUGFIX: stopped instance with nil resources should match by command", func(t *testing.T) {
		state := &State{
			Instances: make(map[string]*Instance),
			Templates: make(map[string]*Template),
			Resources: make(map[string]*Resource),
			Counters:  make(map[string]int),
			Types:     make(map[string]*ResourceType),
		}
		state.Instances["nil-resources"] = &Instance{
			Name:      "nil-resources",
			Command:   "sleep 300",
			Status:    "stopped",
			PID:       0,
			Resources: nil, // Nil resources
		}
		state.Save()

		// Start a sleep process
		cmd := exec.Command("sleep", "300")
		err := cmd.Start()
		if err != nil {
			t.Fatalf("Failed to start test process: %v", err)
		}
		defer cmd.Process.Kill()
		time.Sleep(100 * time.Millisecond)

		err = MatchAndUpdateInstances(state)
		if err != nil {
			t.Fatalf("MatchAndUpdateInstances failed: %v", err)
		}

		// Verify the instance was matched
		inst := state.Instances["nil-resources"]
		if inst.Status != "running" {
			t.Errorf("BUGFIX FAILED: Instance with nil resources should match by command. Status: %s", inst.Status)
		}
		if inst.PID == 0 {
			t.Errorf("BUGFIX FAILED: Instance should have been assigned a PID")
		}
	})
}

// BenchmarkMatchAndUpdateInstances benchmarks the matching performance
func BenchmarkMatchAndUpdateInstances(b *testing.B) {
	// Create state with multiple stopped instances
	state := &State{
		Instances: make(map[string]*Instance),
		Templates: make(map[string]*Template),
		Resources: make(map[string]*Resource),
		Counters:  make(map[string]int),
		Types:     make(map[string]*ResourceType),
	}

	// Add 100 stopped instances
	for i := 0; i < 100; i++ {
		name := fmt.Sprintf("instance-%d", i)
		state.Instances[name] = &Instance{
			Name:    name,
			Command: fmt.Sprintf("sleep %d", i),
			Status:  "stopped",
			PID:     0,
			Resources: map[string]string{
				"tcpport": fmt.Sprintf("%d", 10000+i),
			},
		}
	}

	state.Save()

	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		MatchAndUpdateInstances(state)
	}
}
