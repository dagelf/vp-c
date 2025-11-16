package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
)

//go:embed web.html
var webHTML string

// corsMiddleware adds CORS headers to allow cross-origin requests
func corsMiddleware(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		// Allow all origins
		origin := r.Header.Get("Origin")
		if origin != "" {
			w.Header().Set("Access-Control-Allow-Origin", origin)
		} else {
			w.Header().Set("Access-Control-Allow-Origin", "*")
		}
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
		w.Header().Set("Access-Control-Allow-Credentials", "true")

		// Handle preflight OPTIONS request
		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusOK)
			return
		}

		next(w, r)
	}
}

// ServeHTTP starts the HTTP server
func ServeHTTP(addr string) error {
	// Web UI
	http.HandleFunc("/", serveWeb)

	// API endpoints with CORS
	http.HandleFunc("/api/instances", corsMiddleware(handleInstances))
	http.HandleFunc("/api/templates", corsMiddleware(handleTemplates))
	http.HandleFunc("/api/resources", corsMiddleware(handleResources))
	http.HandleFunc("/api/resource-types", corsMiddleware(handleResourceTypes))
	http.HandleFunc("/api/discover", corsMiddleware(handleDiscover))
	http.HandleFunc("/api/discover-port", corsMiddleware(handleDiscoverPort))
	http.HandleFunc("/api/config", corsMiddleware(handleConfig))
	http.HandleFunc("/api/monitor", corsMiddleware(handleMonitor))
	http.HandleFunc("/api/execute-action", corsMiddleware(handleExecuteAction))

	return http.ListenAndServe(addr, nil)
}

func serveWeb(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "text/html")
	w.Write([]byte(webHTML))
}

