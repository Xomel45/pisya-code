#include "tools.h"
#include "lang.h"
#include "ui.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

// Categories allowed for the whole session (populated by user choice)
static std::unordered_set<std::string> g_session_allowed;

namespace fs = std::filesystem;

// ── project root (CWD at first call) ─────────────────────────────────────────

static const fs::path& project_root() {
    static fs::path root = fs::current_path();
    return root;
}

static bool inside_project(const fs::path& p) {
    auto rel = p.lexically_relative(project_root());
    if (rel.empty()) return true;
    return !rel.string().starts_with("..");
}

// ── word-boundary command presence check ──────────────────────────────────────

// Returns true if 'name' appears as a standalone shell token in cmd.
static bool cmd_present(const std::string& cmd, const std::string& name) {
    for (size_t pos = cmd.find(name); pos != std::string::npos;
         pos = cmd.find(name, pos + 1)) {
        bool before_ok = (pos == 0) ||
            cmd[pos-1] == ' '  || cmd[pos-1] == '\t' ||
            cmd[pos-1] == ';'  || cmd[pos-1] == '|'  ||
            cmd[pos-1] == '&'  || cmd[pos-1] == '\n';
        size_t after = pos + name.size();
        bool after_ok = (after >= cmd.size()) ||
            cmd[after] == ' '  || cmd[after] == '\t' ||
            cmd[after] == ';'  || cmd[after] == '|'  ||
            cmd[after] == '&'  || cmd[after] == '\n' ||
            cmd[after] == '=';  // dd if=...
        if (before_ok && after_ok) return true;
    }
    return false;
}

// ── hard-blocked commands — no confirmation possible ──────────────────────────

static std::string hard_block(const std::string& cmd) {
    auto has = [&](const std::string& name) { return cmd_present(cmd, name); };

    if (has("sudo") || has("su"))
        return "Error: root operations (sudo/su) are permanently blocked.";
    if (has("reboot") || has("poweroff") || has("shutdown"))
        return "Error: system power commands (reboot/poweroff/shutdown) are permanently blocked.";
    if (has("pkill") || has("killall"))
        return "Error: mass process kill (pkill/killall) is permanently blocked.";
    if (has("dd") || has("mkfs") || has("fdisk") || has("parted"))
        return "Error: low-level disk operations (dd/mkfs/fdisk/parted) are permanently blocked.";
    return "";
}

// ── path safety guard ─────────────────────────────────────────────────────────

// Returns non-empty error string if the path is blocked or user denies access.
static std::string path_guard(const std::string& raw) {
    fs::path p   = fs::weakly_canonical(raw);
    std::string abs = p.string();

    // /root is always blocked
    if (abs == "/root" || abs.starts_with("/root/"))
        return "Error: access to /root is permanently blocked.";

    // System directories — hard blocked
    static constexpr std::string_view SYSTEM_DIRS[] = {
        "/etc", "/usr", "/bin", "/sbin", "/lib", "/lib64", "/lib32",
        "/boot", "/sys", "/proc", "/dev", "/run", "/snap",
    };
    for (auto dir : SYSTEM_DIRS) {
        std::string d(dir);
        if (abs == d || abs.starts_with(d + "/"))
            return std::format("Error: '{}' is inside a protected system directory — permanently blocked.", abs);
    }

    // Outside project — confirm each time (no session-allow)
    if (!inside_project(p)) {
        const auto& L = lang::S();
        std::string choice = ui::select(
            std::format("{}: {}", L.outside_project_prompt, abs),
            {L.perm_allow, L.perm_deny});
        if (choice != L.perm_allow)
            return std::format("Error: access to '{}' denied by user.", abs);
    }

    return "";
}

// ── read_file ─────────────────────────────────────────────────────────────────
std::string tools::read_file(const std::string& path) {
    if (auto err = path_guard(path); !err.empty()) return err;

    if (!fs::exists(path))
        return std::format("Error: file '{}' not found", path);

    std::ifstream f(path);
    if (!f) return std::format("Error: cannot open '{}'", path);

    // Add line numbers (like Claude Code does)
    std::string result;
    std::string line;
    int n = 1;
    while (std::getline(f, line))
        result += std::format("{:>4}\t{}\n", n++, line);

    return result.empty() ? "(empty file)" : result;
}

