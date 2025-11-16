package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/fsnotify/fsnotify"
)

// State holds all application state
type State struct {
	mu             sync.RWMutex               // Protects concurrent access to state
	Instances      map[string]*Instance       `json:"instances"`       // name -> Instance
	Templates      map[string]*Template       `json:"templates"`       // id -> Template
	Resources      map[string]*Resource       `json:"resources"`       // type:value -> Resource
	Counters       map[string]int             `json:"counters"`        // counter_name -> current
	Types          map[string]*ResourceType   `json:"types"`           // Resource type definitions
	RemotesAllowed map[string]bool            `json:"remotes_allowed"` // origin -> allowed (true=can execute, false=blocked)
}

// LoadState loads state from ~/.config/vp/state.json
func LoadState() *State {
	homeDir, err := os.UserHomeDir()
	if err != nil {
		// Fallback to /tmp if home directory cannot be determined
		homeDir = "/tmp"
	}
	stateFile := filepath.Join(homeDir, ".config/vp/", "state.json")

	data, err := os.ReadFile(stateFile)
	if err != nil {
		// Initialize with defaults
		return &State{
			Instances:      make(map[string]*Instance),
			Templates:      loadDefaultTemplates(),
			Resources:      make(map[string]*Resource),
			Counters:       make(map[string]int),
			Types:          DefaultResourceTypes(),
			RemotesAllowed: make(map[string]bool),
		}
	}

	var s State
	if err := json.Unmarshal(data, &s); err != nil {
		// Return defaults on parse error
		return &State{
			Instances:      make(map[string]*Instance),
			Templates:      loadDefaultTemplates(),
			Resources:      make(map[string]*Resource),
			Counters:       make(map[string]int),
			Types:          DefaultResourceTypes(),
			RemotesAllowed: make(map[string]bool),
		}
	}

	// Merge with default types (in case new defaults were added)
	if s.Types == nil {
		s.Types = make(map[string]*ResourceType)
	}
	for name, rt := range DefaultResourceTypes() {
		if s.Types[name] == nil {
			s.Types[name] = rt
		}
	}

	// Ensure maps are initialized
	if s.Instances == nil {
		s.Instances = make(map[string]*Instance)
	}
	if s.Templates == nil {
		s.Templates = loadDefaultTemplates()
	}
	if s.Resources == nil {
		s.Resources = make(map[string]*Resource)
	}
	if s.Counters == nil {
		s.Counters = make(map[string]int)
	}
	if s.RemotesAllowed == nil {
		s.RemotesAllowed = make(map[string]bool)
	}

	return &s
}

// Save persists state to ~/.vibeprocess/state.json
func (s *State) Save() error {
	s.mu.RLock()
	defer s.mu.RUnlock()

	homeDir, err := os.UserHomeDir()
	if err != nil {
		// Fallback to /tmp if home directory cannot be determined
		homeDir = "/tmp"
	}
	stateDir := filepath.Join(homeDir, ".vibeprocess")

	// Create directory if it doesn't exist
	if err := os.MkdirAll(stateDir, 0755); err != nil {
		return err
	}

	stateFile := filepath.Join(stateDir, "state.json")
	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(stateFile, data, 0600)
}

// ClaimResource claims a resource for an instance
func (s *State) ClaimResource(rtype, value, owner string) {
	s.mu.Lock()
	defer s.mu.Unlock()

	key := rtype + ":" + value
	s.Resources[key] = &Resource{
		Type:  rtype,
		Value: value,
		Owner: owner,
	}
}

// ReleaseResources releases all resources owned by an instance
func (s *State) ReleaseResources(owner string) {
	s.mu.Lock()
	defer s.mu.Unlock()

	for key, res := range s.Resources {
		if res.Owner == owner {
			delete(s.Resources, key)
		}
	}
}

// loadDefaultTemplates returns default templates
func loadDefaultTemplates() map[string]*Template {
	return map[string]*Template{
		"postgres": {
			ID:        "postgres",
			Label:     "PostgreSQL Database",
			Command:   "postgres -D ${datadir} -p ${tcpport}",
			Resources: []string{"tcpport", "datadir"},
			Vars: map[string]string{
				"datadir": "/tmp/pgdata",
			},
		},
		"node-express": {
			ID:        "node-express",
			Label:     "Node.js Express Server",
			Command:   "node server.js --port ${tcpport}",
			Resources: []string{"tcpport"},
			Vars:      map[string]string{},
		},
		"qemu": {
			ID:        "qemu",
			Label:     "QEMU Virtual Machine",
			Command:   "qemu-system-x86_64 -vnc :${vncport} -serial tcp::${serialport},server,nowait ${args}",
			Resources: []string{"vncport", "serialport"},
			Vars: map[string]string{
				"args": "-m 2G",
			},
		},
	}
}

// WatchConfig watches the state file for changes and reloads it automatically
func (s *State) WatchConfig() error {
	homeDir, err := os.UserHomeDir()
	if err != nil {
		homeDir = "/tmp"
	}
	stateFile := filepath.Join(homeDir, ".vibeprocess", "state.json")

	watcher, err := fsnotify.NewWatcher()
	if err != nil {
		return fmt.Errorf("failed to create watcher: %w", err)
	}

	// Watch the state file
	err = watcher.Add(stateFile)
	if err != nil {
		// If file doesn't exist yet, watch the directory instead
		stateDir := filepath.Join(homeDir, ".vibeprocess")
		if err := os.MkdirAll(stateDir, 0755); err != nil {
			return fmt.Errorf("failed to create state directory: %w", err)
		}
		err = watcher.Add(stateDir)
		if err != nil {
			return fmt.Errorf("failed to watch state directory: %w", err)
		}
	}

	fmt.Println("Started watching config file for changes:", stateFile)

	go func() {
		defer watcher.Close()

		// Debounce timer to avoid reloading multiple times for rapid changes
		var debounceTimer *time.Timer

		for {
			select {
			case event, ok := <-watcher.Events:
				if !ok {
					return
				}

				// Only reload on Write or Create events for the state file
				if (event.Op&fsnotify.Write == fsnotify.Write || event.Op&fsnotify.Create == fsnotify.Create) &&
					filepath.Base(event.Name) == "state.json" {

					// Debounce: wait 100ms before reloading to group rapid changes
					if debounceTimer != nil {
						debounceTimer.Stop()
					}

					debounceTimer = time.AfterFunc(100*time.Millisecond, func() {
						fmt.Println("Config file changed, reloading...")

						// Load the new state
						newState := LoadState()

						// Update the global state with proper locking
						s.mu.Lock()
						s.Instances = newState.Instances
						s.Templates = newState.Templates
						s.Resources = newState.Resources
						s.Counters = newState.Counters
						s.Types = newState.Types
						s.mu.Unlock()

						fmt.Println("Config reloaded successfully")
					})
				}

			case err, ok := <-watcher.Errors:
				if !ok {
					return
				}
				fmt.Printf("Config watcher error: %v\n", err)
			}
		}
	}()

	return nil
}
