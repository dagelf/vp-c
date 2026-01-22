# Visual Process Manager - Technical Specification

## 1. Overview

**Visual Process Manager (VP)** is an ultra-lean, flexible process orchestration system that makes zero assumptions about resource types or management policies. It provides a minimalist framework for defining, starting, stopping, and monitoring processes with custom resource requirements.

### Core Philosophy

- **Minimal LoC**: Brutally simple codebase with minimal dependencies
- **Zero Assumptions**: No built-in opinions about what constitutes a "resource"
- **Shell-based Validation**: Resources are validated using arbitrary shell commands
- **Pure Mechanism**: Provides primitives, not policies
- **Web + CLI**: Dual interface for maximum flexibility

## 2. Architecture

### 2.1. Component Diagram

```
┌───────────────────────────────────────────────────────┐
│                 Visual Process Manager                │
├───────────────────────────────────────────────────────┤
│                                                       │
│  ┌─────────────┐    ┌─────────────┐    ┌───────────┐  │
│  │   CLI       │    │   Web UI    │    │   State   │  │
│  └─────────────┘    └─────────────┘    └───────────┘  │
│       │                  │                  │         │
│       ▼                  ▼                  ▼         │
│  ┌─────────────┐    ┌─────────────┐    ┌───────────┐  │
│  │  API Layer  │◄──►│ HTTP Server │    │ JSON DB   │  │
│  └─────────────┘    └─────────────┘    └───────────┘  │
│       │                  │                            │
│       ▼                  ▼                            │
│  ┌─────────────┐    ┌─────────────┐                   │
│  │ Process Mgr │    │ Resource Mgr│                   │
│  └─────────────┘    └─────────────┘                   │
│       │                  │                            │
│       ▼                  ▼                            │
│  ┌─────────────┐    ┌─────────────┐                   │
│  │  System     │    │ Shell Cmds  │                   │
│  │  Processes  │    │ (Validation)│                   │
│  └─────────────┘    └─────────────┘                   │
│                                                       │
└───────────────────────────────────────────────────────┘
```

### 2.2. Core Components

#### 2.2.1. State Management

- **Persistence**: `~/.config/vp/state.json`
- **Data Model**: JSON-based state containing:
  - `instances`: Running/stopped process instances
  - `templates`: Process templates with resource requirements
  - `resources`: Allocated resources and their owners
  - `types`: Resource type definitions with validation commands
  - `counters`: Auto-incrementing counters for resources

#### 2.2.2. Process Instance Management

- **Lifecycle**: `start` → `running` → `stopping` → `stopped`
- **Discovery**: Auto-detection of running processes via `/proc`
- **Monitoring**: CPU time tracking, status updates
- **Control**: Start, stop, restart, delete operations

#### 2.2.3. Resource Management

- **Types**: User-defined resource types with shell validation
- **Allocation**: Automatic or manual resource assignment
- **Validation**: Shell command execution to verify resource availability
- **Counters**: Auto-incrementing resources (e.g., TCP ports)

#### 2.2.4. API Layer

- **HTTP Server**: Custom C++ HTTP server on port 8080
- **REST Endpoints**: `/api/*` with CORS support
- **Authentication**: Optional API key via `X-VP-Key` header
- **Web UI**: Embedded HTML/JS interface with hot-reload support

## 3. Data Structures

### 3.1. Resource

```cpp
struct Resource {
    std::string type;    // Resource type name
    std::string value;   // Resource value (port, path, etc.)
    std::string owner;   // Instance name that owns this resource
}
```

### 3.2. ResourceType

```cpp
struct ResourceType {
    std::string name;     // Type name (tcpport, vncport, gpu, etc.)
    std::string check;    // Shell command to validate availability
    bool counter;         // Is this auto-incrementing?
    int start;            // Counter start value
    int end;              // Counter end value
}
```

### 3.3. Template

```cpp
struct Template {
    std::string id;             // Unique template ID
    std::string label;          // Human-readable label
    std::string command;        // Command template with ${var} placeholders
    std::vector<std::string> resources; // Required resource types
    std::map<std::string, std::string> vars; // Default variables
    std::string action;         // Optional action URL/command
    std::map<std::string, std::string> launchers; // Launcher commands
}
```

