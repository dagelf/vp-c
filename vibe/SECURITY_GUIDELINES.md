# Visual Process Manager - Security Guidelines

## 🎯 Security Philosophy

**VP is designed as a single-user, local development tool** - not a security boundary or multi-tenant system.

### Core Principles
- **No false security**: We don't pretend to prevent users from doing what they intend
- **Explicit control**: Users explicitly define what commands to execute
- **No privilege escalation**: Everything runs with the user's own permissions
- **User responsibility**: Users are responsible for understanding what they execute

## ✅ Security by Design

### 1. No Privilege Escalation
- ✅ All processes run with the user's permissions
- ✅ No sudo/su functionality
- ✅ No root access required or provided

### 2. Explicit Command Execution
- ✅ Users explicitly define templates and commands
- ✅ No hidden or implicit command execution
- ✅ All commands are visible in templates and logs

### 3. Proper Authentication
- ✅ API key authentication works correctly when enabled
- ✅ No authentication bypass possible
- ✅ Authentication is optional for local development

### 4. Transparent Operation
- ✅ All state is visible in `~/.config/vp/state.json`
- ✅ All commands are logged and visible
- ✅ No hidden functionality or backdoors

## ⚠️ User Responsibilities

### For Safe Usage

#### 1. Keep It Local
```bash
# ✅ SAFE: Default localhost binding
vp serve

# ❌ UNSAFE: Exposing to network
vp serve 0.0.0.0
```

#### 2. Set Proper Permissions
```bash
# ✅ SAFE: Restrict state file access
chmod 600 ~/.config/vp/state.json
chmod 700 ~/.config/vp/
```

#### 3. Enable Authentication When Needed
```bash
# ✅ SAFE: Enable API key for shared environments
vp key  # Generates and sets API key

# Use with web UI or API clients
curl -H "X-VP-Key: your-api-key" http://localhost:8080/api/instances
```

#### 4. Understand Your Commands
```bash
# ✅ SAFE: Review templates before using
vp template show postgres

# ❌ UNSAFE: Running untrusted templates
vp template add untrusted.json
vp new untrusted myinstance
```

## 🛡️ Security Features

### Authentication System
- **API Key**: Optional but recommended for shared environments
- **Header-based**: `X-VP-Key` header for API requests
- **Web UI Integration**: API key stored in browser localStorage

### State Management
- **JSON Format**: Human-readable state file
- **Atomic Writes**: Prevents corruption during saves
- **Backup Friendly**: Easy to backup and restore

### Process Control
- **Explicit Commands**: All commands visible in templates
- **No Hidden Execution**: Everything is user-defined
- **Process Isolation**: Each process runs independently

## 🌐 Network Security

### Intended Use: Localhost Only
```
┌───────────────────────────────────────┐
│           Single User System           │
├───────────────────────────────────────┤
│                                       │
│  ┌─────────┐    ┌─────────────┐      │
│  │  User   │───►│  VP Local   │      │
│  └─────────┘    └─────────────┘      │
│          ▲               │            │
│          │               ▼            │
│  ┌───────┴─────┐  ┌─────────────┐    │
│  │  Processes │  │  State File │    │
│  └─────────────┘  └─────────────┘    │
│                                       │
└───────────────────────────────────────┘
```

### If Network Exposure is Needed
```
┌───────────────────────────────────────┐
│           Networked Environment        │
├───────────────────────────────────────┤
│                                       │
│  ┌─────────┐    ┌─────────────┐      │
│  │  Users  │───►│  Reverse    │───►  │
│  └─────────┘    │  Proxy      │      │
│                 │  (HTTPS)    │      │
│  ┌─────────┐    └─────────────┘      │
│  │  Admin  │───►│  VP Local   │      │
│  └─────────┘    └─────────────┘      │
│          ▲               │            │
│          │               ▼            │
│  ┌───────┴─────┐  ┌─────────────┐    │
│  │  Processes │  │  State File │    │
│  └─────────────┘  └─────────────┘    │
│                                       │
└───────────────────────────────────────┘
```

