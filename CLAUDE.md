# vp - Process Orchestration

## Core Concept

Zero-assumption process manager. Pure primitives for resource allocation + process control.

**Philosophy:** Firmware-style design. No hardcoded resource types. Everything validated via shell commands. Designed for Mars.

**Assume nothing, enable everything.**

Want GPU allocation? Add a resource type.
Want database connections? Add a resource type.
Want anything? Just define a check command.
Interact with anything? Just add an action. 

## Design Principles

1. **Zero Hardcoded Assumptions** - Resources aren't hardcoded
2. **Maximum Flexibility** - Add ANY resource type at runtime
3. **Validation via Shell** - Use any installed tool
4. **Brutally Simple** - minimal LoC
5. **Firmware-Style** - Pure primitives, users configure behavior
6. **Debuggable** - Human-readable JSON state
7. **Extensible Without Code Changes** - Add types via CLI
8. **Security Is Up To You (Today)** - Shells and HTTP are wide open unless you add controls

## Design Constraints

**Maintain:**
- Single binary, no dependencies beyond stdlib
- All state in one JSON file
- Zero resource type assumptions
- Shell commands for validation

**Avoid:**
- Framework dependencies
- Complex abstractions
- Special-casing resource types
- Breaking single-JSON-file invariant

## Architecture

C++ implementation:

```
src/api.cpp       HTTP API + embedded web UI
src/process.cpp   Lifecycle: start/stop/restart/discover/monitor
src/main.cpp      CLI entrypoint, command routing
src/procutil.cpp  /proc parsing, port discovery, parent chains
src/state.cpp     JSON persistence (nlohmann/json)
src/resource.cpp  Generic allocation: type:value pairs + check commands
web.html          Single-page UI
```

## Line counts

$ printf '%s\0' src/*.cpp | grep -Ezv '(json|test)' | xargs -0 wc -l | sort -nr
  3143 total
   874 src/api.cpp
   799 src/process.cpp
   491 src/main.cpp
   419 src/procutil.cpp
   411 src/state.cpp
   149 src/resource.cpp

## Key Concepts

- **Template**: Process blueprint (command + resource requirements + default vars)
- **Instance**: Process running from template (name + PID + status + allocated resources)
- **ResourceType**: User-defined with shell check command (counter flag for auto-increment)
- **Resource**: ResourceType in use as type:value pair (eg. tcpport:3000, gpu:0, license:@server, etc)
- **Actions**: shell commands or Url strings to interact with the process, aka Launcher

## Resource System

```bash
# Built-in types (defaults)
tcpport     -> nc -z localhost ${value}  # counter: 3000-9999
vncport     -> nc -z localhost ${value}  # counter: 5900-5999
dbfile      -> test -f ${value}
workdir     # no check, informational

# Add custom types at runtime
vp resource-type add gpu --check='nvidia-smi -L | grep GPU-${value}'
vp resource-type add license --check='lmutil lmstat -c ${value} | grep UP'
```

Shell command exits 0 = in-use (unavailable), exits non-zero = free (available for use).

## Process Discovery

Automatic matching: On every refresh, scan /proc to:
1. Update CPU time for running instances
2. Match stopped instances to running processes (by name + port)
3. Discover unmanaged processes (filters available, eg only processes listening on TCP ports)

Monitor mode: Import existing process as read-only instance (managed=false). # TODO check what this means

## State File

`~/.vibeprocess/state.json` contains everything 
- instances: name -> Instance
- templates: id -> Template
- resources: type:value -> Resource
- counters: type -> current_value
- types: name -> ResourceType

Hot-reload via inotify when file changes externally.

**Potential unfixed issues:** 
- TODO Check Config hot-reload (inotify) - setup exists but watcher thread not implemented
- TODO Check Race conditions: state maps/counters mutated across threads without consistent locking
- TODO Check Process group kill assumes pgid==pid and can overreach if PIDs are reused
- TODO Check Discovery/matching is heuristic (basename + optional ports/CWD) and can mis-associate processes
- TODO Check Tests touch real state dir and rely on host /proc; no HTTP/web integration tests yet
- TODO Check Minor: Parent chain basename extraction edge case
- TODO 17/17 unit-style tests passing (CLI/process/resource/state/template add/delete; no API/web coverage)
- TODO Manually tested, automate tests: template/resource-type management works
- TODO State persistence verified working, automate tests
- TODO Process lifecycle verified working, automate tests

## Accepted Security Considerations, review periodically
- Commands/actions/resource-checks are executed via shell with full user privileges
- No isolation: processes inherit environment/stdio; no seccomp/cgroups/uid drop
- Mitigation today: run behind a trusted reverse proxy, restrict bind address, or sandbox the binary

## Roadmap

### Short-term
- [ ] if launcher is a url, just provide it as an href to open it in a new window
- [ ] store the launcher templates in the instance so that conflicting resources can be reallocated
- [ ] add make option to build in a specified container with musl / older libc (debian 9/10, other distros) 
- [ ] Implement file watching thread
- [ ] Better error messages (resource conflicts, validation failures)
- [ ] Comprehensive integration tests (CLI + HTTP + web)
- [ ] Split state persistence/locking for concurrent API calls
- [ ] Harden process matching (exe inode, UID, ports) to avoid false matches
- [ ] Add request size/time limits and minimal HTTP parser guards
- [ ] Align state file path/docs and add migration helper

### Medium-term
- [ ] Remove `system`/`sh -c` in favor of execve with argv and constrained env
- [ ] Resource tags/grouping (dev/prod/test)
- [ ] Bulk operations (stop-all, restart-all by tag)
- [ ] Health checks (periodic validation + auto-restart)
- [ ] Log capture (stdout/stderr to files with size limits)

### Long-term (Maybe - would increase complexity)
- [ ] Resource limits (CPU/mem via cgroups)
- [ ] Dependency chains (start B after A running)
- [ ] Multi-host coordination (cluster mode)
