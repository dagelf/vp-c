# Visual Processmanager 

**Process orchestration with zero assumptions**

Visual Processmanager is an ultra-lean and ultra-flexible process manager built on a radical philosophy: make **zero assumptions** about what resources are or how they work. Everything is user-defined through simple shell commands or great Web UX.

## Philosophy

- **Minimal LoC** - Brutally simple
- **Almost no dependencies** - mostly stdlib 
- **Generic resources** - Not opinionated about ports, files, GPUs, etc.
- **Validation via shell commands** - Use any tool (nc, test, nvidia-smi, lmutil)
- **Pure mechanism, no policy** - Like firmware that provides primitives

## Quick Start

```bash
# Build
go build -o vp

# Start web UI
./vp serve

# Or use CLI
./vp ps
./vp start postgres mydb
./vp stop mydb
```

## Resource System

Resources are just **type:value pairs** validated by **shell commands**:

```bash
# Built-in resources (defaults)
tcpport   -> nc -z localhost ${value}
vncport   -> nc -z localhost ${value}
dbfile    -> test -f ${value}
socket    -> test -S ${value}

The only special resource is workdir, which is where an instance is run.

# Add custom resources
vp resource-type add gpu --check='nvidia-smi -L | grep GPU-${value}'
vp resource-type add license --check='lmutil lmstat -c ${value} | grep "UP"'
```

## Templates

Define how to start processes with resource requirements:

```json
{
  "id": "postgres",
  "label": "PostgreSQL Database",
  "command": "postgres -D ${datadir} -p ${tcpport}",
  "resources": ["tcpport", "datadir"],
  "vars": {
    "datadir": "/tmp/pgdata"
  }
}
```

## Usage

```bash
# Start with auto-allocated resources
vp start postgres mydb

# Start with explicit resource values
vp start postgres mydb --tcpport=5432 --datadir=/var/db

# Mix explicit and auto
vp start qemu vm1 --vncport=5901  # serialport auto-allocated

# List instances
vp ps

# Stop instance
vp stop mydb

# Manage templates
vp template list
vp template add template.json

# Manage resource types
vp resource-type list
vp resource-type add gpu --check='nvidia-smi -L | grep GPU-${value}'
```

## Web UI

```bash
vp serve
# Open http://localhost:8080
```

Features:
- View all instances
- Start/stop with buttons
- Add templates via form
- Add resource types via form
- View resource allocations
- Auto-refresh
- Responsive

## State Storage

Everything persists to `~/.config/vp/state.json`:

```json
{
  "instances": {...},
  "templates": {...},
  "resources": {...},
  "counters": {...},
  "types": {...}
}
```

## Examples

### Custom GPU Resource
```bash
vp resource-type add gpu \
  --check='nvidia-smi -L | grep GPU-${value}' \
  --counter=false

# Now use in templates
vp start ml-training job1 --gpu=0
```

### License Server
```bash
vp resource-type add flexlm \
  --check='lmutil lmstat -c ${value} | grep "UP"'

vp start matlab session1 --flexlm=27000@licserver
```

### Database Connection
```bash
vp resource-type add dbconn \
  --check='psql -h ${value} -c "SELECT 1"'

vp start webapp api --dbconn=localhost:5432/mydb
```