**Recommended Setup**:
```bash
# Use reverse proxy with HTTPS and authentication
# Example: Nginx configuration

location /vp/ {
    proxy_pass http://localhost:8080/;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header Host $host;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    
    # Authentication
    auth_basic "VP Access";
    auth_basic_user_file /etc/nginx/.htpasswd;
    
    # HTTPS
    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;
}
```

## 🔧 Security Configuration

### File Permissions
```bash
# Set secure permissions
mkdir -p ~/.config/vp
chmod 700 ~/.config/vp
chmod 600 ~/.config/vp/state.json
```

### API Key Management
```bash
# Generate new API key
vp key

# Set specific API key
echo "my-secret-key" | vp key

# Remove API key (disable authentication)
echo "" | vp key
```

### Network Configuration
```bash
# Bind to specific interface (more secure than 0.0.0.0)
vp serve 127.0.0.1:8080

# Use Unix domain socket for local-only access
# (Future feature)
```

## 📋 Security Checklist

### ✅ Basic Security
- [ ] Keep VP on localhost (default)
- [ ] Set proper file permissions (chmod 600)
- [ ] Understand commands before executing
- [ ] Review templates before adding
- [ ] Review resource types before adding

### ✅ Enhanced Security
- [ ] Enable API key authentication
- [ ] Use reverse proxy with HTTPS
- [ ] Add basic authentication
- [ ] Restrict network access
- [ ] Monitor process activity

### ✅ Advanced Security
- [ ] Use process isolation (containers)
- [ ] Implement network segmentation
- [ ] Add audit logging
- [ ] Implement backup strategy
- [ ] Regular security reviews

## 🚫 Anti-Patterns to Avoid

### ❌ Don't Expose to Untrusted Networks
```bash
# ❌ UNSAFE: Exposing to all interfaces
vp serve 0.0.0.0:8080

# ❌ UNSAFE: Exposing to public internet
vp serve your-public-ip:8080
```

### ❌ Don't Use Untrusted Templates
```bash
# ❌ UNSAFE: Downloading templates from untrusted sources
curl https://untrusted.com/template.json | vp template add -

# ✅ SAFE: Review templates first
curl https://trusted.com/template.json > template.json
vim template.json  # Review contents
vp template add template.json
```

### ❌ Don't Share State Files
```bash
# ❌ UNSAFE: Sharing state files with sensitive data
cp ~/.config/vp/state.json /shared/folder/

# ✅ SAFE: Clean state before sharing
vp stop all
vp delete all
# Remove sensitive templates/resources
vp key  # Clear API key
cp ~/.config/vp/state.json /shared/folder/
```

## 🔍 Security Monitoring

### Log Monitoring
```bash
# Monitor VP activity (future feature)
tail -f ~/.config/vp/vp.log

# Monitor process activity
watch -n 1 "vp ps"
```

### Resource Monitoring
```bash
# Check resource usage
vp ps

# Check allocated resources
# (Future: vp resources)
```

### Network Monitoring
```bash
# Check network connections
netstat -tulnp | grep vp

# Check open ports
ss -tulnp | grep 8080
```

## 🛡️ Security Best Practices

### For Users
1. **Understand the tool**: VP executes what you tell it to execute
2. **Review everything**: Templates, resource types, commands
3. **Start small**: Test with simple templates first
4. **Monitor activity**: Keep an eye on running processes
5. **Secure your system**: VP is as secure as your system
6. **Use authentication**: Enable API key for shared environments
7. **Keep it local**: Don't expose to untrusted networks
8. **Backup regularly**: State files contain important data
9. **Stay updated**: Use latest version for bug fixes
10. **Report issues**: Help improve security for everyone

