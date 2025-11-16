#ifndef VP_API_HPP
#define VP_API_HPP

#include "state.hpp"
#include <memory>
#include <string>

namespace vp {

// Embedded web HTML content
extern const char* WEB_HTML;

// Start HTTP server
bool serveHTTP(const std::string& addr, std::shared_ptr<State> state);

} // namespace vp

#endif // VP_API_HPP
