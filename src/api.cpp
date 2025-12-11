#include "api.hpp"
#include "process.hpp"
#include "resource.hpp"
#include "types.hpp"
#include "web_html.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <thread>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace vp {

// Helper to read file contents (fallback for development)
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

static std::shared_ptr<State> g_state;

static std::string getHeaderValue(const std::string& headers, const std::string& name) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::istringstream iss(headers);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string hname = line.substr(0, colon);
        std::string hval = line.substr(colon + 1);
        // trim leading spaces
        hval.erase(0, hval.find_first_not_of(" \t"));
        std::transform(hname.begin(), hname.end(), hname.begin(), ::tolower);
        if (hname == lowerName) return hval;
    }
    return "";
}

std::string handleRequest(const std::string& method, const std::string& path, const std::string& body, const std::string& headers) {
    std::ostringstream response;

    // Handle CORS preflight first (before auth)
    if (method == "OPTIONS") {
        response << "HTTP/1.1 204 No Content\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
        response << "Access-Control-Allow-Headers: Content-Type, X-VP-Key\r\n";
        response << "\r\n";
        return response.str();
    }

    // Serve web.html (embedded or from file for development) - NO AUTH REQUIRED
    // The web UI will handle authentication via the API
    if (path == "/" && method == "GET") {
        std::string html;

        // Try to read from file first (for development/hot-reload)
        html = readFile("web.html");

        // Fall back to embedded version
        if (html.empty()) {
            html = EMBEDDED_WEB_HTML;
        }

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/html\r\n";
        response << "Content-Length: " << html.length() << "\r\n";
        response << "\r\n";
        response << html;
        return response.str();
    }

    // Auth check for ALL API endpoints
    bool isApiPath = path.rfind("/api", 0) == 0;
    if (isApiPath && !g_state->apiKey.empty()) {
        std::string key = getHeaderValue(headers, "X-VP-Key");
        if (key != g_state->apiKey) {
            response << "HTTP/1.1 401 Unauthorized\r\n";
            response << "Content-Type: text/plain\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: 12\r\n";
            response << "\r\n";
            response << "Unauthorized";
            return response.str();
        }
    }

    if (path == "/api/instances" && method == "GET") {
        matchAndUpdateInstances(g_state);

        // Serialize instances to JSON
        json instances_json = json::object();
        for (const auto& [key, value] : g_state->instances) {
            instances_json[key] = *value;
        }
        std::string body = instances_json.dump(2);

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "\r\n";
        response << body;
        return response.str();
    }

    if (path == "/api/templates" && method == "GET") {
        // Serialize templates to JSON
        json templates_json = json::object();
        for (const auto& [key, value] : g_state->templates) {
            templates_json[key] = *value;
        }
        std::string body_str = templates_json.dump(2);

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body_str.length() << "\r\n";
        response << "\r\n";
        response << body_str;
        return response.str();
    }

    // GET /api/resources - List allocated resources
    if (path == "/api/resources" && method == "GET") {
        json resources_json = json::array();
        for (const auto& [key, value] : g_state->resources) {
            json res_obj = *value;
            res_obj["key"] = key;  // Add the key for reference
            resources_json.push_back(res_obj);
        }
        std::string body_str = resources_json.dump(2);

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body_str.length() << "\r\n";
        response << "\r\n";
        response << body_str;
        return response.str();
    }

    // GET /api/resource-types - List resource types
    if (path == "/api/resource-types" && method == "GET") {
        json types_json = json::object();
        for (const auto& [key, value] : g_state->types) {
            types_json[key] = *value;
        }
        std::string body_str = types_json.dump(2);

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body_str.length() << "\r\n";
        response << "\r\n";
        response << body_str;
        return response.str();
    }

    // GET /api/debug/key - Show stored API key (for debugging)
    if (path == "/api/debug/key" && method == "GET") {
        json result;
        result["stored_key"] = g_state->apiKey;
        result["key_length"] = g_state->apiKey.length();
        result["key_empty"] = g_state->apiKey.empty();
        std::string body_str = result.dump(2);

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body_str.length() << "\r\n";
        response << "\r\n";
        response << body_str;
        return response.str();
    }

    // GET /api/config - Get configuration
    if (path == "/api/config" && method == "GET") {
        std::string body_str = g_state->toJson();

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body_str.length() << "\r\n";
        response << "\r\n";
        response << body_str;
        return response.str();
    }

    // POST /api/config - Update configuration
    if (path == "/api/config" && method == "POST") {
        if (g_state->fromJson(body)) {
            g_state->save();
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: 2\r\n";
            response << "\r\n";
            response << "{}";
        } else {
            std::string error = "Invalid configuration JSON";
            response << "HTTP/1.1 400 Bad Request\r\n";
            response << "Content-Type: text/plain\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: " << error.length() << "\r\n";
            response << "\r\n";
            response << error;
        }
        return response.str();
    }

    // GET /api/discover - Discover processes
    if (path.find("/api/discover") == 0 && method == "GET") {
        bool portsOnly = path.find("ports_only=true") != std::string::npos;
        // Use cache if available (2 seconds max age)
        auto discovered = discoverProcesses(g_state, true, portsOnly, 2);
        
        // Also update managed instances using this fresh data
        matchAndUpdateInstances(g_state, &discovered);

        json result_json = json::array();
        for (const auto& proc : discovered) {
            json proc_json;
            for (const auto& [key, value] : proc) {
                if (key == "ports") {
                    // Convert comma-separated ports string to array
                    json ports_array = json::array();
                    if (!value.empty()) {
                        std::istringstream iss(value);
                        std::string port;
                        while (std::getline(iss, port, ',')) {
                            ports_array.push_back(std::stoi(port));
                        }
                    }
                    proc_json["ports"] = ports_array;
                } else {
                    proc_json[key] = value;
                }
            }
            result_json.push_back(proc_json);
        }

        std::string body_str = result_json.dump(2);

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body_str.length() << "\r\n";
        response << "\r\n";
        response << body_str;
        return response.str();
    }

    // POST /api/monitor - Monitor existing process
    if (path == "/api/monitor" && method == "POST") {
        try {
            json req = json::parse(body);
            int pid = req.value("pid", 0);
            std::string name = req.value("name", "");

            if (pid <= 0 || name.empty()) {
                std::string error_body = R"({"error": "Invalid pid or name"})";
                response << "HTTP/1.1 400 Bad Request\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
                return response.str();
            }

            auto inst = monitorProcess(g_state, pid, name);
            if (inst) {
                json result = *inst;
                std::string body_str = result.dump(2);
                response << "HTTP/1.1 200 OK\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << body_str.length() << "\r\n";
                response << "\r\n";
                response << body_str;
            } else {
                std::string error_body = R"({"error": "Failed to monitor process"})";
                response << "HTTP/1.1 500 Internal Server Error\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
            }
            return response.str();
        } catch (const std::exception& e) {
            std::string error_body = R"({"error": "Invalid request"})";
            response << "HTTP/1.1 400 Bad Request\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Content-Length: " << error_body.length() << "\r\n";
            response << "\r\n";
            response << error_body;
            return response.str();
        }
    }

    // POST /api/execute-action - Execute action for an instance
    if (path == "/api/execute-action" && method == "POST") {
        try {
            json req = json::parse(body);
            std::string actionToExecute = req.value("action", "");

            // If action is directly provided, use it (no instance lookup needed)
            if (actionToExecute.empty()) {
                // Need instance lookup for launcher or default action
                std::string instanceId = req.value("instance_id", "");
                // Fallback to instance_name for backwards compatibility
                if (instanceId.empty()) {
                    instanceId = req.value("instance_name", "");
                }

                if (g_state->instances.find(instanceId) == g_state->instances.end()) {
                    std::string error_body = R"({"error": "Instance not found"})";
                    response << "HTTP/1.1 404 Not Found\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Access-Control-Allow-Origin: *\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                    return response.str();
                }

                auto inst = g_state->instances[instanceId];

                // Check for launcher key
                std::string launcherKey = req.value("launcher", "");
                if (!launcherKey.empty()) {
                    // Look up launcher in instance's launchers map
                    if (inst->launchers.find(launcherKey) != inst->launchers.end()) {
                        actionToExecute = inst->launchers[launcherKey];
                    } else {
                        std::string error_body = R"({"error": "Launcher not found"})";
                        response << "HTTP/1.1 404 Not Found\r\n";
                        response << "Content-Type: application/json\r\n";
                        response << "Access-Control-Allow-Origin: *\r\n";
                        response << "Content-Length: " << error_body.length() << "\r\n";
                        response << "\r\n";
                        response << error_body;
                        return response.str();
                    }
                } else {
                    // Fall back to instance's default action
                    actionToExecute = inst->action;
                }
            }

            if (actionToExecute.empty()) {
                std::string error_body = R"({"error": "No action provided"})";
                response << "HTTP/1.1 400 Bad Request\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Access-Control-Allow-Origin: *\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
                return response.str();
            }

            // Check if we should execute or just return the command
            bool shouldExecute = req.value("execute", true);

            json result;
            if (shouldExecute) {
                // Execute on server (for server-side actions)
                bool success = executeAction(actionToExecute);
                result = {{"success", success}, {"command", actionToExecute}};
            } else {
                // Just return the command (for client-side tools like VNC viewers)
                result = {{"command", actionToExecute}};
            }

            std::string body_str = result.dump(2);
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: " << body_str.length() << "\r\n";
            response << "\r\n";
            response << body_str;
            return response.str();
        } catch (const std::exception& e) {
            std::string error_body = R"({"error": "Invalid request"})";
            response << "HTTP/1.1 400 Bad Request\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: " << error_body.length() << "\r\n";
            response << "\r\n";
            response << error_body;
            return response.str();
        }
    }

    // POST /api/templates - Add template
    if (path == "/api/templates" && method == "POST") {
        try {
            json req = json::parse(body);
            std::string id = req.value("id", "");

            if (id.empty()) {
                std::string error_body = R"({"error": "Template ID required"})";
                response << "HTTP/1.1 400 Bad Request\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
                return response.str();
            }

            auto tmpl = std::make_shared<Template>();
            tmpl->id = id;
            tmpl->label = req.value("label", "");
            tmpl->command = req.value("command", "");

            if (req.contains("resources")) {
                for (const auto& res : req["resources"]) {
                    tmpl->resources.push_back(res.get<std::string>());
                }
            }

            if (req.contains("vars")) {
                for (auto& [key, value] : req["vars"].items()) {
                    tmpl->vars[key] = value.get<std::string>();
                }
            }

            tmpl->action = req.value("action", "");

            g_state->templates[id] = tmpl;
            g_state->save();

            json result = {{"success", true}};
            std::string body_str = result.dump(2);
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Content-Length: " << body_str.length() << "\r\n";
            response << "\r\n";
            response << body_str;
            return response.str();
        } catch (const std::exception& e) {
            std::string error_body = R"({"error": "Invalid request"})";
            response << "HTTP/1.1 400 Bad Request\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Content-Length: " << error_body.length() << "\r\n";
            response << "\r\n";
            response << error_body;
            return response.str();
        }
    }

    // POST /api/resource-types - Add resource type
    if (path == "/api/resource-types" && method == "POST") {
        try {
            json req = json::parse(body);
            std::string name = req.value("name", "");

            if (name.empty()) {
                std::string error_body = R"({"error": "Resource type name required"})";
                response << "HTTP/1.1 400 Bad Request\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
                return response.str();
            }

            auto rt = std::make_shared<ResourceType>();
            rt->name = name;
            rt->check = req.value("check", "");
            rt->counter = req.value("counter", false);
            rt->start = req.value("start", 0);
            rt->end = req.value("end", 0);

            g_state->types[name] = rt;
            g_state->save();

            json result = {{"success", true}};
            std::string body_str = result.dump(2);
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Content-Length: " << body_str.length() << "\r\n";
            response << "\r\n";
            response << body_str;
            return response.str();
        } catch (const std::exception& e) {
            std::string error_body = R"({"error": "Invalid request"})";
            response << "HTTP/1.1 400 Bad Request\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Content-Length: " << error_body.length() << "\r\n";
            response << "\r\n";
            response << error_body;
            return response.str();
        }
    }

    // POST /api/preview-resources - Preview resource allocation for a template
    if (path == "/api/preview-resources" && method == "POST") {
        try {
            json req = json::parse(body);
            std::string templateId = req.value("template", "");
            std::string name = req.value("name", "");

            if (templateId.empty() || name.empty()) {
                std::string error_body = R"({"error": "Template and name required"})";
                response << "HTTP/1.1 400 Bad Request\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
                return response.str();
            }

            if (g_state->templates.find(templateId) == g_state->templates.end()) {
                std::string error_body = R"({"error": "Template not found"})";
                response << "HTTP/1.1 404 Not Found\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
                return response.str();
            }

            const auto& tmpl = *g_state->templates[templateId];
            std::map<std::string, std::string> vars;
            if (req.contains("vars")) {
                for (auto& [key, value] : req["vars"].items()) {
                    vars[key] = value.get<std::string>();
                }
            }

            // Allocate resources (temporarily)
            std::map<std::string, std::string> previewResources;
            for (const auto& rtype : tmpl.resources) {
                try {
                    std::string reqValue = (vars.find(rtype) != vars.end()) ? vars[rtype] : "";
                    std::string value = allocateResource(g_state, rtype, reqValue);
                    previewResources[rtype] = value;
                } catch (const std::exception& e) {
                    std::string error_body = R"({"error": "Resource preview failed: )" + std::string(e.what()) + R"("})";
                    response << "HTTP/1.1 500 Internal Server Error\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                    return response.str();
                }
            }

            // Cache the preview
            {
                std::lock_guard<std::mutex> lock(g_state->previewMutex);
                g_state->resourcePreviews[name] = previewResources;
                g_state->resourcePreviewTimes[name] = time(nullptr);
            }

            // Return the preview
            json result = previewResources;
            std::string body_str = result.dump(2);
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: " << body_str.length() << "\r\n";
            response << "\r\n";
            response << body_str;
            return response.str();
        } catch (const std::exception& e) {
            std::string error_body = R"({"error": ")" + std::string(e.what()) + R"("})";
            response << "HTTP/1.1 500 Internal Server Error\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Content-Length: " << error_body.length() << "\r\n";
            response << "\r\n";
            response << error_body;
            return response.str();
        }
    }

    // POST /api/instances - Start/stop/restart/delete instances
    if (path == "/api/instances" && method == "POST") {
        try {
            json req = json::parse(body);
            std::string action = req.value("action", "");

            if (action == "start") {
                std::string templateId = req.value("template", "");
                std::string name = req.value("name", "");

                if (templateId.empty() || name.empty()) {
                    std::string error_body = R"({"error": "Template and name required"})";
                    response << "HTTP/1.1 400 Bad Request\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                    return response.str();
                }

                if (g_state->templates.find(templateId) == g_state->templates.end()) {
                    std::string error_body = R"({"error": "Template not found"})";
                    response << "HTTP/1.1 404 Not Found\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                    return response.str();
                }

                std::map<std::string, std::string> vars;
                if (req.contains("vars")) {
                    for (auto& [key, value] : req["vars"].items()) {
                        vars[key] = value.get<std::string>();
                    }
                }

                auto inst = startProcess(g_state, *g_state->templates[templateId], name, vars);
                if (inst) {
                    json result = *inst;
                    std::string body_str = result.dump(2);
                    response << "HTTP/1.1 200 OK\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << body_str.length() << "\r\n";
                    response << "\r\n";
                    response << body_str;
                } else {
                    std::string error_body = R"({"error": "Failed to start process"})";
                    response << "HTTP/1.1 500 Internal Server Error\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                }
                return response.str();
            }
            else if (action == "stop" || action == "restart" || action == "delete") {
                // Accept both 'id' and 'instance_id' for the instance identifier
                std::string instanceId = req.value("id", "");
                if (instanceId.empty()) {
                    instanceId = req.value("instance_id", "");
                }

                if (instanceId.empty()) {
                    std::string error_body = R"({"error": "Instance ID required"})";
                    response << "HTTP/1.1 400 Bad Request\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                    return response.str();
                }

                if (g_state->instances.find(instanceId) == g_state->instances.end()) {
                    std::string error_body = R"({"error": "Instance not found"})";
                    response << "HTTP/1.1 404 Not Found\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                    return response.str();
                }

                if (action == "stop") {
                    bool success = stopProcess(g_state, g_state->instances[instanceId]);
                    json result = {{"success", success}};
                    std::string body_str = result.dump(2);
                    response << "HTTP/1.1 200 OK\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << body_str.length() << "\r\n";
                    response << "\r\n";
                    response << body_str;
                    return response.str();
                }
                else if (action == "restart") {
                    bool success = restartProcess(g_state, g_state->instances[instanceId]);
                    json result = {{"success", success}};
                    std::string body_str = result.dump(2);
                    response << "HTTP/1.1 200 OK\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << body_str.length() << "\r\n";
                    response << "\r\n";
                    response << body_str;
                    return response.str();
                }
                else if (action == "delete") {
                    g_state->instances.erase(instanceId);
                    g_state->save();
                    json result = {{"success", true}};
                    std::string body_str = result.dump(2);
                    response << "HTTP/1.1 200 OK\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << body_str.length() << "\r\n";
                    response << "\r\n";
                    response << body_str;
                    return response.str();
                }
            }
        } catch (const std::exception& e) {
            std::string error_body = R"({"error": "Invalid request"})";
            response << "HTTP/1.1 400 Bad Request\r\n";
            response << "Content-Type: application/json\r\n";
            response << "Content-Length: " << error_body.length() << "\r\n";
            response << "\r\n";
            response << error_body;
            return response.str();
        }
    }

    // Default 404
    response << "HTTP/1.1 404 Not Found\r\n";
    response << "Content-Type: text/plain\r\n";
    response << "Content-Length: 9\r\n";
    response << "\r\n";
    response << "Not Found";
    return response.str();
}

void handleClient(int clientSocket) {
    std::string request;
    char buffer[4096];
    size_t contentLength = 0;
    bool headersComplete = false;
    size_t bodyPos = std::string::npos;

    while (true) {
        ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer));
        if (bytesRead <= 0) break;

        request.append(buffer, bytesRead);

        if (!headersComplete) {
            bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                headersComplete = true;
                
                // Parse Content-Length (case-insensitive)
                std::string headers = request.substr(0, bodyPos);
                std::string clHeader = "content-length: ";
                
                auto it = std::search(
                    headers.begin(), headers.end(),
                    clHeader.begin(), clHeader.end(),
                    [](char a, char b) {
                        return std::tolower(a) == std::tolower(b);
                    }
                );

                if (it != headers.end()) {
                    size_t valStart = std::distance(headers.begin(), it) + clHeader.length();
                    size_t valEnd = headers.find("\r\n", valStart);
                    if (valEnd != std::string::npos) {
                        try {
                            contentLength = std::stoul(headers.substr(valStart, valEnd - valStart));
                        } catch (...) {
                            contentLength = 0;
                        }
                    }
                }
            }
        }

        if (headersComplete) {
            if (request.length() >= bodyPos + 4 + contentLength) {
                break;
            }
        }
    }

    if (!request.empty()) {
        std::istringstream iss(request);
        std::string method, path, version;
        iss >> method >> path >> version;

        std::string body;
        if (bodyPos != std::string::npos && request.length() > bodyPos + 4) {
            body = request.substr(bodyPos + 4);
        }

        std::string headersStr = bodyPos != std::string::npos ? request.substr(0, bodyPos) : request;
        std::string response = handleRequest(method, path, body, headersStr);
        
        // Write response in chunks if needed (though write usually handles it)
        size_t totalWritten = 0;
        while (totalWritten < response.length()) {
            ssize_t written = write(clientSocket, response.c_str() + totalWritten, response.length() - totalWritten);
            if (written <= 0) break;
            totalWritten += written;
        }
    }

    close(clientSocket);
}

bool serveHTTP(const std::string& addr, std::shared_ptr<State> state) {
    g_state = state;

    // Parse address (format: ":8080" or "0.0.0.0:8080")
    int port = 8080;
    size_t colonPos = addr.find(':');
    if (colonPos != std::string::npos) {
        port = std::stoi(addr.substr(colonPos + 1));
    }

    // Create socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Failed to create socket\n";
        return false;
    }

    // Set FD_CLOEXEC to prevent child processes from inheriting the socket
    int flags = fcntl(serverSocket, F_GETFD);
    if (flags != -1) {
        fcntl(serverSocket, F_SETFD, flags | FD_CLOEXEC);
    }

    // Set socket options
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind socket
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Failed to bind socket\n";
        close(serverSocket);
        return false;
    }

    // Listen
    if (listen(serverSocket, 10) == -1) {
        std::cerr << "Failed to listen on socket\n";
        close(serverSocket);
        return false;
    }

    std::cout << "HTTP server listening on port " << port << std::endl;

    // Accept connections
    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == -1) {
            continue;
        }

        // Handle client in a new thread
        std::thread(handleClient, clientSocket).detach();
    }

    close(serverSocket);
    return true;
}

} // namespace vp
