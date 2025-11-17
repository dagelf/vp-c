#include "api.hpp"
#include "process.hpp"
#include "types.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <thread>

namespace vp {

// Embedded web HTML (simplified - in production, embed actual web.html)
const char* WEB_HTML = R"(
<!DOCTYPE html>
<html>
<head>
    <title>VP Process Manager</title>
    <style>
        body { font-family: sans-serif; margin: 20px; }
        h1 { color: #333; }
        .status { margin: 20px 0; }
    </style>
</head>
<body>
    <h1>VP Process Manager</h1>
    <div class="status">
        <p>Web UI placeholder - C++ conversion</p>
        <p>API endpoints available at /api/*</p>
    </div>
</body>
</html>
)";

static std::shared_ptr<State> g_state;

std::string handleRequest(const std::string& method, const std::string& path, const std::string& /*body*/) {
    std::ostringstream response;

    if (path == "/" && method == "GET") {
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: text/html\r\n";
        response << "Content-Length: " << strlen(WEB_HTML) << "\r\n";
        response << "\r\n";
        response << WEB_HTML;
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
        std::string body = templates_json.dump(2);

        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "\r\n";
        response << body;
        return response.str();
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
