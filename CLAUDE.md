# vp - Process Orchestration

## Core Concept

Zero-assumption process manager. Pure primitives for resource allocation + process control.

**Philosophy:** Firmware-style design. No hardcoded resource types. Everything validated via shell commands. Designed for Mars

**Assume nothing, enable everything.**

Want GPU allocation? Add a resource type.
Want database connections? Add a resource type.
Want anything? Just define a check command.
Interact with anything? Just add an action. 

## Design Principles

1. **Zero Hardcoded Assumptions** - Resources aren't hardcoded
2. **Maximum Flexibility** - Add ANY resource type at runtime
3. **Validation via Shell** - Use any installed tool
5. **Brutally Simple** - 6 files, ~500 lines
6. **Firmware-Style** - Pure primitives, users configure behavior
7. **Debuggable** - Human-readable JSON state
8. **Extensible Without Code Changes** - Add types via CLI

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

```
main.go       CLI entrypoint, command routing
state.go      JSON persistence, inotify reload
process.go    Lifecycle: start/stop/restart/discover/monitor
resource.go   Generic allocation: type:value pairs + check commands
api.go        HTTP API + embedded web UI
procutil.go   /proc parsing, port discovery, parent chains
web.html      Single-page UI
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

`~/.vibeprocess/state.json` contains everything:
- instances: name -> Instance
- templates: id -> Template
- resources: type:value -> Resource
- counters: type -> current_value
- types: name -> ResourceType

Hot-reload via inotify when file changes externally.

## Implementation Status

**Complete:**
- ✓ CLI commands (start/stop/restart/delete/ps/inspect)
- ✓ Template system with ${var} + %counter interpolation
- ✓ Generic resource allocation + validation
- ✓ Process discovery + matching
- ✓ Web UI with auto-refresh
- ✓ Config hot-reload (inotify)
- ✓ CPU time tracking
- ✓ Monitor mode for existing processes
- ✓ Action execution (URLs/commands)

**Roadmap:**

### Short-term
- [ ] Code review doc
- [ ] Better error messages (resource conflicts, validation failures)

### Medium-term
- [ ] Resource tags/grouping (dev/prod/test)
- [ ] Bulk operations (stop-all, restart-all by tag, default tags)
- [ ] Health checks (periodic validation + auto-restart)

### Long-term? - to be decided. Would ruin the simplicity and there are other tools for this already
- [ ] Log capture (stdout/stderr to files)
- [ ] Resource limits (CPU/mem via cgroups)
- [ ] Dependency chains (start B after A running)
- [ ] Template inheritance (extend base templates)
- [ ] Template library/marketplace (shareable templates)
- [ ] Multi-host coordination (cluster mode)
- [ ] Time-based scheduling (cron-style)
- [ ] Rollback on failure (restore previous state)
- [ ] Audit log (who started/stopped what when)
- [ ] API authentication/authorization
- [ ] Plugin system for custom resource validators
