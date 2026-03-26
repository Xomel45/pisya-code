#pragma once
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
    AIClient(const std::string& host, int port, const std::string& model);

    // Returns full response JSON from the model
    nlohmann::json chat(const std::vector<Message>& messages,
                        const nlohmann::json& tools);

private:
    std::string host_;
    int         port_;
    std::string model_;

    nlohmann::json build_request(const std::vector<Message>& messages,
                                 const nlohmann::json& tools) const;
};
