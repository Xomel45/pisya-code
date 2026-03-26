#pragma once
#include "ai_client.h"
#include "config.h"
#include <vector>
#include <string>

class Agent {
public:
    explicit Agent(const Config& config);
    Agent(const Config& config, std::vector<Message> history); // resume session

    void run(const std::string& user_message);
    void clear_history();

    const std::vector<Message>& get_history() const { return history_; }

private:
    Config             config_;
    AIClient           client_;
    std::vector<Message> history_;

    static constexpr int MAX_ITERATIONS = 30;

    void handle_tool_calls(const nlohmann::json& tool_calls);
    void print_commentary(const std::string& text);
    void print_file_success(const std::string& label, const std::string& path, const std::string& detail);
    void print_failure(const std::string& msg);
    void print_info_tool(const std::string& label, const std::string& path);
    void print_tool_output(const std::string& name, const nlohmann::json& args, const std::string& result);
};
