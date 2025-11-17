# Why Match by Exe Path Instead of Name?

## Current Problem: Name Matching is Weak

The current implementation matches by **process name**:

```go
expectedName := extractProcessName(inst.Command)  // "node" from "node server.js"
if procInfo.Name != expectedName {
    continue
}
```

### Example Scenario with Name Matching

**Instance 1:** `node api-server.js` (port 3000)
**Instance 2:** `node worker.js` (port 3001)

Both extract to name: `"node"`

If Instance 1 dies and Instance 2 is running:
- Expected name: `"node"` ✓
- Actual name: `"node"` ✓
- Port check: Instance 1 expects 3000, Instance 2 has 3001 ✗

**Good:** Port check prevents wrong match.

**But what if ports match?**

**Instance 1:** `node server.js` (port 3000) - stopped
**Instance 2:** `node server.js` (port 3000) - running (different server)

Both have same name AND same port → **WRONG MATCH**

## Why Exe Path is Better

### Data Available

From `/proc/[pid]/exe` symlink:
```bash
$ ls -l /proc/12345/exe
lrwxrwxrwx 1 user user 0 Nov 17 14:30 /proc/12345/exe -> /usr/bin/node

$ ls -l /proc/67890/exe
lrwxrwxrwx 1 user user 0 Nov 17 14:31 /proc/67890/exe -> /opt/app/bin/node
```

Same "name" but **different executables**!

### Exe Matching Advantages

1. **Precise Binary Identification**
   - `/usr/bin/node` ≠ `/opt/app/bin/node`
   - `/usr/bin/python3.11` ≠ `/usr/bin/python3.12`

2. **Can't Be Spoofed**
   - Process can change `argv[0]` (cmdline)
   - Process can change `/proc/[pid]/comm`
   - Process **cannot** change `/proc/[pid]/exe` symlink

3. **Works with Version Changes**
   ```
   Instance created:  node -> /usr/bin/node (v18)
   System upgraded:   node -> /usr/bin/node (v20)
   Same exe path = still matches!
   ```

4. **Works with Symlinks**
   ```
   Command: /usr/local/bin/myapp
   Exe:     /opt/app/bin/myapp  (via symlink)
   Both resolve to same canonical path
   ```

## Implementation Approach

### Step 1: Resolve Command to Exe Path

When creating an instance, resolve the command to absolute exe path:

```go
func resolveCommandToExe(command string) (string, error) {
    // Extract executable from command
    parts := strings.Fields(command)
    if len(parts) == 0 {
        return "", errors.New("empty command")
    }

    exe := parts[0]

    // If absolute path, check if exists
    if filepath.IsAbs(exe) {
        exePath, err := filepath.Abs(exe)
        if err == nil {
            return exePath, nil
        }
    }

    // If relative path with /, resolve relative to cwd
    if strings.Contains(exe, "/") {
        exePath, err := filepath.Abs(exe)
        if err == nil {
            return exePath, nil
        }
    }

    // Otherwise look up in PATH
    exePath, err := exec.LookPath(exe)
    if err != nil {
        return "", err
    }

    // Resolve symlinks to canonical path
    canonicalPath, err := filepath.EvalSymlinks(exePath)
    if err == nil {
        return canonicalPath, nil
    }

    return exePath, nil
}
```

### Step 2: Store Exe Path in Instance

```go
type Instance struct {
    Name      string
    Command   string
    ExpectedExe string  // NEW: resolved exe path
    PID       int
    // ...
}
```

### Step 3: Match by Exe Path

```go
// For stopped instances, try to find matching process
for _, inst := range state.Instances {
    if inst.Status != "stopped" {
        continue
    }

    // Try to find matching process
    for _, proc := range processes {
        procInfo, _ := ReadProcessInfo(pid)

        // Match by exe path (primary)
        if inst.ExpectedExe != "" && procInfo.Exe != "" {
            if inst.ExpectedExe != procInfo.Exe {
                continue  // Exe mismatch
            }
        } else {
            // Fallback to name matching if exe not available
            expectedName := extractProcessName(inst.Command)
            if procInfo.Name != expectedName {
                continue
            }
        }

        // Verify ports match
        if portsMatch {
            // Match found!
            inst.PID = pid
            inst.Status = "running"
            break
        }
    }
}
```

## Edge Cases

### 1. Deleted Executables

```bash
$ ls -l /proc/12345/exe
lrwxrwxrwx 1 user user 0 Nov 17 14:30 /proc/12345/exe -> /usr/bin/node (deleted)
```

**Solution:** Strip " (deleted)" suffix when comparing

### 2. Exe Not Readable

Some processes have unreadable `/proc/[pid]/exe` (permission denied).

**Solution:** Fallback to name matching if exe is empty

### 3. Command is a Shell Script

```bash
Command: ./start-server.sh
Exe:     /usr/bin/bash  (interpreter, not script!)
```

**Solution:** Use parent chain analysis (already implemented) to find the actual launch script

### 4. Binary Moved/Upgraded

```bash
Instance created:  /opt/app/v1/bin/server
Binary upgraded:   /opt/app/v2/bin/server (new path!)
```

**Solution:**
- Primary match: full exe path
- Secondary fallback: basename + ports
- Let user decide which instance to keep

## Recommendation

**Implement hybrid matching:**

```
Priority 1: Exe path + Ports
Priority 2: Exe basename + Ports
Priority 3: Process name + Ports
Priority 4: Process name only (current)
```

This gives maximum reliability while handling edge cases gracefully.

## Code Location

- **Go:** `process.go:630` - change `procInfo.Name != expectedName`
- **C++:** Need to implement in `src/process.cpp:matchAndUpdateInstances()`

## Benefits

1. **Prevents false positives** - won't match wrong processes
2. **More reliable** - uses kernel data that can't be spoofed
3. **Better for upgrades** - handles binary version changes
4. **Production ready** - used by systemd, supervisord, etc.

## Conclusion

**YES, we should match by exe path!** It's significantly more reliable than name matching. The only reason the current code doesn't is probably because it's simpler to implement name matching first, with exe matching as a "TODO" improvement.
