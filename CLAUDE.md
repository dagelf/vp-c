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
4. **Brutally Simple** - 6 files, ~500 lines
5. **Firmware-Style** - Pure primitives, users configure behavior
6. **Debuggable** - Human-readable JSON state
7. **Extensible Without Code Changes** - Add types via CLI
8. **Security Is Up To You (Today)** - Shells and HTTP are wide open unless you add controls

## Design Constraints

**Maintain:**
- Minimal LoC (currently ~2400 lines)
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

C++ implementation (~2400 lines):

```
src/main.cpp      CLI entrypoint, command routing
src/state.cpp     JSON persistence (nlohmann/json)
src/process.cpp   Lifecycle: start/stop/restart/discover/monitor
src/resource.cpp  Generic allocation: type:value pairs + check commands
src/api.cpp       HTTP API + embedded web UI
src/procutil.cpp  /proc parsing, port discovery, parent chains
web.html          Single-page UI
```

## Key Concepts

- **Template**: Process blueprint (command + resource requirements + default vars)
- **Instance**: Running process from template (name + PID + status + allocated resources)
- **ResourceType**: User-defined with shell check command (counter flag for auto-increment)
- **Resource**: Allocated type:value pair (tcpport:3000, gpu:0, license:@server, etc)

## Resource System

```bash
# Built-in types (defaults)
tcpport     -> nc -z localhost ${value}  # counter: 3000-9999
vncport     -> nc -z localhost ${value}  # counter: 5900-5999
dbfile      -> test -f ${value}
workdir     -> (no check, informational)

# Add custom types at runtime
vp resource-type add gpu --check='nvidia-smi -L | grep GPU-${value}'
vp resource-type add license --check='lmutil lmstat -c ${value} | grep UP'
```

Shell command exits 0 = in-use (unavailable), exits non-zero = free (available).

## Process Discovery

Automatic matching: On every refresh, scan /proc to:
1. Update CPU time for running instances
2. Match stopped instances to running processes (by name + port)
3. Discover unmanaged processes (ports only by default)

Monitor mode: Import existing process as read-only instance (managed=false).

## State File

`~/.vibeprocess/state.json` contains everything (README previously referenced `~/.config/vp/state.json`; path should be unified):
- instances: name -> Instance
- templates: id -> Template
- resources: type:value -> Resource
- counters: type -> current_value
- types: name -> ResourceType

Hot-reload via inotify when file changes externally.

## C++ Conversion Status

**Completed Features (100% functional parity with Go):**
- ✅ CLI commands (start/stop/restart/delete/ps)
- ✅ Template management (list/add/show via CLI and API)
- ✅ Resource-type management (list/add via CLI and API)
- ✅ Template system with ${var} + %counter interpolation
- ✅ Generic resource allocation + validation
- ✅ JSON persistence (nlohmann/json)
- ✅ Process lifecycle (fork/exec/signal handling)
- ✅ Process discovery (full /proc scanning)
- ✅ Process matching (stopped instances to running PIDs - Step 1 only, see notes)
- ✅ Monitor mode for existing processes
- ✅ /proc parsing and port discovery
- ✅ Parent chain traversal
- ✅ All 15 tests passing
- ✅ HTTP API fully functional
- ✅ Web UI working (serves web.html from file)

**Completed API Endpoints:**
- ✅ GET /api/instances - List instances with PID checking
- ✅ GET /api/templates - List templates
- ✅ GET /api/resources - List allocated resources
- ✅ GET /api/resource-types - List resource types
- ✅ GET /api/config - Get configuration
- ✅ GET /api/discover - Discover processes (ports as arrays)
- ✅ POST /api/instances - Start/stop/restart/delete operations
- ✅ POST /api/monitor - Monitor existing process
- ✅ POST /api/execute-action - Execute instance actions
- ✅ POST /api/templates - Add template dynamically
- ✅ POST /api/resource-types - Add resource type dynamically

**Implementation Notes:**
- Process matching: `/api/instances` only does Step 1 (PID checking). Step 2 (matching stopped instances to new processes) should be triggered explicitly via discovery workflow, not on every instance list refresh
- Web UI: Serves from web.html file (not embedded) for easier development

**Known Issues:**
- HTTP server is unauthenticated, CORS is `*`, manual parser has no request/size limits
- Shell execution everywhere (`/bin/sh -c`, `system`) for commands, actions, and resource checks; no sandboxing
- State path mismatch between code (`~/.vibeprocess`) and earlier docs (`~/.config/vp`)
- Config hot-reload (inotify) - setup exists but watcher thread not implemented
- Race conditions: state maps/counters mutated across threads without consistent locking
- Process group kill assumes pgid==pid and can overreach if PIDs are reused
- Discovery/matching is heuristic (basename + optional ports/CWD) and can mis-associate processes
- Tests touch real state dir and rely on host /proc; no HTTP/web integration tests yet
- Minor: Parent chain basename extraction edge case

**Benefits vs Go Version:**
- Zero external dependencies (Go had 2: fsnotify, sys)
- Single binary, stdlib only
- Similar LoC (~2400 lines)
- Slightly faster startup

**Testing:**
- 17/17 unit-style tests passing (CLI/process/resource/state/template add/delete; no API/web coverage)
- Manual testing: template/resource-type management works
- State persistence verified working
- Process lifecycle verified working

**Recent changes:**
- `vp template add -` accepts JSON from stdin; templates can be deleted via API/state helper
- `vp key` prints or sets API key (reads stdin if piped; otherwise generates)
- CMake now builds `vp_test`; `make test` runs the suite; `build.sh` runs tests automatically
- API requests now require `X-VP-Key` when `api_key` is set (enforced server-side)

## Security Considerations (current posture)
- No authZ/authN; any local client can start/stop processes or run actions
- HTTP server has open CORS and no request limits; DoS is trivial
- Commands/actions/resource-checks are executed via shell with full user privileges
- No isolation: processes inherit environment/stdio; no seccomp/cgroups/uid drop
- Mitigation today: run behind a trusted reverse proxy, restrict bind address, or sandbox the binary

## Roadmap

### Immediate (To complete C++ conversion)
- [ ] Add mutex protection for shared state (instances/types/resources/counters)
- [ ] Optional auth (token/header) and bind-address restriction
- [ ] add make option to build statically

### Short-term
- [ ] add make option to build in a specified container with musl / older libc (debian 9/10) 
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
