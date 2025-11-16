#ifndef VP_RESOURCE_HPP
#define VP_RESOURCE_HPP

#include "types.hpp"
#include "state.hpp"
#include <string>
#include <memory>

namespace vp {

// Get default resource types
std::map<std::string, std::shared_ptr<ResourceType>> defaultResourceTypes();

// Allocate a resource of the given type
std::string allocateResource(std::shared_ptr<State> state, const std::string& rtype, const std::string& requestedValue);

// Check if a resource is available using the check command
bool checkResource(const ResourceType& rt, const std::string& value);

} // namespace vp

#endif // VP_RESOURCE_HPP