// ── write_file ────────────────────────────────────────────────────────────────
std::string tools::write_file(const std::string& path, const std::string& content) {
    if (auto err = path_guard(path); !err.empty()) return err;

    if (fs::exists(path)) {
        const auto& L = lang::S();
        std::string choice = ui::select(
            std::format("{}: {}", L.overwrite_prompt, path),
            {L.perm_allow, L.perm_deny});
        if (choice != L.perm_allow) return "Write cancelled by user.";
    }

    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return std::format("Error: cannot write '{}'", path);
    f << content;
    return std::format("Written {} bytes to '{}'", content.size(), path);
}

// ── edit_file ─────────────────────────────────────────────────────────────────
std::string tools::edit_file(const std::string& path,
                              const std::string& old_str,
                              const std::string& new_str) {
    if (auto err = path_guard(path); !err.empty()) return err;

    if (!fs::exists(path))
        return std::format("Error: file '{}' not found", path);

    std::ifstream in(path);
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();

    auto pos = content.find(old_str);
    if (pos == std::string::npos)
        return "Error: old_string not found in file (must be unique and exact)";
    if (content.find(old_str, pos + 1) != std::string::npos)
        return "Error: old_string appears more than once — make it more specific";

    content.replace(pos, old_str.size(), new_str);

    std::ofstream out(path);
    out << content;
    return std::format("Edited '{}' successfully", path);
}

// ── list_dir ──────────────────────────────────────────────────────────────────
std::string tools::list_dir(const std::string& path) {
    if (auto err = path_guard(path); !err.empty()) return err;

    if (!fs::exists(path))
        return std::format("Error: path '{}' not found", path);

    std::string result;
    for (const auto& entry : fs::directory_iterator(path)) {
        std::string name = entry.path().filename().string();
        if (entry.is_directory()) name += "/";
        result += name + "\n";
    }
    return result.empty() ? "(empty directory)" : result;
}

// ── glob_files ────────────────────────────────────────────────────────────────
std::string tools::glob_files(const std::string& pattern, const std::string& dir) {
    if (pattern.empty()) return "Error: pattern is empty";
    if (auto err = path_guard(dir); !err.empty()) return err;
    // Simple recursive search matching files by extension or name substring
    std::string result;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied)) {
            std::string p = entry.path().string();
            // Naive glob: support "*.ext" and "*name*"
            if (pattern.front() == '*' && pattern.back() == '*') {
                std::string mid = pattern.substr(1, pattern.size() - 2);
                if (p.find(mid) != std::string::npos) result += p + "\n";
            } else if (pattern.front() == '*') {
                std::string ext = pattern.substr(1);
                if (p.size() >= ext.size() &&
                    p.substr(p.size() - ext.size()) == ext)
                    result += p + "\n";
            } else {
                if (p.find(pattern) != std::string::npos) result += p + "\n";
            }
        }
    } catch (...) {}
    return result.empty() ? "No files found" : result;
}

// ── bash ──────────────────────────────────────────────────────────────────────
static std::string run_command(const std::string& command) {
    std::array<char, 256> buf{};
    std::string output;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) return "Error: popen failed";
    while (fgets(buf.data(), buf.size(), pipe))
        output += buf.data();
    int ret = pclose(pipe);
    if (output.empty()) output = "(no output)";
    output += std::format("\n[exit code: {}]", ret);
    return output;
}

std::string tools::bash(const std::string& command) {
    // Hard-blocked — no confirmation possible, ever
    if (auto block = hard_block(command); !block.empty()) return block;

    std::string category = ui::danger_category(command);

    if (category.empty()) {
        // Non-dangerous — simple confirm
        const auto& L = lang::S();
        std::string choice = ui::select(
            std::format("{}: {}", L.run_cmd, command),
            {L.perm_allow, L.perm_deny});
        if (choice != L.perm_allow) return "Command cancelled by user.";
    } else {
        // Dangerous — check session allowlist first
        if (!g_session_allowed.count(category)) {
            auto perm = ui::ask_permission(command, category);
            if (perm == ui::Perm::Deny)          return "Command cancelled by user.";
            if (perm == ui::Perm::AllowSession)  g_session_allowed.insert(category);
            // Perm::Allow or AllowSession → proceed
        }
    }

    return run_command(command);
}

