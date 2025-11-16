#ifndef VP_STATE_HPP
#define VP_STATE_HPP

#include "types.hpp"
#include <mutex>
#include <memory>

namespace vp {

// State holds all application state
class State {
public:
    State();
    ~State();

    // Load state from ~/.vibeprocess/state.json
    static std::shared_ptr<State> load();

    // Save state to ~/.vibeprocess/state.json
    bool save();

    // Resource management
    void claimResource(const std::string& rtype, const std::string& value, const std::string& owner);
    void releaseResources(const std::string& owner);

    // Watch config file for changes and reload automatically
    bool watchConfig();

    // State data
    std::map<std::string, std::shared_ptr<Instance>> instances;
    std::map<std::string, std::shared_ptr<Template>> templates;
    std::map<std::string, std::shared_ptr<Resource>> resources;    // type:value -> Resource
    std::map<std::string, int> counters;                            // counter_name -> current
    std::map<std::string, std::shared_ptr<ResourceType>> types;    // Resource type definitions
    std::map<std::string, bool> remotesAllowed;                    // origin -> allowed

private:
    std::mutex mutex_;
    int inotify_fd_;
    int watch_fd_;

    // Load default templates
    void loadDefaultTemplates();

    // Load default resource types
    void loadDefaultResourceTypes();

    // Serialize to JSON
    std::string toJson() const;

    // Deserialize from JSON
    bool fromJson(const std::string& json);

    // Get state file path
    static std::string getStateFilePath();
    static std::string getStateDir();
};

} // namespace vp

#endif // VP_STATE_HPP
