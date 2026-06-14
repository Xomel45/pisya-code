#include "tools.h"
#include "lang.h"
#include "ui.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
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

// ── permissions.json (Federal Law layer) ─────────────────────────────────────

struct Perms {
    std::unordered_set<std::string> allowed; // run without confirmation
    std::unordered_set<std::string> denied;  // always blocked
};

// Returns first shell token of a command (the actual program name).
static std::string first_cmd_token(const std::string& cmd) {
    size_t i = cmd.find_first_not_of(" \t");
    if (i == std::string::npos) return "";
    size_t j = cmd.find_first_of(" \t;|&\n", i);
    if (j == std::string::npos) return cmd.substr(i);
    return cmd.substr(i, j - i);
}

// Returns path to ~/.pisya/permissions.json, creating the file with defaults if absent.
static fs::path perms_path() {
    const char* home = getenv("HOME");
    fs::path dir = fs::path(home ? home : ".") / ".pisya";
    fs::path p   = dir / "permissions.json";

    if (!fs::exists(p)) {
        fs::create_directories(dir);
        std::ofstream f(p);
        f << R"({
  "_note": "Глобальные расширения разрешений pisya-code (~/. pisya/permissions.json). Применяются ко всем проектам и сессиям. Жёсткие блокировки в коде всегда в приоритете — этот файл не может разрешить: sudo/su, reboot/shutdown, dd/mkfs/fdisk, массовый kill, /root и системные директории, rm -f вне проекта без подтверждения, wget/curl без подтверждения.",
  "allowed": [
    "git",
    "ls",
    "cat",
    "echo",
    "pwd",
    "which",
    "make",
    "cmake",
    "ninja",
    "clang++",
    "g++"
  ],
  "denied": []
}
)";
    }
    return p;
}

// Loads ~/.pisya/permissions.json on every call (reflects manual edits mid-session).
static Perms load_perms() {
    Perms p;
    std::ifstream f(perms_path());
    if (!f) return p;
    try {
        auto j = nlohmann::json::parse(f);
        if (j.contains("allowed") && j["allowed"].is_array())
            for (const auto& a : j["allowed"])
                p.allowed.insert(a.get<std::string>());
        if (j.contains("denied") && j["denied"].is_array())
            for (const auto& d : j["denied"])
                p.denied.insert(d.get<std::string>());
    } catch (...) {}
    return p;
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

// ── .pisyaignore ──────────────────────────────────────────────────────────────
// Gitignore-style patterns (project root) for files that should never be
// read by the model or exposed via list_dir/glob_files/search_files.

struct IgnoreRule {
    std::regex re;
    bool       negate;
};

// Translates one .gitignore-style pattern into a regex matching paths
// relative to the project root (forward-slash separated, no leading '/').
static IgnoreRule compile_ignore_pattern(std::string pat, bool negate) {
    bool anchored = !pat.empty() && pat.front() == '/';
    if (anchored) pat.erase(0, 1);
    if (!pat.empty() && pat.back() == '/') pat.pop_back(); // dir-only marker

    std::string re_str = anchored ? "^" : "^(?:.*/)?";
    for (size_t i = 0; i < pat.size(); ++i) {
        char c = pat[i];
        if (c == '*' && i + 1 < pat.size() && pat[i+1] == '*') {
            ++i;
            if (i + 1 < pat.size() && pat[i+1] == '/') { re_str += "(?:.*/)?"; ++i; }
            else                                        re_str += ".*";
        } else if (c == '*') {
            re_str += "[^/]*";
        } else if (c == '?') {
            re_str += "[^/]";
        } else if (std::string(".^$+(){}|\\[]").find(c) != std::string::npos) {
            re_str += '\\';
            re_str += c;
        } else {
            re_str += c;
        }
    }
    re_str += "(?:/.*)?$";
    return {std::regex(re_str), negate};
}

static std::vector<IgnoreRule> load_ignore_rules() {
    std::vector<IgnoreRule> rules;
    std::ifstream f(project_root() / ".pisyaignore");
    if (!f) return rules;

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        bool negate = false;
        if (line[0] == '!') { negate = true; line.erase(0, 1); }
        if (line.empty()) continue;

        try { rules.push_back(compile_ignore_pattern(line, negate)); }
        catch (...) {}
    }
    return rules;
}

