#pragma once
#include "config.h"
#include <optional>
#include <string>
#include <vector>
#include "../third_party/json.hpp"

struct Message {
    std::string role;          // "system" | "user" | "assistant" | "tool"
    std::string content;
    std::string tool_call_id;  // for role=="tool"
    nlohmann::json tool_calls; // for role=="assistant" with tool calls
};

class AIClient {
public:
    struct ModelInfo {
        std::string param_size;   // e.g. "14.8B"
        std::string quantization; // e.g. "Q4_K_M"
    };

    // Picks local (host:port, Ollama) or API mode (cfg.api_url + Bearer cfg.api_key)
    // based on whether cfg.api_url is set.
    explicit AIClient(const Config& cfg);

    // Query /api/show for model details (Ollama only). Returns nullopt on failure
    // or when running in API mode.
    std::optional<ModelInfo> fetch_model_info() const;

    // Returns full response JSON from the model
    nlohmann::json chat(const std::vector<Message>& messages,
                        const nlohmann::json& tools);

private:
    bool        api_mode_;
    std::string host_;        // display host (local: cfg.host, api: parsed from api_url)
    int         port_;        // display port
    std::string model_;
    std::string origin_;      // api mode: scheme://host[:port]
    std::string path_prefix_; // api mode: e.g. "/v1" (no trailing slash)
    std::string api_key_;

    nlohmann::json build_request(const std::vector<Message>& messages,
                                 const nlohmann::json& tools) const;
};
