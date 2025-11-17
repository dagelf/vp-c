# Bugs and Issues Found in C++ Conversion

## Critical Issues

### 1. **JSON Serialization Not Implemented**
- **Location**: `state.cpp`, `toJson()` and `fromJson()` methods
- **Impact**: HIGH - State persistence doesn't work properly
- **Description**: The JSON serialization is stubbed out with minimal implementation. State saving/loading doesn't actually persist instances, templates, or resources.
- **Fix Required**: Integrate nlohmann/json or implement proper JSON serialization

```cpp
// Current implementation (BROKEN):
std::string State::toJson() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"instances\": {},\n";  // <-- Always empty!
    oss << "  \"templates\": {},\n";  // <-- Always empty!
    // ...
}
```

### 2. **File Watching Not Implemented**
- **Location**: `state.cpp`, `watchConfig()` method
- **Impact**: MEDIUM - Config hot-reload doesn't work
- **Description**: inotify setup exists but the watcher thread is not implemented
- **Fix Required**: Implement the file watching thread

```cpp
// Current implementation (INCOMPLETE):
bool State::watchConfig() {
    // ... setup code ...
    // TODO: Implement file watching thread
    // For now, just return success
    return true;
}
```

## Major Issues

### 3. **HTTP API Returns Empty Responses**
- **Location**: `api.cpp`, all API handlers
- **Impact**: HIGH - Web UI won't work
- **Description**: All API endpoints return `{}` instead of actual data
- **Fix Required**: Implement proper JSON serialization for API responses

```cpp
// Current (BROKEN):
response << "{}"; // Simplified - would serialize instances here
```

### 4. **Process Discovery Not Implemented**
- **Location**: `process.cpp`, `discoverProcesses()` function
- **Impact**: MEDIUM - Discovery feature incomplete
- **Description**: Function is stubbed and returns empty vector
- **Fix Required**: Port full discovery logic from Go version

### 5. **Parent Chain Basename Extraction Bug**
- **Location**: `procutil.cpp:369`
- **Impact**: LOW - Edge case in shell detection
- **Description**: May crash on empty exe path
- **Fix Required**: Add null check

```cpp
if (isShell(parent.Name) || isShell(filepath.Base(parent.Exe))) {
    // ^^ parent.Exe could be empty, causing issues
}
```

## Minor Issues

### 6. **Memory Safety - Bare Pointers in Fork**
- **Location**: `process.cpp`, process starting code
- **Impact**: LOW - Potential issue if resources freed before child execs
- **Description**: Using `it->second.c_str()` in child process could be unsafe
- **Fix Required**: Copy strings to stack before fork

### 7. **Race Condition in Reaper Threads**
- **Location**: `process.cpp:151-158, 280-287`
- **Impact**: LOW - Theoretical race accessing state
- **Description**: Detached threads access shared state without proper locking
- **Fix Required**: Use state mutex or redesign reaper pattern

### 8. **Error Handling - system() Calls**
- **Location**: `resource.cpp:128`, `process.cpp:326`
- **Impact**: LOW - Poor error messages
- **Description**: Using `system()` doesn't provide detailed error info
- **Fix Required**: Use fork/exec pattern for better error reporting

### 9. **Port Scanning Performance**
- **Location**: `procutil.cpp`, port-to-process mapping
- **Impact**: LOW - Can be slow on systems with many FDs
- **Description**: Scans all FDs for all PIDs
- **Fix Required**: Already has caching, but could optimize further

### 10. **String Size Calculation in HTTP Response**
- **Location**: `api.cpp:45`
- **Impact**: LOW - Incorrect Content-Length for embedded strings
- **Description**: Uses `strlen()` on raw string literal which may not account for actual length
- **Fix Required**: Use compile-time size or string.length()

```cpp
response << "Content-Length: " << strlen(WEB_HTML) << "\r\n";
// Should be: std::string(WEB_HTML).length()
```

## Feature Gaps (Not Bugs, But Missing Functionality)

### 11. **Template Command Parsing**
- **Impact**: MEDIUM
- **Description**: CLI commands for template management (add, show) not implemented in main.cpp

### 12. **Resource Type Management**
- **Impact**: MEDIUM
- **Description**: CLI commands for resource type management not implemented

### 13. **Discover/Inspect Commands**
- **Impact**: MEDIUM
- **Description**: discover, discover-port, inspect commands not implemented in main.cpp

### 14. **Environment Variable Preservation**
- **Impact**: LOW
- **Description**: Child processes don't inherit specific environment variables from templates

### 15. **Working Directory Capture**
- **Impact**: LOW
- **Description**: `inst->cwd` capture in parent process may not match child's actual cwd

## Test Coverage Gaps

The following scenarios from Go tests are not covered:

1. **Port matching with multiple instances** - Only one instance should match a PID
2. **Resource conflict detection** - What happens when a port is already in use
3. **Template variable interpolation edge cases** - Missing vars, nested vars
4. **Process group killing** - Verify entire process tree is killed
5. **Restart with changed resources** - Resource availability checking

## Recommendations

### Immediate Priorities (Fix Before Production):
1. Implement JSON serialization (use nlohmann/json header-only library)
2. Implement HTTP API responses
3. Fix process discovery
4. Implement config file watching

### Medium Term:
5. Add missing CLI commands
6. Improve error handling and reporting
7. Add mutex protection to shared state access
8. Fix minor safety issues

### Long Term:
9. Performance optimization for large-scale deployments
10. Comprehensive test coverage
11. Documentation for edge cases
12. Security audit (input validation, resource limits)

## Testing Status

✅ **Passing Tests** (15/15):
- Basic state operations
- Process lifecycle (start/stop)
- Resource allocation
- Process info reading
- Parent chain traversal
- Port mapping
- Process monitoring

❌ **Missing Test Coverage**:
- State persistence (load/save with real data)
- HTTP API endpoints
- Template management
- Resource type management
- Multi-process scenarios
- Error conditions
- Concurrency/race conditions
