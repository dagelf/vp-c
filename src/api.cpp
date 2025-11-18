#include "api.hpp"
#include "process.hpp"
#include "resource.hpp"
#include "types.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <thread>
#include <fstream>

namespace vp {

// Helper to read file contents
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

std::string handleRequest(const std::string& method, const std::string& path, const std::string& body) {
    std::ostringstream response;

    // Handle CORS preflight
    if (method == "OPTIONS") {
        response << "HTTP/1.1 204 No Content\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
        response << "Access-Control-Allow-Headers: Content-Type\r\n";
        response << "\r\n";
        return response.str();
    }

    // Serve web.html from file
    if (path == "/" && method == "GET") {
        std::string html = readFile("web.html");
        if (html.empty()) {
            html = "<html><body><h1>VP Process Manager</h1><p>Error: web.html not found</p></body></html>";
        }
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/html\r\n";
        response << "Content-Length: " << html.length() << "\r\n";
        response << "\r\n";
        response << html;
        return response.str();
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

    // GET /api/config - Get configuration
    if (path == "/api/config" && method == "GET") {
        json config_json;
        config_json["auto_refresh_interval"] = 5000;
        std::string body_str = config_json.dump(2);

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body_str.length() << "\r\n";
        response << "\r\n";
        response << body_str;
        return response.str();
    }

    // GET /api/discover - Discover processes
    if (path.find("/api/discover") == 0 && method == "GET") {
        bool portsOnly = path.find("ports_only=true") != std::string::npos;
        auto discovered = discoverProcesses(g_state, portsOnly);

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
            std::string instanceName = req.value("instance_name", "");

            if (g_state->instances.find(instanceName) == g_state->instances.end()) {
                std::string error_body = R"({"error": "Instance not found"})";
                response << "HTTP/1.1 404 Not Found\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
                return response.str();
            }

            auto inst = g_state->instances[instanceName];
            if (inst->action.empty()) {
                std::string error_body = R"({"error": "No action defined"})";
                response << "HTTP/1.1 400 Bad Request\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << error_body.length() << "\r\n";
                response << "\r\n";
                response << error_body;
                return response.str();
            }

            bool success = executeAction(inst->action);
            json result = {{"success", success}};
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

    // POST /api/instances - Start/stop/restart/delete instances
    if (path == "/api/instances" && method == "POST") {
        try {
            json req = json::parse(body);
            std::string action = req.value("action", "");
            // Accept both 'name' and 'instance_id' for compatibility
            std::string name = req.value("name", "");
            if (name.empty()) {
                name = req.value("instance_id", "");
            }

            if (action == "start") {
                std::string templateId = req.value("template", "");
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
            else if (action == "stop") {
                if (g_state->instances.find(name) == g_state->instances.end()) {
                    std::string error_body = R"({"error": "Instance not found"})";
                    response << "HTTP/1.1 404 Not Found\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                    return response.str();
                }

                bool success = stopProcess(g_state, g_state->instances[name]);
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
                if (g_state->instances.find(name) == g_state->instances.end()) {
                    std::string error_body = R"({"error": "Instance not found"})";
                    response << "HTTP/1.1 404 Not Found\r\n";
                    response << "Content-Type: application/json\r\n";
                    response << "Content-Length: " << error_body.length() << "\r\n";
                    response << "\r\n";
                    response << error_body;
                    return response.str();
                }

                bool success = restartProcess(g_state, g_state->instances[name]);
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
                if (g_state->instances.find(name) != g_state->instances.end()) {
                    g_state->instances.erase(name);
                    g_state->save();
                }
                json result = {{"success", true}};
                std::string body_str = result.dump(2);
                response << "HTTP/1.1 200 OK\r\n";
                response << "Content-Type: application/json\r\n";
                response << "Content-Length: " << body_str.length() << "\r\n";
                response << "\r\n";
                response << body_str;
                return response.str();
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
    char buffer[4096];
    ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1);

    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';

        // Parse HTTP request
        std::string request(buffer);
        std::istringstream iss(request);
        std::string method, path, version;
        iss >> method >> path >> version;

        // Find request body
        std::string body;
        size_t bodyPos = request.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            body = request.substr(bodyPos + 4);
        }

        // Handle request
        std::string response = handleRequest(method, path, body);

        // Send response
        ssize_t written = write(clientSocket, response.c_str(), response.length());
        (void)written; // Suppress unused warning
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