### For Developers
1. **Document clearly**: Explain security model and responsibilities
2. **Warn appropriately**: Highlight risks of network exposure
3. **Provide secure defaults**: File permissions, authentication
4. **Make security easy**: Simple commands for secure setup
5. **Educate users**: Clear documentation about safe usage
6. **Handle errors safely**: Don't leak sensitive information
7. **Validate inputs**: Prevent crashes from malformed data
8. **Test thoroughly**: Include security scenarios in tests
9. **Respond to issues**: Quick fixes for reported vulnerabilities
10. **Disclose responsibly**: Transparent security communication

## 🔒 Security Model

### Trust Boundaries
```
┌───────────────────────────────────────────────────────┐
│                 Trust Boundary                         │
├───────────────────────────────────────────────────────┤
│                                                       │
│  ┌─────────────────────────────────────────────────┐  │
│  │             VP Trusted Zone                      │  │
│  ├─────────────────────────────────────────────────┤  │
│  │                                                 │  │
│  │  ┌─────────────┐    ┌───────────────────────┐  │  │
│  │  │  User       │    │  VP Core             │  │  │
│  │  └─────────────┘    └───────────────────────┘  │  │
│  │        ▲                   │                  │  │
│  │        │                   ▼                  │  │
│  │  ┌─────┴─────┐      ┌─────────────────────┐  │  │
│  │  │  Commands │      │  State Management   │  │  │
│  │  └───────────┘      └─────────────────────┘  │  │
│  │                                                 │  │
│  └─────────────────────────────────────────────────┘  │
│                                                       │
│  ❌ Untrusted Network                                  │
│                                                       │
└───────────────────────────────────────────────────────┘
```

### What VP Trusts
- ✅ The user knows what they're doing
- ✅ Commands are intentionally executed
- ✅ Templates are reviewed before use
- ✅ Resource types are validated
- ✅ State file is protected by filesystem

### What VP Doesn't Trust
- ❌ Untrusted network connections
- ❌ Malicious templates/resources
- ❌ Unreviewed commands
- ❌ External input without validation

## 🚨 Security Incident Response

### Reporting Issues
```
1. **Private Disclosure**: Open GitHub Security Advisory
2. **Public Discussion**: GitHub Issues with "security" label
3. **Email**: Contact maintainers directly
```

### Response Process
```
1. **Acknowledgment**: Within 24 hours
2. **Assessment**: Within 48 hours
3. **Fix Development**: Based on severity
4. **Release**: Security patch as needed
5. **Disclosure**: Transparent communication
```

### Severity Levels
```
┌─────────────┌─────────────────┌─────────────────┐
│ Critical    │ High            │ Medium          │
├─────────────┼─────────────────┼─────────────────┤
│ Remote code │ Authentication  │ Information      │
│ execution   │ bypass          │ disclosure      │
│ Privilege   │ Data corruption │ Denial of       │
│ escalation  │                 │ service         │
├─────────────┼─────────────────┼─────────────────┤
│ Fix in      │ Fix in          │ Fix in next     │
│ 24 hours    │ 1 week          │ minor release   │
└─────────────┴─────────────────┴─────────────────┘
```

## 📚 Security Resources

### Documentation
- **Official Docs**: README.md, TECHNICAL_SPEC.md
- **Security Guide**: This document
- **API Reference**: API documentation

### Tools
- **File Permissions**: `chmod`, `chown`
- **Network Monitoring**: `netstat`, `ss`, `iftop`
- **Process Monitoring**: `ps`, `top`, `htop`
- **Authentication**: `htpasswd`, reverse proxies

### Best Practices
- **OWASP**: Application security guidelines
- **CIS Benchmarks**: System hardening
- **NIST**: Secure software development

## 🎯 Conclusion

**Visual Process Manager is secure by design for its intended use case** as a single-user, local development tool. The security model is based on:

1. **User responsibility**: You control what gets executed
2. **Transparency**: All operations are visible and explicit
3. **No false security**: We don't pretend to be a security boundary
4. **Proper defaults**: Secure out-of-the-box for local use

**For safe usage**:
- Keep it local (localhost)
- Review what you execute
- Enable authentication when needed
- Secure your system properly

**VP gives you the power to control processes** - use that power responsibly!