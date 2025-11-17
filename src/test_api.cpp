#include "test.hpp"
#include "api.hpp"
#include "state.hpp"
#include "types.hpp"
#include <sstream>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

using namespace vp;

// Force linker to include this file
extern "C" void register_api_tests() {}

// Helper function to parse HTTP response
struct HttpResponse {
    int statusCode;
    std::map<std::string, std::string> headers;
    std::string body;
};

HttpResponse parseHttpResponse(const std::string& response) {
    HttpResponse result;
    std::istringstream iss(response);
    std::string line;

    // Parse status line
    std::getline(iss, line);
    if (line.find("HTTP/1.1") != std::string::npos) {
        size_t codeStart = line.find(' ') + 1;
        size_t codeEnd = line.find(' ', codeStart);
        result.statusCode = std::stoi(line.substr(codeStart, codeEnd - codeStart));
    }

    // Parse headers
    while (std::getline(iss, line) && line != "\r") {
        if (line.back() == '\r') line.pop_back();
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 2);
            result.headers[key] = value;
        }
    }

    // Parse body
    std::string bodyPart;
    while (std::getline(iss, bodyPart)) {
        result.body += bodyPart;
    }

    return result;
}

// Helper function to make HTTP request
std::string makeHttpRequest(const std::string& host, int port, const std::string& method,
                           const std::string& path, const std::string& body = "") {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        throw std::runtime_error("Failed to create socket");
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        close(sock);
        throw std::runtime_error("Failed to connect to server");
    }

    // Build request
    std::ostringstream request;
    request << method << " " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << ":" << port << "\r\n";
    if (!body.empty()) {
        request << "Content-Length: " << body.length() << "\r\n";
    }
    request << "Connection: close\r\n";
    request << "\r\n";
    if (!body.empty()) {
        request << body;
    }

    std::string requestStr = request.str();
    ssize_t sent = write(sock, requestStr.c_str(), requestStr.length());
    if (sent == -1) {
        close(sock);
        throw std::runtime_error("Failed to send request");
    }

    // Read response
    std::string response;
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(sock, buffer, sizeof(buffer))) > 0) {
        response.append(buffer, bytesRead);
    }

    close(sock);
    return response;
}

TEST(API_RootEndpoint_ReturnsHTML) {
    auto state = State::load();

    // Start server in background thread
    int port = 18080;
    std::thread serverThread([state, port]() {
        serveHTTP(":" + std::to_string(port), state);
    });
    serverThread.detach();

    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Make request
    std::string response = makeHttpRequest("127.0.0.1", port, "GET", "/");
    HttpResponse parsed = parseHttpResponse(response);

    ASSERT_EQUALS(200, parsed.statusCode, "Root endpoint should return 200");
    ASSERT_CONTAINS(parsed.body, "VP Process Manager", "Root endpoint should return HTML with title");
    ASSERT_CONTAINS(parsed.headers["Content-Type"], "text/html", "Root endpoint should return HTML content type");
}

TEST(API_InstancesEndpoint_ReturnsJSON) {
    auto state = State::load();

    // Start server in background thread
    int port = 18081;
    std::thread serverThread([state, port]() {
        serveHTTP(":" + std::to_string(port), state);
    });
    serverThread.detach();

    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Make request
    std::string response = makeHttpRequest("127.0.0.1", port, "GET", "/api/instances");
    HttpResponse parsed = parseHttpResponse(response);

    ASSERT_EQUALS(200, parsed.statusCode, "Instances endpoint should return 200");
    ASSERT_CONTAINS(parsed.headers["Content-Type"], "application/json", "Instances endpoint should return JSON content type");
    ASSERT_NOT_EMPTY(parsed.body, "Instances endpoint should return non-empty body");
}

TEST(API_TemplatesEndpoint_ReturnsJSON) {
    auto state = State::load();

    // Start server in background thread
    int port = 18082;
    std::thread serverThread([state, port]() {
        serveHTTP(":" + std::to_string(port), state);
    });
    serverThread.detach();

    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Make request
    std::string response = makeHttpRequest("127.0.0.1", port, "GET", "/api/templates");
    HttpResponse parsed = parseHttpResponse(response);

    ASSERT_EQUALS(200, parsed.statusCode, "Templates endpoint should return 200");
    ASSERT_CONTAINS(parsed.headers["Content-Type"], "application/json", "Templates endpoint should return JSON content type");
    ASSERT_NOT_EMPTY(parsed.body, "Templates endpoint should return non-empty body");
}

TEST(API_NotFoundEndpoint_Returns404) {
    auto state = State::load();

    // Start server in background thread
    int port = 18083;
    std::thread serverThread([state, port]() {
        serveHTTP(":" + std::to_string(port), state);
    });
    serverThread.detach();

    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Make request
    std::string response = makeHttpRequest("127.0.0.1", port, "GET", "/api/nonexistent");
    HttpResponse parsed = parseHttpResponse(response);

    ASSERT_EQUALS(404, parsed.statusCode, "Nonexistent endpoint should return 404");
    ASSERT_CONTAINS(parsed.body, "Not Found", "404 response should contain 'Not Found'");
}

TEST(API_CORS_HeadersPresent) {
    auto state = State::load();

    // Start server in background thread
    int port = 18084;
    std::thread serverThread([state, port]() {
        serveHTTP(":" + std::to_string(port), state);
    });
    serverThread.detach();

    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Make request
    std::string response = makeHttpRequest("127.0.0.1", port, "GET", "/api/instances");
    HttpResponse parsed = parseHttpResponse(response);

    ASSERT_TRUE(parsed.headers.count("Access-Control-Allow-Origin") > 0,
                "API endpoints should include CORS headers");
    ASSERT_EQUALS(std::string("*"), parsed.headers["Access-Control-Allow-Origin"],
                  "CORS header should allow all origins");
}
