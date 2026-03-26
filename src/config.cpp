#include "config.h"
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

std::string Config::config_path() {
    const char* home = getenv("HOME");
    return std::string(home ? home : ".") + "/.pisya/config";
}

static void trim(std::string& s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    auto pos = s.find_last_not_of(" \t\r\n");
    if (pos != std::string::npos) s.erase(pos + 1);
}

Config Config::load() {
    Config cfg;
    std::string path = config_path();

    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream out(path);
        out << "host = 127.0.0.1\n"
            << "port = 11434\n"
            << "model = qwen2.5-coder:14b\n";
        std::cout << std::format("Created default config at {}\n", path);
        return cfg;
    }

    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        trim(key);
        trim(val);

        if      (key == "host")          cfg.host   = val;
        else if (key == "port")          cfg.port   = std::stoi(val);
        else if (key == "model")         cfg.model  = val;
        else if (key == "lang")          cfg.lang   = val;
        else if (key == "system_prompt") cfg.system_prompt = val;
    }
    return cfg;
}

void Config::save() const {
    std::ofstream out(config_path());
    out << "host  = " << host  << "\n"
        << "port  = " << port  << "\n"
        << "model = " << model << "\n"
        << "lang  = " << lang  << "\n";
}

void Config::print(const std::string& model_extra) const {
    std::cout << std::format("  host  : {}:{}\n", host, port);
    if (model_extra.empty())
        std::cout << std::format("  model : {}\n", model);
    else
        std::cout << std::format("  model : {}  ·  {}\n", model, model_extra);
}
