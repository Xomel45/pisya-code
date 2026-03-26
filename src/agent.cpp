#include "agent.h"
#include "tools.h"
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

namespace clr {
    constexpr auto reset  = "\033[0m";
    constexpr auto bold   = "\033[1m";
    constexpr auto dim    = "\033[2m";
    constexpr auto white  = "\033[97m";
    constexpr auto green  = "\033[32m";
    constexpr auto red    = "\033[31m";
    constexpr auto yellow = "\033[33m";
}

// ── helpers ───────────────────────────────────────────────────────────────────

// ── print functions ───────────────────────────────────────────────────────────

// ● (white) — AI commentary / explanation
void Agent::print_commentary(const std::string& text) {
    std::cout << "\n"
              << clr::white << clr::bold << "● " << clr::reset
              << text << "\n";
}

// ● (green) — successful file operation
void Agent::print_file_success(const std::string& label,
                                const std::string& path,
                                const std::string& detail) {
    std::cout << clr::green << clr::bold << "● " << clr::reset
              << clr::bold << label << clr::reset
              << clr::dim << "(" << clr::reset
              << path
              << clr::dim << ")" << clr::reset << "\n";
    if (!detail.empty())
        std::cout << clr::dim << "  ⎿  " << clr::reset << detail << "\n";
}

// ● (red) — failure
void Agent::print_failure(const std::string& msg) {
    std::cout << clr::red << clr::bold << "● " << clr::reset
              << clr::red << msg << clr::reset << "\n";
}

// dim line — informational tool (read, list, glob)
void Agent::print_info_tool(const std::string& label, const std::string& path) {
    std::cout << clr::dim << "● " << label << "(" << path << ")" << clr::reset << "\n";
}

// ── diff renderer ────────────────────────────────────────────────────────────

static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream ss(s);
    std::string l;
    while (std::getline(ss, l)) lines.push_back(l);
    if (!s.empty() && s.back() == '\n') lines.push_back("");
    return lines;
}

static std::vector<std::string> read_file_lines(const std::string& path) {
    std::ifstream f(path);
    std::vector<std::string> lines;
    std::string l;
    while (std::getline(f, l)) lines.push_back(l);
    return lines;
}

// Find the 0-based line index where new_lines starts in file_lines.
static int find_start_line(const std::vector<std::string>& file,
                            const std::vector<std::string>& new_lines) {
    if (new_lines.empty()) return -1;
    for (int i = 0; i + (int)new_lines.size() <= (int)file.size(); ++i) {
        bool match = true;
        for (int j = 0; j < (int)new_lines.size(); ++j)
            if (file[i+j] != new_lines[j]) { match = false; break; }
        if (match) return i;
    }
    return -1;
}

static void print_diff(const std::string& path,
                        const std::string& old_str,
                        const std::string& new_str) {
    constexpr int CONTEXT   = 3;
    constexpr int MAX_LINES = 40; // total diff lines shown before truncating

    auto new_lines  = split_lines(new_str);
    auto old_lines  = split_lines(old_str);
    auto file_lines = read_file_lines(path);

    // Remove trailing empty line artefact from split
    auto trim_tail = [](std::vector<std::string>& v) {
        if (!v.empty() && v.back().empty()) v.pop_back();
    };
    trim_tail(new_lines);
    trim_tail(old_lines);

    int start = find_start_line(file_lines, new_lines);
    if (start == -1) return; // couldn't locate — skip diff

    // Stats line
    int added   = (int)new_lines.size();
    int removed = (int)old_lines.size();
    std::cout << clr::dim << "  ⎿  "
              << clr::reset << clr::green << "+" << added
              << clr::reset << clr::dim << " / "
              << clr::reset << clr::red   << "-" << removed
              << clr::reset << "\n";

    int ctx_start = std::max(0, start - CONTEXT);
    int ctx_end   = std::min((int)file_lines.size(),
                              start + (int)new_lines.size() + CONTEXT);
    int printed   = 0;

    // Context before the change
    for (int i = ctx_start; i < start && printed < MAX_LINES; ++i, ++printed) {
        std::cout << clr::dim
                  << std::format("  {:>5}  ", i + 1)
                  << file_lines[i] << clr::reset << "\n";
    }

    // Removed lines (old_str)
    for (int i = 0; i < (int)old_lines.size() && printed < MAX_LINES; ++i, ++printed) {
        std::cout << clr::red
                  << std::format("  {:>5} -", start + i + 1)
                  << old_lines[i] << clr::reset << "\n";
    }

    // Added lines (new_str)
    for (int i = 0; i < (int)new_lines.size() && printed < MAX_LINES; ++i, ++printed) {
        std::cout << clr::green
                  << std::format("  {:>5} +", start + i + 1)
                  << new_lines[i] << clr::reset << "\n";
    }

    // Context after
    for (int i = start + (int)new_lines.size();
         i < ctx_end && printed < MAX_LINES; ++i, ++printed) {
        std::cout << clr::dim
                  << std::format("  {:>5}  ", i + 1)
                  << file_lines[i] << clr::reset << "\n";
    }

    if (printed >= MAX_LINES)
        std::cout << clr::dim
                  << std::format("       … +{} lines (ctrl+o to expand)",
                                 (int)old_lines.size() + (int)new_lines.size()
                                 + (ctx_end - start - (int)new_lines.size())
                                 - MAX_LINES)
                  << clr::reset << "\n";
}

