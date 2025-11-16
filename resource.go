package main

import (
	"fmt"
	"os/exec"
	"strconv"
	"strings"
)

// Resource represents an allocated resource
type Resource struct {
	Type  string `json:"type"`  // tcpport|vncport|gpu|license|whatever
	Value string `json:"value"` // "3000" or "/path" or "0"
	Owner string `json:"owner"` // Instance name
}

// ResourceType defines a type of resource with validation
type ResourceType struct {
	Name    string `json:"name"`    // Resource type name
	Check   string `json:"check"`   // Shell command to check availability
	Counter bool   `json:"counter"` // Is this auto-incrementing?
	Start   int    `json:"start"`   // Counter start value
	End     int    `json:"end"`     // Counter end value
}

// DefaultResourceTypes returns the built-in resource types
func DefaultResourceTypes() map[string]*ResourceType {
	return map[string]*ResourceType{
		"tcpport": {
			Name:    "tcpport",
			Check:   "nc -z localhost ${value}", // Exits 0 if in use, 1 if available
			Counter: true,
			Start:   3000,
			End:     9999,
		},
		"vncport": {
			Name:    "vncport",
			Check:   "nc -z localhost ${value}", // Exits 0 if in use, 1 if available
			Counter: true,
			Start:   5900,
			End:     5999,
		},
		"serialport": {
			Name:    "serialport",
			Check:   "nc -z localhost ${value}", // Exits 0 if in use, 1 if available
			Counter: true,
			Start:   9600,
			End:     9699,
		},
		"dbfile": {
			Name:    "dbfile",
			Check:   "test -f ${value}", // Exits 0 if exists, 1 if available
			Counter: false,
		},
		"socket": {
			Name:    "socket",
			Check:   "test -S ${value}", // Exits 0 if exists, 1 if available
			Counter: false,
		},
		"datadir": {
			Name:    "datadir",
			Check:   "", // No check - always available
			Counter: false,
		},
		"workdir": {
			Name:    "workdir",
			Check:   "", // No check - always available (informational resource)
			Counter: false,
		},
	}
}

// AllocateResource allocates a resource of the given type
func AllocateResource(state *State, rtype string, requestedValue string) (string, error) {
	rt := state.Types[rtype]
	if rt == nil {
		return "", fmt.Errorf("unknown resource type: %s", rtype)
	}

	var value string

	if rt.Counter && requestedValue == "" {
		// Auto-increment counter
		current := state.Counters[rtype]
		if current == 0 {
			current = rt.Start
		}

		found := false
		for v := current; v <= rt.End; v++ {
			value = strconv.Itoa(v)
			if CheckResource(rt, value) {
				state.Counters[rtype] = v + 1
				found = true
				break
			}
		}

		if !found {
			return "", fmt.Errorf("no available %s in range %d-%d", rtype, rt.Start, rt.End)
		}
	} else {
		// Explicit value requested or non-counter resource
		if requestedValue != "" {
			value = requestedValue
		} else {
			return "", fmt.Errorf("resource type %s requires explicit value", rtype)
		}

		if !CheckResource(rt, value) {
			return "", fmt.Errorf("%s %s not available", rtype, value)
		}
	}

	return value, nil
}

// CheckResource validates resource availability using the check command
func CheckResource(rt *ResourceType, value string) bool {
	if rt.Check == "" {
		return true // No check command = always available
	}

	// Interpolate check command
	check := strings.ReplaceAll(rt.Check, "${value}", value)

	// Execute check
	cmd := exec.Command("sh", "-c", check)
	err := cmd.Run()
	// Natural command behavior: exit 0 = exists/in-use (not available)
	// exit 1 = free/doesn't exist (available)
	return err != nil // Resource is available if check command fails
}
