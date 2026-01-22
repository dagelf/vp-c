# Multi-Host/Multi-User Security Analysis

## 🎯 Understanding the Multi-Host Architecture

### **Dual-Host Design**

VP supports a **source/target host separation**:

```
┌───────────────────────────────────────────────────────┐
│                 Multi-Host Architecture                │
├───────────────────────────────────────────────────────┤
│                                                       │
│  ┌─────────────┐    ┌─────────────┐    ┌───────────┐  │
│  │  Source     │    │  Target     │    │  Remote   │  │
│  │  Host       │    │  Host       │    │  Services │  │
│  │  (Web UI)   │    │  (VP API)   │    │           │  │
│  └─────────────┘    └─────────────┘    └───────────┘  │
│       │                  │                  │           │
│       ▼                  ▼                  │           │
│  ┌─────────────┐    ┌─────────────┐    │           │
│  │  Read       │    │  Execute    │    │           │
│  │  Templates  │    │  Commands   │    │           │
│  │  List       │    │  Manage     │    │           │
│  │  Instances  │    │  Resources  │    │           │
│  └─────────────┘    └─────────────┘    │           │
│       │                  │                  │           │
│       ▼                  ▼                  │           │
│  ┌─────────────┐    ┌─────────────┐    │           │
│  │  GET /api/* │    │  POST /api/*│    │           │
│  └─────────────┘    └─────────────┘    │           │
│       │                  │                  │           │
│       ▼                  ▼                  │           │
│  ┌─────────────┐    ┌─────────────┐    │           │
│  │  Source     │    │  Target     │    │           │
│  │  API Key    │    │  API Key    │    │           │
│  └─────────────┘    └─────────────┘    │           │
│                                       │           │
└───────────────────────────────────────┘           │
                                                        │
                                                        ▼
                                                ┌───────────────┐
                                                │  Remote       │
                                                │  Services     │
                                                │  (PostgreSQL, │
                                                │   VNC, etc.)  │
                                                └───────────────┘
```

### **How It Works**

```javascript
// From web_html.hpp
const isLaunchOperation = (
    endpoint === '/api/instances' && method === 'POST' &&
    body && (body.action === 'start' || body.template)
) || endpoint === '/api/preview-resources';

// Choose host and key based on operation
const host = isLaunchOperation ? config.targetHost : config.sourceHost;
const apiKey = isLaunchOperation ? config.targetApiKey : config.sourceApiKey;
```

### **Operation Types**

| Operation | Host | API Key | Purpose |
|-----------|------|---------|---------|
| **Read Operations** | Source | Source Key | View templates, list instances |
| **Launch Operations** | Target | Target Key | Start processes, allocate resources |
| **Preview Operations** | Target | Target Key | Check resource availability |

## 🔐 Security Implications

### **The Multi-Host Security Model**

#### **1. Source Host (Web UI)**
- **Purpose**: Provide web interface for management
- **Access**: Read-only operations (templates, instances)
- **Security**: Protected by source API key
- **Risk**: Information disclosure if compromised

#### **2. Target Host (VP API)**
- **Purpose**: Execute commands and manage resources
- **Access**: Write operations (start/stop processes)
- **Security**: Protected by target API key
- **Risk**: **COMMAND EXECUTION** if compromised

#### **3. Remote Services**
- **Purpose**: Services managed by VP (PostgreSQL, VNC, etc.)
- **Access**: Controlled by target host
- **Security**: Depends on service configuration
- **Risk**: Service-specific vulnerabilities

### **Security Boundaries**

```
┌───────────────────────────────────────────────────────┐
│                 Trust Boundaries                       │
├───────────────────────────────────────────────────────┤
│                                                       │
│  ┌─────────────────────────────────────────────────┐  │
│  │             User's Trusted Zone                 │  │
│  ├─────────────────────────────────────────────────┤  │
│  │                                                 │  │
│  │  ┌─────────────┐    ┌───────────────────────┐  │  │
│  │  │  User       │    │  Source Host         │  │  │
│  │  │  (Browser)  │    │  (Web UI)            │  │  │
│  │  └─────────────┘    └───────────────────────┘  │  │
│  │        ▲                   │                  │  │
│  │        │                   ▼                  │  │
│  │  ┌─────┴─────┐      ┌─────────────────────┐  │  │
│  │  │  Source   │      │  Target Host        │  │  │
│  │  │  API Key  │      │  (VP API)           │  │  │
│  │  └───────────┘      └─────────────────────┘  │  │
│  │        ▲                   │                  │  │
│  │        │                   ▼                  │  │
│  │  ┌─────┴─────┐      ┌─────────────────────┐  │  │
│  │  │  Read     │      │  Execute            │  │  │
│  │  │  Only      │      │  Commands           │  │  │
│  │  └───────────┘      └─────────────────────┘  │  │
│  │                                                 │  │
│  └─────────────────────────────────────────────────┘  │
│                                                       │
│  ❌ Untrusted Network                                  │
│                                                       │
└───────────────────────────────────────────────────────┘
```