### 3.4. Instance

```cpp
struct Instance {
    std::string id;             // Unique instance ID
    std::string name;           // User-provided name
    std::string template_name;  // Template ID
    std::string command;        // Final interpolated command
    int pid;                    // Process ID
    std::string status;         // stopped|starting|running|stopping|error
    std::map<std::string, std::string> resources; // Allocated resources
    time_t started;             // Unix timestamp
    std::string cwd;            // Working directory
    bool managed;               // Can be stopped/restarted
    double cpu_time;            // CPU time in seconds
    std::string error;          // Error message
    std::string action;         // Action URL/command
    std::map<std::string, std::string> launchers; // Launcher commands
}
```

## 4. Core Algorithms

### 4.1. Resource Allocation

```
1. For each required resource type in template:
   a. If explicit value provided: validate with check command
   b. If no value provided:
      i. If counter resource: find next available value
      ii. If non-counter: search for available value
   c. Mark resource as allocated to instance
   d. Add to instance.resources map
```

### 4.2. Process Discovery

```
1. Scan /proc directory for running processes
2. For each process:
   a. Read cmdline, status, environ
   b. Match against known instances by PID
   c. Update status, CPU time, resources
   d. Detect new processes that match templates
```

### 4.3. Command Interpolation

```
1. Start with template command string
2. Replace ${var} placeholders with:
   a. User-provided values (highest priority)
   b. Template default values
   c. Allocated resource values
3. Execute final command in shell
```

## 5. CLI Interface

### 5.1. Command Structure

```bash
vp <command> [args...]
```

### 5.2. Commands

| Command | Description | Example |
|---------|-------------|---------|
| `new` | Create and start new process | `vp new postgres mydb --tcpport=5432` |
| `start` | Start stopped process | `vp start mydb` |
| `stop` | Stop running process | `vp stop mydb` |
| `restart` | Restart process | `vp restart mydb` |
| `delete` | Delete process instance | `vp delete mydb` |
| `ps` | List all instances | `vp ps` |
| `serve` | Start web UI | `vp serve 8080` |
| `template` | Manage templates | `vp template list` |
| `resource-type` | Manage resource types | `vp resource-type add gpu --check='nvidia-smi'` |
| `key` | Manage API key | `vp key` |

## 6. Web Interface

### 6.1. Features

- **Dashboard**: View all instances with status, resources, actions
- **Instance Control**: Start/stop buttons for each instance
- **Template Management**: Add/edit templates via form
- **Resource Type Management**: Add/edit resource types via form
- **Resource Allocation**: View and manage allocated resources
- **Auto-refresh**: Real-time updates
- **Responsive Design**: Mobile-friendly interface
- **Dark Mode**: Theme support

### 6.2. API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Serve web UI |
| `/api/instances` | GET | List all instances |
| `/api/instances` | POST | Create new instance |
| `/api/instances/<id>` | DELETE | Delete instance |
| `/api/instances/<id>/start` | POST | Start instance |
| `/api/instances/<id>/stop` | POST | Stop instance |
| `/api/templates` | GET | List all templates |
| `/api/templates` | POST | Add new template |
| `/api/resource-types` | GET | List all resource types |
| `/api/resource-types` | POST | Add new resource type |

## 7. Resource System

### 7.1. Built-in Resource Types

| Type | Check Command | Counter |
|------|---------------|---------|
| `tcpport` | `nc -z localhost ${value}` | Yes |
| `vncport` | `nc -z localhost ${value}` | Yes |
| `dbfile` | `test -f ${value}` | No |
| `socket` | `test -S ${value}` | No |
| `workdir` | Special: working directory | No |

### 7.2. Custom Resource Types

```bash
# Add custom GPU resource
vp resource-type add gpu \
  --check='nvidia-smi -L | grep GPU-${value}' \
  --counter=false

# Add license server resource
vp resource-type add flexlm \
  --check='lmutil lmstat -c ${value} | grep "UP"'
```

