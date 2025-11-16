#ifndef VP_JSON_SIMPLE_HPP
#define VP_JSON_SIMPLE_HPP

// Simplified JSON handling - in a real implementation, use nlohmann/json or similar
// This is a minimal implementation to avoid external dependencies

#include <string>
#include <sstream>
#include <map>
#include <vector>

namespace vp {
namespace json {

inline std::string escape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

inline std::string quote(const std::string& s) {
    return "\"" + escape(s) + "\"";
}

inline std::string toJson(const std::map<std::string, std::string>& m) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& kv : m) {
        if (!first) oss << ",";
        oss << quote(kv.first) << ":" << quote(kv.second);
        first = false;
    }
    oss << "}";
    return oss.str();
}

inline std::string toJson(const std::map<std::string, bool>& m) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& kv : m) {
        if (!first) oss << ",";
        oss << quote(kv.first) << ":" << (kv.second ? "true" : "false");
        first = false;
    }
    oss << "}";
    return oss.str();
}

inline std::string toJson(const std::map<std::string, int>& m) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& kv : m) {
        if (!first) oss << ",";
        oss << quote(kv.first) << ":" << kv.second;
        first = false;
    }
    oss << "}";
    return oss.str();
}

inline std::string toJson(const std::vector<std::string>& v) {
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& s : v) {
        if (!first) oss << ",";
        oss << quote(s);
        first = false;
    }
    oss << "]";
    return oss.str();
}

// Note: For a production system, use a proper JSON library like nlohmann/json
// This is a minimal implementation for demonstration

} // namespace json
} // namespace vp

#endif // VP_JSON_SIMPLE_HPP