## 🚨 Security Risks in Multi-Host Scenario

### **1. Target Host Compromise** ❌ CRITICAL

**Scenario**: Attacker gains access to target API key

**Impact**:
- ✅ Can execute arbitrary commands on target host
- ✅ Can allocate/deallocate resources
- ✅ Can start/stop any managed processes
- ✅ Can access services managed by VP

**Mitigation**:
- ✅ Keep target API key secret
- ✅ Use strong, random API keys
- ✅ Rotate API keys regularly
- ✅ Use separate keys for source/target

### **2. Source Host Compromise** ⚠️ MEDIUM

**Scenario**: Attacker gains access to source API key

**Impact**:
- ✅ Can read templates and instances
- ✅ Can see resource allocations
- ✅ Can see process status
- ❌ **Cannot execute commands**
- ❌ **Cannot modify state**

**Mitigation**:
- ✅ Keep source API key secret
- ✅ Use read-only accounts if possible
- ✅ Monitor read access

### **3. Cross-Site Request Forgery (CSRF)** ⚠️ MEDIUM

**Scenario**: Malicious website tricks user into making API requests

**Impact**:
- ✅ Could execute commands if user is authenticated
- ✅ Could modify state
- ✅ Could allocate resources

**Mitigation**:
- ✅ Implement CSRF tokens
- ✅ Use SameSite cookies
- ✅ Validate Origin header
- ✅ Require explicit user confirmation for sensitive operations

### **4. API Key Leakage** ⚠️ MEDIUM

**Scenario**: API keys stored in browser localStorage

**Impact**:
- ✅ XSS attacks can steal API keys
- ✅ Malicious browser extensions can access keys
- ✅ Keys persist in browser storage

**Mitigation**:
- ✅ Use HttpOnly cookies instead of localStorage
- ✅ Implement short-lived session tokens
- ✅ Provide key rotation mechanism
- ✅ Warn users about browser security

### **5. Network Eavesdropping** ⚠️ MEDIUM

**Scenario**: API keys transmitted over unencrypted connections

**Impact**:
- ✅ Keys can be intercepted
- ✅ Commands can be intercepted
- ✅ State can be intercepted

**Mitigation**:
- ✅ Use HTTPS/TLS for all communications
- ✅ Use reverse proxy with SSL termination
- ✅ Implement certificate pinning
- ✅ Warn about unencrypted connections

## 🛡️ Security Recommendations

### **For Multi-Host Deployments**

#### **1. Network Architecture**
```
┌───────────────────────────────────────────────────────┐
│                 Secure Multi-Host Setup                │
├───────────────────────────────────────────────────────┤
│                                                       │
│  ┌─────────────┐    ┌─────────────┐    ┌───────────┐  │
│  │  User       │    │  Reverse    │    │  Target  │  │
│  │  (Browser)  │───►│  Proxy      │───►│  Host    │  │
│  └─────────────┘    │  (HTTPS)    │    │  (VP)    │  │
│                     └─────────────┘    └───────────┘  │
│          ▲                  │                  │       │
│          │                  │                  │       │
│  ┌───────┴─────┐    ┌──────┴──────┐    ┌─────┴─────┐│
│  │  HTTPS      │    │  Auth       │    │  Local  ││
│  │  (TLS 1.3)  │    │  (Basic/    │    │  Firewall││
│  │             │    │  Bearer)    │    │          ││
│  └─────────────┘    └─────────────┘    └──────────┘│
│                                                       │
└───────────────────────────────────────────────────────┘
```

#### **2. API Key Management**

**Best Practices**:
```bash
# Generate strong API keys
vp key  # Uses cryptographically secure random generation

# Use separate keys for source and target
vp key > source_key.txt
vp key > target_key.txt

# Rotate keys regularly
vp key  # Generate new key
# Update all clients with new key

# Revoke compromised keys
vp key  # Generate new key (old one becomes invalid)
```

#### **3. Network Configuration**

**Secure Setup**:
```bash
# Bind to specific interface (not 0.0.0.0)
vp serve 192.168.1.100:8080

# Use firewall rules
ufw allow from 192.168.1.0/24 to any port 8080
ufw deny from any to any port 8080

# Use reverse proxy with HTTPS
# Nginx example:
location /vp/ {
    proxy_pass http://localhost:8080/;
    proxy_set_header X-Real-IP $remote_addr;
    
    # HTTPS
    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;
    
    # Authentication
    auth_basic "VP Access";
    auth_basic_user_file /etc/nginx/.htpasswd;
}
```

