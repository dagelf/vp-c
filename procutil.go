package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

// portCache caches port-to-PID mappings to avoid repeated /proc/net/tcp reads
type portCache struct {
	sync.RWMutex
	mapping   map[int][]int // port -> []pid
	timestamp time.Time
	ttl       time.Duration
}

var globalPortCache = &portCache{
	mapping: make(map[int][]int),
	ttl:     500 * time.Millisecond, // Cache for 500ms
}

// processInfoCache caches ProcessInfo to avoid redundant /proc reads
type processInfoCache struct {
	sync.RWMutex
	cache     map[int]*ProcessInfo // pid -> ProcessInfo
	timestamp map[int]time.Time    // pid -> last read time
	ttl       time.Duration
}

var globalProcessCache = &processInfoCache{
	cache:     make(map[int]*ProcessInfo),
	timestamp: make(map[int]time.Time),
	ttl:       1 * time.Second, // Cache process info for 1 second
}

// ProcessInfo contains detailed information about a discovered process
type ProcessInfo struct {
	PID     int               `json:"pid"`
	PPID    int               `json:"ppid"`   // Parent process ID
	Name    string            `json:"name"`   // Process name
	Cmdline string            `json:"cmdline"` // Full command line
	Exe     string            `json:"exe"`    // Executable path
	Cwd     string            `json:"cwd"`    // Working directory
	Environ map[string]string `json:"environ"` // Environment variables
	Ports   []int             `json:"ports"`  // TCP ports this process listens on
	CPUTime float64           `json:"cputime"` // CPU time in seconds
}

// ShellNames contains common shell executable names
var ShellNames = map[string]bool{
	"sh":      true,
	"bash":    true,
	"zsh":     true,
	"fish":    true,
	"dash":    true,
	"ksh":     true,
	"tcsh":    true,
	"csh":     true,
}

// buildPortToProcessMap builds a map of all listening ports to PIDs (optimized version)
func buildPortToProcessMap() (map[int][]int, error) {
	// Check cache first
	globalPortCache.RLock()
	if time.Since(globalPortCache.timestamp) < globalPortCache.ttl {
		result := make(map[int][]int)
		for k, v := range globalPortCache.mapping {
			result[k] = v
		}
		globalPortCache.RUnlock()
		return result, nil
	}
	globalPortCache.RUnlock()

	// Build new mapping
	portToPIDs := make(map[int][]int)
	inodeToPort := make(map[string]int)

	// Parse /proc/net/tcp and /proc/net/tcp6 once
	for _, tcpFile := range []string{"/proc/net/tcp", "/proc/net/tcp6"} {
		file, err := os.Open(tcpFile)
		if err != nil {
			continue
		}

		scanner := bufio.NewScanner(file)
		scanner.Scan() // Skip header

		for scanner.Scan() {
			fields := strings.Fields(scanner.Text())
			if len(fields) < 10 {
				continue
			}

			// Field 3 is connection state (0A = LISTEN)
			if fields[3] != "0A" {
				continue
			}

			// Parse port from local_address (IP:PORT in hex)
			localAddr := fields[1]
			parts := strings.Split(localAddr, ":")
			if len(parts) != 2 {
				continue
			}

			portNum, err := strconv.ParseInt(parts[1], 16, 64)
			if err != nil {
				continue
			}

			// Store inode -> port mapping
			inode := fields[9]
			inodeToPort[inode] = int(portNum)
		}
		file.Close()
	}

	// Now scan /proc to find PIDs for each inode (batched approach)
	procDir, err := os.Open("/proc")
	if err != nil {
		return nil, err
	}
	defer procDir.Close()

	entries, err := procDir.Readdirnames(-1)
	if err != nil {
		return nil, err
	}

	for _, entry := range entries {
		// Check if entry is a PID (numeric)
		pid, err := strconv.Atoi(entry)
		if err != nil {
			continue
		}

		// Read all FDs for this PID
		fdDir := filepath.Join("/proc", entry, "fd")
		fds, err := os.ReadDir(fdDir)
		if err != nil {
			continue
		}

		for _, fd := range fds {
			link, err := os.Readlink(filepath.Join(fdDir, fd.Name()))
			if err != nil {
				continue
			}

			// Check if it's a socket
			if !strings.HasPrefix(link, "socket:[") {
				continue
			}

			inode := strings.TrimPrefix(link, "socket:[")
			inode = strings.TrimSuffix(inode, "]")

			// Check if this inode corresponds to a listening port
			if port, exists := inodeToPort[inode]; exists {
				portToPIDs[port] = append(portToPIDs[port], pid)
			}
		}
	}

	// Update cache
	globalPortCache.Lock()
	globalPortCache.mapping = portToPIDs
	globalPortCache.timestamp = time.Now()
	globalPortCache.Unlock()

	return portToPIDs, nil
}

