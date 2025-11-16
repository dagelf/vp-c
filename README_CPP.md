# VP - Process Orchestration (C++ Port)

This is a C++ port of the VP (Vibe Process) manager, originally written in Go.

## Overview

VP is a zero-assumption process manager with pure primitives for resource allocation and process control. It follows a firmware-style design with no hardcoded resource types, validating everything via shell commands.

## Building

### Requirements
- C++17 compiler (g++ or clang++)
- Linux (uses /proc filesystem and Linux-specific APIs)
- Standard build tools (make)

### Build Instructions

```bash
make
```

Or using CMake:

```bash
mkdir build
cd build
cmake ..
make
```

### Installation

```bash
sudo make install
```

This installs the `vp` binary to `/usr/local/bin/`.

## Usage

The C++ version maintains API compatibility with the Go version:

```bash
# List all process instances
vp
vp ps

# Start a process from a template
vp start <template> <name> [--key=value...]

# Stop a running process
vp stop <name>

# Restart a stopped process
vp restart <name>

# Delete a process instance
vp delete <name>

# Start web UI
vp serve [port]
```

## Architecture

The C++ implementation follows the same architecture as the Go version:

- `main.cpp` - CLI entrypoint, command routing
- `state.cpp` - JSON persistence and state management
- `process.cpp` - Lifecycle: start/stop/restart/discover/monitor
- `resource.cpp` - Generic allocation: type:value pairs + check commands
- `procutil.cpp` - /proc parsing, port discovery, parent chains
- `api.cpp` - HTTP API + embedded web UI
- `types.hpp` - Core data structures

## Implementation Notes

### Current Status

This is a functional C++ port with the following characteristics:

- **Core functionality**: Process lifecycle management, resource allocation, /proc parsing
- **Simplified JSON**: Basic JSON serialization (production would use nlohmann/json)
- **Minimal HTTP**: Simple HTTP server (production would use cpp-httplib or similar)
- **File watching**: inotify support (partial implementation)

### Differences from Go Version

1. **JSON handling**: Simplified implementation - for production use, integrate nlohmann/json
2. **HTTP server**: Basic implementation - for production use, integrate cpp-httplib or Boost.Beast
3. **State serialization**: Minimal implementation - full serialization pending JSON library
4. **Web UI**: Embedded placeholder - full web.html integration pending

### Production Improvements

For production use, consider:

1. **JSON Library**: Add nlohmann/json (header-only) for proper JSON handling
2. **HTTP Library**: Add cpp-httplib or Boost.Beast for robust HTTP server
3. **Error Handling**: Enhanced error reporting and recovery
4. **Testing**: Port process_test.go to C++ unit tests
5. **Logging**: Add structured logging
6. **Documentation**: API documentation with Doxygen

## Design Constraints (Maintained)

- Minimal LoC (C++ ~2500 lines vs Go ~2400 lines)
- Single binary, minimal dependencies
- All state in one JSON file
- Zero resource type assumptions
- Shell commands for validation

## License

Same as original VP project.

## Contributing

This C++ port aims to maintain feature parity with the Go version while following C++ best practices.