// True if `p` matches a .pisyaignore rule (last matching rule wins, like .gitignore).
static bool is_pisya_ignored(const fs::path& p, const std::vector<IgnoreRule>& rules) {
    if (rules.empty()) return false;

    fs::path rel = fs::weakly_canonical(p).lexically_relative(project_root());
    if (rel.empty() || rel.string().starts_with("..")) return false;

    std::string rp = rel.generic_string();
    bool ignored = false;
    for (const auto& r : rules)
        if (std::regex_match(rp, r.re)) ignored = !r.negate;
    return ignored;
}

// ── read_file ─────────────────────────────────────────────────────────────────
std::string tools::read_file(const std::string& path) {
    if (auto err = path_guard(path); !err.empty()) return err;

    if (is_pisya_ignored(path, load_ignore_rules()))
        return std::format("Error: '{}' is excluded by .pisyaignore", path);

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

// ── create_file ───────────────────────────────────────────────────────────────
std::string tools::create_file(const std::string& path) {
    if (auto err = path_guard(path); !err.empty()) return err;

    if (fs::exists(path))
        return std::format("File '{}' already exists.", path);

    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return std::format("Error: cannot create '{}'", path);
    return std::format("Created '{}'", path);
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

// ── ignored directories (build artifacts, VCS, caches) ───────────────────────

static bool is_ignored_dir(const std::string& name) {
    static const std::unordered_set<std::string> ignored = {
        ".git", ".idea", ".cache", ".vscode", "node_modules",
        "build", "build-asan",
    };
    return ignored.count(name) > 0 || name.starts_with("cmake-build-");
}

// ── naive glob matching ───────────────────────────────────────────────────────

// Supports "*.ext" (suffix), "*name*" (substring) and plain substring patterns.
static bool naive_glob_match(const std::string& pattern, const std::string& path) {
    if (pattern.size() >= 2 && pattern.front() == '*' && pattern.back() == '*') {
        std::string mid = pattern.substr(1, pattern.size() - 2);
        return path.contains(mid);
    }
    if (!pattern.empty() && pattern.front() == '*') {
        std::string ext = pattern.substr(1);
        return path.size() >= ext.size() &&
               path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
    }
    return path.contains(pattern);
}

// ── list_dir ──────────────────────────────────────────────────────────────────
std::string tools::list_dir(const std::string& path) {
    if (auto err = path_guard(path); !err.empty()) return err;

    if (!fs::exists(path))
        return std::format("Error: path '{}' not found", path);

    auto rules = load_ignore_rules();
    std::string result;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (is_pisya_ignored(entry.path(), rules)) continue;
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

    auto rules = load_ignore_rules();
    std::string result;
    try {
        for (auto it = fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator(); ++it) {
            const auto& entry = *it;
            if (entry.is_directory()) {
                if (is_ignored_dir(entry.path().filename().string()) ||
                    is_pisya_ignored(entry.path(), rules))
                    it.disable_recursion_pending();
                continue;
            }
            if (is_pisya_ignored(entry.path(), rules)) continue;
            std::string p = entry.path().string();
            if (naive_glob_match(pattern, p)) result += p + "\n";
        }
    } catch (...) {}
    return result.empty() ? "No files found" : result;
}

// ── search_files ──────────────────────────────────────────────────────────────
std::string tools::search_files(const std::string& pattern, const std::string& dir,
                                  const std::string& file_glob) {
    if (pattern.empty()) return "Error: pattern is empty";
    if (auto err = path_guard(dir); !err.empty()) return err;

    std::regex re;
    try {
        re = std::regex(pattern);
    } catch (const std::regex_error& e) {
        return std::format("Error: invalid regex '{}': {}", pattern, e.what());
    }

    constexpr int MAX_MATCHES = 200;
    int matches = 0;
    std::string result;

    auto rules = load_ignore_rules();
    try {
        for (auto it = fs::recursive_directory_iterator(
                 dir, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator(); ++it) {
            const auto& entry = *it;
            if (entry.is_directory()) {
                if (is_ignored_dir(entry.path().filename().string()) ||
                    is_pisya_ignored(entry.path(), rules))
                    it.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file()) continue;
            if (is_pisya_ignored(entry.path(), rules)) continue;

            std::string p = entry.path().string();
            if (!file_glob.empty() && !naive_glob_match(file_glob, p)) continue;

            std::ifstream f(entry.path(), std::ios::binary);
            std::string line;
            int lineno = 0;
            while (std::getline(f, line)) {
                ++lineno;
                if (line.find('\0') != std::string::npos) break; // skip binary files

                if (std::regex_search(line, re)) {
                    result += std::format("{}:{}: {}\n", p, lineno, line);
                    if (++matches >= MAX_MATCHES) {
                        result += std::format("… truncated at {} matches\n", MAX_MATCHES);
                        return result;
                    }
                }
            }
        }
    } catch (...) {}

    return result.empty() ? "No matches found" : result;
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
    // ── Constitution layer 1: hard-blocked forever, no override ──────────────
    if (auto block = hard_block(command); !block.empty()) return block;

    // ── Constitution layer 2: always-ask rules (no session bypass, ever) ─────
    // wget/curl — network downloads must be confirmed per request
    std::string category = ui::danger_category(command);
    bool is_network = (category == "скачивание из сети");

    // rm -f / rm -rf — force-delete always requires confirmation, especially outside project
    bool is_rm_force = cmd_present(command, "rm") &&
                       (command.find("-f") != std::string::npos  ||
                        command.find("-rf") != std::string::npos ||
                        command.find("-fr") != std::string::npos);

    bool always_ask = is_network || is_rm_force;

    // ── Federal Law layer: permissions.json ───────────────────────────────────
    // (only applies when Constitution doesn't require always_ask)
    std::string token = first_cmd_token(command);
    Perms perms = load_perms();

    if (perms.denied.count(token))
        return std::format("Error: '{}' is blocked by permissions.json.", token);

    if (!always_ask && perms.allowed.count(token))
        return run_command(command); // auto-allowed, no confirmation needed

    // ── Normal permission flow ────────────────────────────────────────────────
    if (always_ask) {
        // Per-request: always ask, no "allow for session" option
        std::string cat = category.empty()
            ? (is_rm_force ? "принудительное удаление" : "команда")
            : category;
        auto perm = ui::ask_permission(command, cat, /*offer_session=*/false);
        if (perm == ui::Perm::Deny) return "Command cancelled by user.";

    } else if (category.empty()) {
        // Non-dangerous — simple confirm
        const auto& L = lang::S();
        std::string choice = ui::select(
            std::format("{}: {}", L.run_cmd, command),
            {L.perm_allow, L.perm_deny});
        if (choice != L.perm_allow) return "Command cancelled by user.";

    } else {
        // Dangerous — check session allowlist
        if (!g_session_allowed.count(category)) {
            auto perm = ui::ask_permission(command, category);
            if (perm == ui::Perm::Deny)         return "Command cancelled by user.";
            if (perm == ui::Perm::AllowSession) g_session_allowed.insert(category);
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

        if (name == "create_file")
            return create_file(args.at("path").get<std::string>());

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

        if (name == "search_files")
            return search_files(args.at("pattern").get<std::string>(),
                                args.value("dir", "."),
                                args.value("file_glob", ""));

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
            {"name", "create_file"},
            {"description", "Create a new empty file at the given path. Fails if the file already exists. Use write_file if you want to write content."},
            {"parameters", {
                {"type", "object"},
                {"properties", {{"path", {{"type","string"},{"description","File path (from project root)"}}}}},
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
            {"name", "search_files"},
            {"description", "Search file contents recursively for a regex pattern (ECMAScript syntax). Returns matches as 'path:line: content'. Use this to find code by content instead of reading whole files."},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"pattern",   {{"type","string"},{"description","Regex pattern to search for"}}},
                    {"dir",       {{"type","string"},{"description","Root directory, default '.'"}}},
                    {"file_glob", {{"type","string"},{"description","Optional file filter, e.g. '*.cpp'"}}}
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