// Print first max_lines of text with "  ⎿  " prefix, then truncation hint
static void print_preview(const std::string& text, int max_lines = 6) {
    auto lines = split_lines(text);
    while (!lines.empty() && lines.back().empty()) lines.pop_back();

    int shown = std::min((int)lines.size(), max_lines);
    for (int i = 0; i < shown; ++i)
        std::cout << clr::dim << "  ⎿  " << clr::reset << lines[i] << "\n";

    int remaining = (int)lines.size() - shown;
    if (remaining > 0)
        std::cout << clr::dim
                  << std::format("       … +{} lines (ctrl+o to expand)", remaining)
                  << clr::reset << "\n";
}

// ── tool output formatter ─────────────────────────────────────────────────────

static void tool_header(const std::string& color, const std::string& label,
                         const std::string& path) {
    std::cout << color << clr::bold << "● " << clr::reset
              << clr::bold << label << clr::reset
              << clr::dim  << "(" << clr::reset
              << path
              << clr::dim  << ")" << clr::reset << "\n";
}

void Agent::print_tool_output(const std::string& name,
                               const nlohmann::json& args,
                               const std::string& result) {
    bool is_error = result.starts_with("Error:");

    if (name == "write_file") {
        std::string path = args.value("path", "?");
        if (is_error) {
            print_failure(result);
        } else {
            tool_header(clr::green, "Write", path);
            print_preview(args.value("content", ""));
        }

    } else if (name == "edit_file") {
        std::string path = args.value("path", "?");
        if (is_error) {
            print_failure(result);
        } else {
            tool_header(clr::green, "Update", path);
            print_diff(path,
                       args.value("old_string", ""),
                       args.value("new_string", ""));
        }

    } else if (name == "read_file") {
        std::string path = args.value("path", "?");
        if (is_error) print_failure(result);
        else          print_info_tool("Read", path);

    } else if (name == "list_dir") {
        std::string path = args.value("path", ".");
        if (is_error) print_failure(result);
        else          print_info_tool("List", path);

    } else if (name == "glob_files") {
        std::string pat = args.value("pattern", "?");
        if (is_error) print_failure(result);
        else          print_info_tool("Glob", pat);

    } else if (name == "bash") {
        if (is_error) {
            print_failure(result);
        } else {
            tool_header(clr::green, "Bash",
                        args.value("command", "?").substr(0, 60));
            print_preview(result, 8);
        }
    } else {
        if (is_error) print_failure(result);
        else          std::cout << clr::dim << "● " << name << ": "
                                << result << clr::reset << "\n";
    }
}

// ── core ──────────────────────────────────────────────────────────────────────

Agent::Agent(const Config& cfg)
    : config_(cfg), client_(cfg.host, cfg.port, cfg.model) {
    history_.push_back({"system", cfg.system_prompt, {}, {}});
}

Agent::Agent(const Config& cfg, std::vector<Message> history)
    : config_(cfg), client_(cfg.host, cfg.port, cfg.model),
      history_(std::move(history)) {
    // Ensure system prompt is always first and up to date
    if (history_.empty() || history_[0].role != "system")
        history_.insert(history_.begin(), {"system", cfg.system_prompt, {}, {}});
    else
        history_[0].content = cfg.system_prompt;
}

void Agent::clear_history() {
    history_.clear();
    history_.push_back({"system", config_.system_prompt, {}, {}});
    std::cout << clr::dim << "  [history cleared]\n" << clr::reset;
}

void Agent::handle_tool_calls(const nlohmann::json& tool_calls) {
    for (const auto& tc : tool_calls) {
        std::string id   = tc.value("id", "");
        std::string name = tc["function"]["name"].get<std::string>();

        nlohmann::json args;
        try {
            args = nlohmann::json::parse(tc["function"]["arguments"].get<std::string>());
        } catch (...) {
            args = {};
        }

        std::string result = tools::execute(name, args);
        print_tool_output(name, args, result);

        history_.push_back({"tool", result, id, {}});
    }
}

// ── random verb ──────────────────────────────────────────────────────────────