#### **4. Access Control**

**Recommended Setup**:
```json
// In state.json
{
  "remotes_allowed": {
    "https://trusted-host.example.com": true,
    "http://localhost:8080": true,
    "https://another-trusted.example.com": true
  }
}
```

### **For Service Management**

#### **1. Service Binding**

**Secure Patterns**:
```bash
# ✅ SAFE: Bind to localhost only (default)
vp new postgres mydb  # PostgreSQL binds to localhost

# ✅ SAFE: Explicit localhost binding
vp new postgres mydb --bind=127.0.0.1

# ❌ UNSAFE: Binding to all interfaces
# (This would require template modification)
```

#### **2. Resource Access**

**Access Control**:
```bash
# Use firewall to restrict access to VP-managed services
ufw allow from 127.0.0.1 to any port 5432  # PostgreSQL
ufw allow from 192.168.1.0/24 to any port 5432
ufw deny from any to any port 5432

# Use SSH tunneling for remote access
ssh -L 5432:localhost:5432 user@target-host
```

#### **3. Template Security**

**Safe Template Design**:
```json
{
  "id": "postgres-secure",
  "label": "Secure PostgreSQL",
  "command": "postgres -D ${datadir} -p ${tcpport} -h 127.0.0.1",
  "resources": ["tcpport", "datadir"],
  "vars": {
    "datadir": "/var/lib/postgresql/data"
  }
}
```

## 📋 Multi-Host Security Checklist

