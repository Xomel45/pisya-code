#pragma once
#include <string>

struct Config {
    std::string host         = "127.0.0.1";
    int         port         = 11434;
    std::string model        = "qwen2.5-coder:14b";
    std::string system_prompt =
        "You are Pisya Code, an AI coding assistant. "
        "You help users with programming tasks by reading and editing files directly. "
        "Always use tools to inspect files before modifying them. "
        "Be concise and precise.";

    static Config load();
    static std::string config_path();
    void print() const;
};
