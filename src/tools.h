#pragma once
#include <string>
#include <vector>
#include "../third_party/json.hpp"

namespace tools {

std::string read_file(const std::string& path);
std::string create_file(const std::string& path);
std::string write_file(const std::string& path, const std::string& content);
std::string edit_file(const std::string& path,
                      const std::string& old_str,
                      const std::string& new_str);
std::string list_dir(const std::string& path);
std::string glob_files(const std::string& pattern, const std::string& dir);
std::string bash(const std::string& command);
std::string ask_user(const std::string& question,
                     const std::vector<std::string>& options);

// Dispatch by name
std::string execute(const std::string& name, const nlohmann::json& args);

// JSON schemas for AI
nlohmann::json get_schemas();

} // namespace tools