// ── ask_user ──────────────────────────────────────────────────────────────────
std::string tools::ask_user(const std::string& question,
                             const std::vector<std::string>& options) {
    std::string answer = ui::select("Q: " + question, options);
    if (answer.empty()) answer = "(нет ответа)";
    return std::format("Q: {}\nA: {}", question, answer);
}

// ── execute ───────────────────────────────────────────────────────────────────
std::string tools::execute(const std::string& name, const nlohmann::json& args) {
    try {
        if (name == "read_file")
            return read_file(args.at("path").get<std::string>());

        if (name == "write_file")
            return write_file(args.at("path").get<std::string>(),
                              args.at("content").get<std::string>());

        if (name == "edit_file")
            return edit_file(args.at("path").get<std::string>(),
                             args.at("old_string").get<std::string>(),
                             args.at("new_string").get<std::string>());

        if (name == "list_dir")
            return list_dir(args.value("path", "."));

        if (name == "glob_files")
            return glob_files(args.at("pattern").get<std::string>(),
                              args.value("dir", "."));

        if (name == "bash")
            return bash(args.at("command").get<std::string>());

        if (name == "ask_user") {
            std::string question = args.at("question").get<std::string>();
            std::vector<std::string> opts;
            if (args.contains("options") && args["options"].is_array())
                for (const auto& o : args["options"])
                    opts.push_back(o.get<std::string>());
            return ask_user(question, opts);
        }

        return std::format("Error: unknown tool '{}'", name);

    } catch (const std::exception& e) {
        return std::format("Error executing '{}': {}", name, e.what());
    }
}

// ── get_schemas ───────────────────────────────────────────────────────────────
nlohmann::json tools::get_schemas() {
    using json = nlohmann::json;
    return json::array({
        {{"type", "function"}, {"function", {
            {"name", "read_file"},
            {"description", "Read the contents of a file with line numbers."},
            {"parameters", {
                {"type", "object"},
                {"properties", {{"path", {{"type","string"},{"description","File path"}}}}},
                {"required", json::array({"path"})}
            }}
        }}},
        {{"type", "function"}, {"function", {
            {"name", "write_file"},
            {"description", "Write (create or overwrite) a file with the given content."},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"path",    {{"type","string"},{"description","File path"}}},
                    {"content", {{"type","string"},{"description","File content"}}}
                }},
                {"required", json::array({"path","content"})}
            }}
        }}},
        {{"type", "function"}, {"function", {
            {"name", "edit_file"},
            {"description", "Replace old_string with new_string in a file. old_string must be unique and exact."},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"path",       {{"type","string"},{"description","File path"}}},
                    {"old_string", {{"type","string"},{"description","Exact text to replace"}}},
                    {"new_string", {{"type","string"},{"description","Replacement text"}}}
                }},
                {"required", json::array({"path","old_string","new_string"})}
            }}
        }}},
        {{"type", "function"}, {"function", {
            {"name", "list_dir"},
            {"description", "List files and directories in a path."},
            {"parameters", {
                {"type", "object"},
                {"properties", {{"path", {{"type","string"},{"description","Directory path, default '.'"}}}}}
            }}
        }}},
        {{"type", "function"}, {"function", {
            {"name", "glob_files"},
            {"description", "Find files recursively by pattern (e.g. '*.cpp', '*test*')."},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"pattern", {{"type","string"},{"description","Glob pattern"}}},
                    {"dir",     {{"type","string"},{"description","Root directory, default '.'"}}}
                }},
                {"required", json::array({"pattern"})}
            }}
        }}},
        {{"type", "function"}, {"function", {
            {"name", "bash"},
            {"description", "Execute a shell command (requires user confirmation)."},
            {"parameters", {
                {"type", "object"},
                {"properties", {{"command", {{"type","string"},{"description","Shell command to run"}}}}},
                {"required", json::array({"command"})}
            }}
        }}},
        {{"type", "function"}, {"function", {
            {"name", "ask_user"},
            {"description", "Ask the user a question with selectable options. Use when you need the user to make a choice or provide input. The user can also type a custom answer."},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"question", {{"type","string"},{"description","The question to ask"}}},
                    {"options",  {{"type","array"}, {"items",{{"type","string"}}},
                                  {"description","Suggested answer options (e.g. [\"Да\",\"Нет\"])"}}}
                }},
                {"required", json::array({"question"})}
            }}
        }}}
    });
}