## 8. Template System

### 8.1. Template Structure

```json
{
  "id": "postgres",
  "label": "PostgreSQL Database",
  "command": "postgres -D ${datadir} -p ${tcpport}",
  "resources": ["tcpport", "datadir"],
  "vars": {
    "datadir": "/tmp/pgdata"
  },
  "action": "http://localhost:${tcpport}",
  "launchers": {
    "web": "xdg-open http://localhost:${tcpport}",
    "cli": "psql -p ${tcpport}"
  }
}
```

### 8.2. Variable Substitution

- `${var}`: Variable substitution
- `%counter`: Counter substitution (deprecated)
- Priority: CLI args > Template defaults > Auto-allocation

## 9. Process Lifecycle

### 9.1. State Transitions

```
┌─────────┐
│  New    │
└────┬────┘
     │
     ▼
┌─────────┐
│ Starting│
└────┬────┘
     │
     ▼
┌─────────┐
│ Running │
└────┬────┘
     │
     ▼
┌─────────┐
│ Stopping│
└────┬────┘
     │
     ▼
┌─────────┐
│ Stopped │
└─────────┘
```

### 9.2. Error Handling

- **Validation Errors**: Invalid resource values
- **Start Errors**: Process fails to launch
- **Monitoring Errors**: Process disappears unexpectedly
- **Resource Conflicts**: Duplicate allocations

## 10. Security

### 10.1. Authentication

- **API Key**: Optional `X-VP-Key` header for API endpoints
- **Generation**: Random 64-character hex string
- **Storage**: Encrypted in state file

### 10.2. Authorization

- **No RBAC**: Single-user system
- **File Permissions**: State file protected by filesystem permissions
- **Process Isolation**: Runs with user permissions

## 11. Performance

### 11.1. Constraints

- **Memory**: Minimal footprint (< 10MB typical)
- **CPU**: Low overhead, mostly I/O bound
- **Scalability**: Designed for 10-100 processes

### 11.2. Optimizations

- **Lazy Discovery**: Process scanning on demand
- **Caching**: Resource availability caching
- **Bulk Operations**: Batch processing for multiple instances

## 12. Dependencies

### 12.1. Runtime

- **C++17 Standard Library**
- **POSIX System Calls** (`fork`, `exec`, `/proc`)
- **Common Shell Utilities** (`nc`, `test`, etc.)

### 12.2. Build

- **CMake 3.10+**
- **GCC/Clang with C++17 support**
- **pthread library**

## 13. Error Handling

### 13.1. Error Codes

- **CLI**: Non-zero exit codes with stderr messages
- **API**: HTTP status codes (400, 401, 404, 500)
- **State**: Error messages stored in instance.error field

### 13.2. Recovery

- **State Corruption**: Backup and restore from previous state
- **Process Crashes**: Auto-detection and status updates
- **Resource Leaks**: Garbage collection on startup

## 14. Future Extensions

### 14.1. Planned Features

- **Multi-user Support**: Basic authentication
- **Remote Management**: SSH-based process control
- **Plugin System**: Extensible resource validators
- **Metrics Export**: Prometheus/StatsD integration
- **Logging**: Structured log output

### 14.2. Architecture Considerations

- **Modular Design**: Easy to extend components
- **Backward Compatibility**: State file versioning
- **Cross-platform**: Windows support via WSL/Cygwin

## 15. Appendix

### 15.1. File Locations

- **State**: `~/.config/vp/state.json`
- **Config**: `~/.config/vp/config.json` (future)
- **Logs**: `~/.config/vp/vp.log` (future)
- **Cache**: `~/.cache/vp/` (future)

### 15.2. Environment Variables

- `VP_STATE_DIR`: Override state directory
- `VP_API_KEY`: Override API key
- `VP_DEBUG`: Enable debug logging

### 15.3. Exit Codes

- `0`: Success
- `1`: General error
- `2`: Invalid arguments
- `3`: Resource conflict
- `4`: Process error