// isKernelThread checks if a process is a kernel thread
func isKernelThread(pid int, cmdline string) bool {
	// Kernel threads have empty cmdline and PPID of 2 (kthreadd)
	// or they are PID 2 itself
	if pid == 2 {
		return true
	}
	if cmdline == "" {
		// Read PPID to confirm
		statPath := fmt.Sprintf("/proc/%d/stat", pid)
		statData, err := os.ReadFile(statPath)
		if err != nil {
			return false
		}
		statStr := string(statData)
		lastParen := strings.LastIndex(statStr, ")")
		if lastParen != -1 {
			fields := strings.Fields(statStr[lastParen+1:])
			if len(fields) >= 2 {
				ppid, _ := strconv.Atoi(fields[1])
				// Kernel threads have PPID of 2 or 0
				if ppid == 2 || ppid == 0 {
					return true
				}
			}
		}
	}
	return false
}

// ReadProcessInfo reads process information from /proc/[pid] (optimized version with caching)
func ReadProcessInfo(pid int) (*ProcessInfo, error) {
	// Check cache first
	globalProcessCache.RLock()
	if cached, exists := globalProcessCache.cache[pid]; exists {
		if time.Since(globalProcessCache.timestamp[pid]) < globalProcessCache.ttl {
			globalProcessCache.RUnlock()
			// Return a copy to avoid race conditions
			infoCopy := *cached
			return &infoCopy, nil
		}
	}
	globalProcessCache.RUnlock()

	procDir := fmt.Sprintf("/proc/%d", pid)

	// Check if process exists
	if _, err := os.Stat(procDir); os.IsNotExist(err) {
		// Remove from cache if it no longer exists
		globalProcessCache.Lock()
		delete(globalProcessCache.cache, pid)
		delete(globalProcessCache.timestamp, pid)
		globalProcessCache.Unlock()
		return nil, fmt.Errorf("process %d does not exist", pid)
	}

	info := &ProcessInfo{
		PID:     pid,
		Environ: make(map[string]string),
	}

	// Read PPID from /proc/[pid]/stat
	statData, err := os.ReadFile(filepath.Join(procDir, "stat"))
	if err != nil {
		return nil, fmt.Errorf("failed to read stat: %w", err)
	}

	// Parse stat file - format: pid (name) state ppid ...
	// We need to handle names with spaces/parentheses
	statStr := string(statData)
	lastParen := strings.LastIndex(statStr, ")")
	if lastParen == -1 {
		return nil, fmt.Errorf("invalid stat format")
	}

	// Extract name from (name)
	firstParen := strings.Index(statStr, "(")
	if firstParen != -1 && lastParen > firstParen {
		info.Name = statStr[firstParen+1 : lastParen]
	}

	// Parse fields after name
	fields := strings.Fields(statStr[lastParen+1:])
	if len(fields) >= 2 {
		info.PPID, _ = strconv.Atoi(fields[1]) // Third field is PPID
	}

	// Extract CPU time (utime + stime)
	// Fields 14 and 15 are utime and stime (in clock ticks)
	// After the name, they are at indices 11 and 12
	if len(fields) >= 13 {
		utime, _ := strconv.ParseInt(fields[11], 10, 64)
		stime, _ := strconv.ParseInt(fields[12], 10, 64)
		// Convert from clock ticks to seconds (typically 100 ticks/second on Linux)
		info.CPUTime = float64(utime+stime) / 100.0
	}

	// Read command line
	cmdlineData, err := os.ReadFile(filepath.Join(procDir, "cmdline"))
	if err == nil {
		// cmdline is null-separated, convert to space-separated
		cmdline := strings.ReplaceAll(string(cmdlineData), "\x00", " ")
		info.Cmdline = strings.TrimSpace(cmdline)
	}

	// Read executable path (skip for kernel threads to save I/O)
	if !isKernelThread(pid, info.Cmdline) {
		exePath, err := os.Readlink(filepath.Join(procDir, "exe"))
		if err == nil {
			info.Exe = exePath
		}

		// Read working directory
		cwdPath, err := os.Readlink(filepath.Join(procDir, "cwd"))
		if err == nil {
			info.Cwd = cwdPath
		}

		// Read environment variables (skip for kernel threads)
		environData, err := os.ReadFile(filepath.Join(procDir, "environ"))
		if err == nil {
			environStr := string(environData)
			for _, pair := range strings.Split(environStr, "\x00") {
				if pair == "" {
					continue
				}
				parts := strings.SplitN(pair, "=", 2)
				if len(parts) == 2 {
					info.Environ[parts[0]] = parts[1]
				}
			}
		}

		// Read ports this process is listening on (lazy - only if not cached)
		ports, err := GetPortsForProcess(pid)
		if err == nil {
			info.Ports = ports
		}
	}

	// Update cache
	globalProcessCache.Lock()
	globalProcessCache.cache[pid] = info
	globalProcessCache.timestamp[pid] = time.Now()
	globalProcessCache.Unlock()

	return info, nil
}

