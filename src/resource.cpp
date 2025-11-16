#include "resource.hpp"
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace vp {

std::map<std::string, std::shared_ptr<ResourceType>> defaultResourceTypes() {
    std::map<std::string, std::shared_ptr<ResourceType>> types;

    auto tcpport = std::make_shared<ResourceType>();
    tcpport->name = "tcpport";
    tcpport->check = "nc -z localhost ${value}";
    tcpport->counter = true;
    tcpport->start = 3000;
    tcpport->end = 9999;
    types["tcpport"] = tcpport;

    auto vncport = std::make_shared<ResourceType>();
    vncport->name = "vncport";
    vncport->check = "nc -z localhost ${value}";
    vncport->counter = true;
    vncport->start = 5900;
    vncport->end = 5999;
    types["vncport"] = vncport;

    auto serialport = std::make_shared<ResourceType>();
    serialport->name = "serialport";
    serialport->check = "nc -z localhost ${value}";
    serialport->counter = true;
    serialport->start = 9600;
    serialport->end = 9699;
    types["serialport"] = serialport;

    auto dbfile = std::make_shared<ResourceType>();
    dbfile->name = "dbfile";
    dbfile->check = "test -f ${value}";
    dbfile->counter = false;
    dbfile->start = 0;
    dbfile->end = 0;
    types["dbfile"] = dbfile;

    auto socket = std::make_shared<ResourceType>();
    socket->name = "socket";
    socket->check = "test -S ${value}";
    socket->counter = false;
    socket->start = 0;
    socket->end = 0;
    types["socket"] = socket;

    auto datadir = std::make_shared<ResourceType>();
    datadir->name = "datadir";
    datadir->check = "";
    datadir->counter = false;
    datadir->start = 0;
    datadir->end = 0;
    types["datadir"] = datadir;

    auto workdir = std::make_shared<ResourceType>();
    workdir->name = "workdir";
    workdir->check = "";
    workdir->counter = false;
    workdir->start = 0;
    workdir->end = 0;
    types["workdir"] = workdir;

    return types;
}

bool checkResource(const ResourceType& rt, const std::string& value) {
    if (rt.check.empty()) {
        return true; // No check command = always available
    }

    // Interpolate check command
    std::string check = rt.check;
    size_t pos = 0;
    while ((pos = check.find("${value}", pos)) != std::string::npos) {
        check.replace(pos, 8, value);
        pos += value.length();
    }

    // Execute check
    int result = system(check.c_str());

    // Natural command behavior: exit 0 = exists/in-use (not available)
    // exit 1 = free/doesn't exist (available)
    return result != 0; // Resource is available if check command fails
}

std::string allocateResource(std::shared_ptr<State> state, const std::string& rtype, const std::string& requestedValue) {
    auto it = state->types.find(rtype);
    if (it == state->types.end()) {
        throw std::runtime_error("unknown resource type: " + rtype);
    }

    auto rt = it->second;
    std::string value;

    if (rt->counter && requestedValue.empty()) {
        // Auto-increment counter
        int current = state->counters[rtype];
        if (current == 0) {
            current = rt->start;
        }

        bool found = false;
        for (int v = current; v <= rt->end; v++) {
            value = std::to_string(v);
            if (checkResource(*rt, value)) {
                state->counters[rtype] = v + 1;
                found = true;
                break;
            }
        }

        if (!found) {
            std::stringstream ss;
            ss << "no available " << rtype << " in range " << rt->start << "-" << rt->end;
            throw std::runtime_error(ss.str());
        }
    } else {
        // Explicit value requested or non-counter resource
        if (!requestedValue.empty()) {
            value = requestedValue;
        } else {
            throw std::runtime_error("resource type " + rtype + " requires explicit value");
        }

        if (!checkResource(*rt, value)) {
            throw std::runtime_error(rtype + " " + value + " not available");
        }
    }

    return value;
}

} // namespace vp