func handleInstances(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	switch r.Method {
	case "GET":
		// Run discovery and matching to update instance status and PIDs
		MatchAndUpdateInstances(state)
		json.NewEncoder(w).Encode(state.Instances)

	case "POST":
		var req struct {
			Action     string            `json:"action"` // "start" or "stop"
			Template   string            `json:"template"`
			Name       string            `json:"name"`
			Vars       map[string]string `json:"vars"`
			InstanceID string            `json:"instance_id"`
		}

		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}

		switch req.Action {
		case "start":
			tmpl := state.Templates[req.Template]
			if tmpl == nil {
				http.Error(w, "template not found", http.StatusNotFound)
				return
			}

			inst, err := StartProcess(state, tmpl, req.Name, req.Vars)
			if err != nil {
				http.Error(w, err.Error(), http.StatusBadRequest)
				return
			}

			json.NewEncoder(w).Encode(inst)

		case "stop":
			inst := state.Instances[req.InstanceID]
			if inst == nil {
				http.Error(w, "instance not found", http.StatusNotFound)
				return
			}

			if err := StopProcess(state, inst); err != nil {
				http.Error(w, err.Error(), http.StatusBadRequest)
				return
			}

			state.ReleaseResources(req.InstanceID)
			state.Save()

			json.NewEncoder(w).Encode(inst)

		case "delete":
			inst := state.Instances[req.InstanceID]
			if inst == nil {
				http.Error(w, "instance not found", http.StatusNotFound)
				return
			}

			// Stop the process if it's running
			if inst.Status == "running" {
				if err := StopProcess(state, inst); err != nil {
					http.Error(w, fmt.Sprintf("failed to stop process: %v", err), http.StatusInternalServerError)
					return
				}
			}

			state.ReleaseResources(req.InstanceID)
			delete(state.Instances, req.InstanceID)
			state.Save()

			json.NewEncoder(w).Encode(map[string]string{"status": "deleted"})

		case "restart":
			inst := state.Instances[req.InstanceID]
			if inst == nil {
				http.Error(w, "instance not found", http.StatusNotFound)
				return
			}

			if err := RestartProcess(state, inst); err != nil {
				http.Error(w, err.Error(), http.StatusBadRequest)
				return
			}

			json.NewEncoder(w).Encode(inst)

		default:
			http.Error(w, "invalid action", http.StatusBadRequest)
		}

	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func handleTemplates(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	switch r.Method {
	case "GET":
		json.NewEncoder(w).Encode(state.Templates)

	case "POST":
		var tmpl Template
		if err := json.NewDecoder(r.Body).Decode(&tmpl); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}

		state.Templates[tmpl.ID] = &tmpl
		state.Save()

		json.NewEncoder(w).Encode(tmpl)

	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func handleResources(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	if r.Method == "GET" {
		// Group resources by type for better display
		grouped := make(map[string][]Resource)
		for _, res := range state.Resources {
			grouped[res.Type] = append(grouped[res.Type], *res)
		}
		json.NewEncoder(w).Encode(grouped)
	} else {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func handleResourceTypes(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	switch r.Method {
	case "GET":
		json.NewEncoder(w).Encode(state.Types)

	case "POST":
		var rt ResourceType
		if err := json.NewDecoder(r.Body).Decode(&rt); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}

		// Validate required fields
		if rt.Name == "" {
			http.Error(w, "name is required", http.StatusBadRequest)
			return
		}

		// Convert name to lowercase for consistency
		rt.Name = strings.ToLower(rt.Name)

		state.Types[rt.Name] = &rt
		state.Save()

		json.NewEncoder(w).Encode(rt)

	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func handleConfig(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	switch r.Method {
	case "GET":
		// Return entire state as JSON
		json.NewEncoder(w).Encode(state)

	case "POST":
		// Replace entire state with provided JSON
		var newState State
		if err := json.NewDecoder(r.Body).Decode(&newState); err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}

		// Validate that maps are initialized
		if newState.Instances == nil {
			newState.Instances = make(map[string]*Instance)
		}
		if newState.Templates == nil {
			newState.Templates = make(map[string]*Template)
		}
		if newState.Resources == nil {
			newState.Resources = make(map[string]*Resource)
		}
		if newState.Counters == nil {
			newState.Counters = make(map[string]int)
		}
		if newState.Types == nil {
			newState.Types = make(map[string]*ResourceType)
		}
		if newState.RemotesAllowed == nil {
			newState.RemotesAllowed = make(map[string]bool)
		}

		// Update global state
		state.Instances = newState.Instances
		state.Templates = newState.Templates
		state.Resources = newState.Resources
		state.Counters = newState.Counters
		state.Types = newState.Types
		state.RemotesAllowed = newState.RemotesAllowed

		// Save to disk
		state.Save()

		json.NewEncoder(w).Encode(map[string]string{"status": "saved"})

	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

func handleMonitor(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	if r.Method != "POST" {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		PID  int    `json:"pid"`
		Name string `json:"name"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	inst, err := MonitorProcess(state, req.PID, req.Name)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	json.NewEncoder(w).Encode(inst)
}

func handleDiscover(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	if r.Method != "GET" {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Parse query parameters
	portsOnly := r.URL.Query().Get("ports_only") != "false"

	processes, err := DiscoverProcesses(state, portsOnly)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	json.NewEncoder(w).Encode(processes)
}

func handleDiscoverPort(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	if r.Method != "POST" {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		Port int    `json:"port"`
		Name string `json:"name"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	inst, err := DiscoverAndImportProcessOnPort(state, req.Port, req.Name)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	json.NewEncoder(w).Encode(inst)
}

func handleExecuteAction(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")

	if r.Method != "POST" {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Get origin from request
	origin := r.Header.Get("Origin")
	if origin == "" {
		// If no origin header, it's a same-origin request (allow)
		origin = "localhost"
	}

	// Check if origin is allowed
	state.mu.Lock()
	allowed, exists := state.RemotesAllowed[origin]
	if !exists {
		// First time seeing this origin - add it as blocked
		state.RemotesAllowed[origin] = false
		state.Save()
		state.mu.Unlock()

		http.Error(w, fmt.Sprintf("Remote origin '%s' not allowed. Enable it in configuration under remotes_allowed to execute actions.", origin), http.StatusForbidden)
		return
	}
	state.mu.Unlock()

	if !allowed {
		http.Error(w, fmt.Sprintf("Remote origin '%s' is blocked. Set to true in configuration under remotes_allowed to execute actions.", origin), http.StatusForbidden)
		return
	}

	var req struct {
		InstanceName string `json:"instance_name"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	inst := state.Instances[req.InstanceName]
	if inst == nil {
		http.Error(w, "instance not found", http.StatusNotFound)
		return
	}

	if inst.Action == "" {
		http.Error(w, "no action defined for this instance", http.StatusBadRequest)
		return
	}

	// Execute the action command
	err := ExecuteAction(inst.Action)
	if err != nil {
		http.Error(w, fmt.Sprintf("failed to execute action: %v", err), http.StatusInternalServerError)
		return
	}

	json.NewEncoder(w).Encode(map[string]string{"status": "executed", "action": inst.Action})
}