// GetParentChain traverses the parent process chain up to init (PID 1)
func GetParentChain(pid int) ([]ProcessInfo, error) {
	var chain []ProcessInfo
	currentPID := pid
	seen := make(map[int]bool) // Prevent infinite loops

	for currentPID > 0 && !seen[currentPID] {
		seen[currentPID] = true

		info, err := ReadProcessInfo(currentPID)
		if err != nil {
			break // Process no longer exists
		}

		chain = append(chain, *info)

		// Stop if we've reached init (PID 1) or if parent is 0
		if currentPID == 1 || info.PPID == 0 {
			break
		}

		currentPID = info.PPID

		// Safety: limit chain length
		if len(chain) > 100 {
			break
		}
	}

	return chain, nil
}

// FindLaunchScript finds the "launch script" in the parent chain
// This is typically the first child of a shell (e.g., "bun dev" launched from bash)
func FindLaunchScript(chain []ProcessInfo) *ProcessInfo {
	// Strategy: Find the first process whose parent is a shell
	for i := 0; i < len(chain); i++ {
		if i+1 < len(chain) {
			parent := chain[i+1]
			if IsShell(parent.Name) || IsShell(filepath.Base(parent.Exe)) {
				return &chain[i]
			}
		}
	}

	// Fallback: Return the last process in chain (closest to user action)
	// before we hit systemd/init
	for i := len(chain) - 1; i >= 0; i-- {
		if chain[i].PID != 1 && chain[i].Name != "systemd" {
			return &chain[i]
		}
	}

	return nil
}

// IsShell checks if a process name is a known shell
func IsShell(name string) bool {
	return ShellNames[name]
}

// GetPortsForProcess finds all TCP ports that a specific process is listening on (optimized)
func GetPortsForProcess(pid int) ([]int, error) {
	// Use the cached port-to-PID mapping
	portMap, err := buildPortToProcessMap()
	if err != nil {
		return nil, err
	}

	// Find all ports where this PID appears
	result := make([]int, 0)
	for port, pids := range portMap {
		for _, p := range pids {
			if p == pid {
				result = append(result, port)
				break
			}
		}
	}

	return result, nil
}

// GetProcessesListeningOnPort finds all processes listening on a specific TCP port (optimized)
func GetProcessesListeningOnPort(port int) ([]int, error) {
	// Use the cached port-to-PID mapping
	portMap, err := buildPortToProcessMap()
	if err != nil {
		return nil, err
	}

	// Return PIDs for this port
	if pids, exists := portMap[port]; exists {
		return pids, nil
	}

	return []int{}, nil
}

// DiscoverProcess discovers a process and its launch context
// This enriches process info with parent chain and identifies the launch script
func DiscoverProcess(pid int) (*ProcessInfo, error) {
	chain, err := GetParentChain(pid)
	if err != nil {
		return nil, err
	}

	if len(chain) == 0 {
		return nil, fmt.Errorf("could not read process info for PID %d", pid)
	}

	// The first element in chain is the target process itself
	info := chain[0]

	return &info, nil
}

// DiscoverProcessOnPort discovers the process listening on a port
func DiscoverProcessOnPort(port int) (*ProcessInfo, error) {
	pids, err := GetProcessesListeningOnPort(port)
	if err != nil {
		return nil, err
	}

	if len(pids) == 0 {
		return nil, fmt.Errorf("no process listening on port %d", port)
	}

	// Use the first PID found
	pid := pids[0]

	// Get full process info
	procInfo, err := DiscoverProcess(pid)
	if err != nil {
		return nil, err
	}

	return procInfo, nil
}