static std::string random_thinking() {
    static constexpr std::string_view words[] = {
        "Thinking",     "Puzzling",     "Pondering",    "Scheming",
        "Hallucinating","Dreaming",     "Cooking",      "Brewing",
        "Calculating",  "Meditating",   "Contemplating","Reasoning",
        "Imagining",    "Plotting",     "Wondering",    "Deliberating",
        "Summoning",    "Wrangling",    "Vibing",       "Crunching",
        "Manifesting",  "Conjuring",    "Mulling",      "Stewing",
    };
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_int_distribution<size_t> dist{0, std::size(words) - 1};
    return std::string(words[dist(rng)]);
}

static std::string random_verb() {
    static constexpr std::string_view verbs[] = {
        "Churned",    "Cooked",      "Sautéed",     "Brewed",
        "Cogitated",  "Pondered",    "Distilled",   "Conjured",
        "Summoned",   "Wrangled",    "Synthesized", "Computed",
        "Baked",      "Marinated",   "Fermented",   "Processed",
        "Contemplated","Deliberated","Calculated",  "Forged",
        "Simulated",  "Meditated",   "Reasoned",    "Deduced",
        "Extrapolated","Hallucinated","Schemed",     "Devised",
        "Mulled",     "Stewed",      "Roasted",     "Smoked",
    };
    static std::mt19937 rng{std::random_device{}()};
    static std::uniform_int_distribution<size_t> dist{0, std::size(verbs) - 1};
    return std::string(verbs[dist(rng)]);
}

// ── spinner ───────────────────────────────────────────────────────────────────

static void run_spinner(std::atomic<bool>& done,
                        std::chrono::steady_clock::time_point start,
                        const std::string& word,
                        std::stop_token st) {
    constexpr std::string_view frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};

    // Save cursor position — every frame restores here and overwrites
    std::cout << "\033[s" << std::flush;

    int i = 0;
    while (!done.load() && !st.stop_requested()) {
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "\033[u\033[K  " << clr::dim
                  << frames[i % 10] << " " << word << "… "
                  << std::format("{:.1f}s", elapsed) << clr::reset
                  << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++i;
    }
    // Erase spinner line, leave cursor at start of clean line
    std::cout << "\033[u\033[K" << std::flush;
}

// ── run ───────────────────────────────────────────────────────────────────────

void Agent::run(const std::string& user_message) {
    history_.push_back({"user", user_message, {}, {}});

    nlohmann::json tool_schemas = tools::get_schemas();
    auto run_start = std::chrono::steady_clock::now();

    int total_prompt     = 0;
    int total_completion = 0;

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
        nlohmann::json response;
        try {
            std::atomic<bool> done{false};
            auto think_start = std::chrono::steady_clock::now();
            std::string word = random_thinking();
            std::jthread spinner([&done, think_start, word](std::stop_token st) {
                run_spinner(done, think_start, word, st);
            });

            response = client_.chat(history_, tool_schemas);
            done = true;
        } catch (const std::exception& e) {
            print_failure(e.what());
            return;
        }

        // Accumulate token usage (present in every response)
        if (response.contains("usage") && !response["usage"].is_null()) {
            auto& u = response["usage"];
            total_prompt     += u.value("prompt_tokens",     0);
            total_completion += u.value("completion_tokens", 0);
        }

        auto& choice = response["choices"][0];
        auto& msg    = choice["message"];

        // Print text content
        if (msg.contains("content") && !msg["content"].is_null()) {
            std::string text = msg["content"].get<std::string>();
            if (!text.empty()) print_commentary(text);
        }

        // Handle tool calls
        if (msg.contains("tool_calls") && !msg["tool_calls"].is_null()
            && !msg["tool_calls"].empty()) {

            Message assistant_msg;
            assistant_msg.role       = "assistant";
            assistant_msg.content    = msg.value("content", "");
            assistant_msg.tool_calls = msg["tool_calls"];
            history_.push_back(std::move(assistant_msg));

            handle_tool_calls(msg["tool_calls"]);
            continue;
        }

        // Final response — no tool calls
        Message assistant_msg;
        assistant_msg.role    = "assistant";
        assistant_msg.content = msg.value("content", "");
        history_.push_back(std::move(assistant_msg));

        // Footer: timing + tokens
        auto total_sec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - run_start).count();

        std::cout << clr::dim << "  " << random_verb() << " for "
                  << std::format("{:.1f}s", total_sec);

        if (total_prompt > 0 || total_completion > 0) {
            std::cout << std::format(
                "  ·  {} prompt + {} completion = {} tokens",
                total_prompt, total_completion, total_prompt + total_completion);
        }

        std::cout << clr::reset << "\n\n";
        return;
    }

    print_failure(std::format("Reached max iterations ({})", MAX_ITERATIONS));
}