### **✅ Basic Security**
- [ ] Use separate API keys for source and target hosts
- [ ] Keep API keys secret (don't commit to version control)
- [ ] Bind VP to specific interfaces (not 0.0.0.0)
- [ ] Use firewall rules to restrict access
- [ ] Review templates before adding them

### **✅ Enhanced Security**
- [ ] Use HTTPS for all communications
- [ ] Implement additional authentication (basic auth)
- [ ] Rotate API keys regularly
- [ ] Monitor API access logs
- [ ] Use separate user accounts for VP

### **✅ Advanced Security**
- [ ] Implement IP whitelisting
- [ ] Use mutual TLS (mTLS) for host authentication
- [ ] Implement rate limiting
- [ ] Add audit logging
- [ ] Use process isolation (containers)

## 🚫 Multi-Host Anti-Patterns

### **❌ Don't Share API Keys**
```bash
# ❌ UNSAFE: Using same key for multiple hosts
vp key > shared_key.txt
# Use same key for source and target

# ✅ SAFE: Use separate keys
vp key > source_key.txt
vp key > target_key.txt
```

### **❌ Don't Expose Target Host**
```bash
# ❌ UNSAFE: Exposing target host to internet
vp serve 0.0.0.0:8080

# ✅ SAFE: Bind to internal interface
vp serve 192.168.1.100:8080
```

### **❌ Don't Use Weak Keys**
```bash
# ❌ UNSAFE: Using weak API keys
vp key "password123"

# ✅ SAFE: Use cryptographically strong keys
vp key  # Auto-generates strong key
```

### **❌ Don't Disable Authentication**
```bash
# ❌ UNSAFE: Disabling authentication
vp key ""  # Clears API key

# ✅ SAFE: Always use authentication for multi-host
vp key  # Generate and use strong key
```

## 🔍 Multi-Host Threat Modeling

### **Attack Scenarios**

#### **1. Compromised Source Host**
**Attack Path**:
```
Attacker → Source Host → Steal Source Key → Read Data
```
**Impact**: Information disclosure only
**Mitigation**: Separate source/target keys, monitor read access

#### **2. Compromised Target Host**
**Attack Path**:
```
Attacker → Target Host → Steal Target Key → Execute Commands
```
**Impact**: Full command execution, resource management
**Mitigation**: Strong target key, network isolation, monitoring

#### **3. Network Eavesdropping**
**Attack Path**:
```
Attacker → Network → Intercept Traffic → Steal Keys
```
**Impact**: Full compromise of both hosts
**Mitigation**: HTTPS/TLS, VPN, network segmentation

#### **4. Cross-Site Scripting (XSS)**
**Attack Path**:
```
Attacker → Malicious Website → User Browser → Steal Keys
```
**Impact**: Full compromise via stolen keys
**Mitigation**: HttpOnly cookies, CSP headers, input validation

### **Security Controls**

| Threat | Control | Implementation |
|--------|---------|----------------|
| Unauthorized Access | Authentication | API keys, HTTPS basic auth |
| Information Disclosure | Encryption | HTTPS/TLS |
| Command Injection | Input Validation | Template validation |
| Key Theft | Secure Storage | HttpOnly cookies, key rotation |
| Network Eavesdropping | Encryption | TLS 1.2+, certificate pinning |
| Brute Force | Rate Limiting | Request throttling |
| Privilege Escalation | Isolation | Separate user accounts, containers |

## 🛡️ Multi-Host Security Architecture

### **Recommended Setup**

```
┌───────────────────────────────────────────────────────┐
│                 Secure Multi-Host Architecture         │
├───────────────────────────────────────────────────────┤
│                                                       │
│  ┌─────────────┐    ┌─────────────┐    ┌───────────┐  │
│  │  User       │    │  Reverse    │    │  Target  │  │
│  │  (Browser)  │───►│  Proxy      │───►│  Host    │  │
│  └─────────────┘    │  (Nginx)    │    │  (VP)    │  │
│                     └─────────────┘    └───────────┘  │
│          ▲                  │                  │       │
│          │                  │                  │       │
│  ┌───────┴─────┐    ┌──────┴──────┐    ┌─────┴─────┐│
│  │  HTTPS      │    │  Auth       │    │  Local  ││
│  │  (TLS 1.3)  │    │  (Basic +   │    │  Only   ││
│  │  Cert Pin   │    │  API Key)   │    │  Services││
│  └─────────────┘    └─────────────┘    └──────────┘│
│          ▲                  │                  │       │
│          │                  │                  │       │
│  ┌───────┴─────┐    ┌──────┴──────┐    ┌─────┴─────┐│
│  │  Firewall   │    │  Rate      │    │  Process ││
│  │  Rules      │    │  Limiting  │    │  Isolation││
│  └─────────────┘    └─────────────┘    └──────────┘│
│          ▲                  │                  │       │
│          │                  │                  │       │
│  ┌───────┴─────┐    ┌──────┴──────┐    ┌─────┴─────┐│
│  │  Monitoring │    │  Logging   │    │  Backup  ││
│  │  (Prometheus)│    │  (ELK)     │    │  Strategy││
│  └─────────────┘    └─────────────┘    └──────────┘│
│                                                       │
└───────────────────────────────────────────────────────┘
```

### **Implementation Guide**

#### **1. Reverse Proxy (Nginx)**
```nginx
server {
    listen 443 ssl;
    server_name vp.example.com;

    ssl_certificate /etc/letsencrypt/live/vp.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/vp.example.com/privkey.pem;

    location / {
        proxy_pass http://localhost:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # Authentication
        auth_basic "VP Access";
        auth_basic_user_file /etc/nginx/.htpasswd;

        # Security headers
        add_header X-Frame-Options "SAMEORIGIN";
        add_header X-Content-Type-Options "nosniff";
        add_header X-XSS-Protection "1; mode=block";
        add_header Content-Security-Policy "default-src 'self'";

        # Rate limiting
        limit_req zone=vp_limit burst=10 nodelay;
    }
}
```

#### **2. Firewall Rules (UFW)**
```bash
# Allow only specific IPs
ufw allow from 192.168.1.0/24 to any port 8080
ufw allow from 10.0.0.0/8 to any port 8080
ufw deny from any to any port 8080

# Allow SSH for management
ufw allow from 192.168.1.100 to any port 22

# Enable firewall
ufw enable
```

#### **3. API Key Rotation Script**
```bash
#!/bin/bash
# rotate_api_keys.sh

# Generate new keys
NEW_SOURCE_KEY=$(vp key)
NEW_TARGET_KEY=$(vp key)

# Backup old keys
cp ~/.config/vp/state.json ~/.config/vp/state.json.bak

# Update state with new keys
# (This would need to be implemented in VP)
vp update-key source $NEW_SOURCE_KEY
vp update-key target $NEW_TARGET_KEY

# Restart VP to apply new keys
pkill vp
vp serve &

echo "API keys rotated successfully"
echo "New source key: $NEW_SOURCE_KEY"
echo "New target key: $NEW_TARGET_KEY"
```

## 🎯 Conclusion

### **Multi-Host Security Summary**

**VP's multi-host architecture is secure by design** when properly configured:

1. ✅ **Separation of concerns**: Source (read) vs Target (write)
2. ✅ **Strong authentication**: API keys for all operations
3. ✅ **No privilege escalation**: Runs with user permissions
4. ✅ **Explicit access control**: Users define what's allowed

**Key security principles for multi-host**:

1. **Keep target host secure**: This is where commands execute
2. **Use separate API keys**: Don't share keys between hosts
3. **Restrict network access**: Use firewalls and VPNs
4. **Monitor activity**: Watch for unusual patterns
5. **Rotate keys regularly**: Prevent long-term compromise

**The system is secure when**:
- Target host is properly protected
- API keys are kept secret
- Network access is restricted
- Users understand the security model

**VP provides the mechanism, users define the policy** - this is the core philosophy that makes it both powerful and secure when used correctly.