#include "agent.h"
#include "colors.h"
#include "lang.h"
#include "rc.h"
#include "tools.h"
#include "ui.h"
#include <string_view>
#include <atomic>
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>
#include <sys/utsname.h>

// ── helpers ───────────────────────────────────────────────────────────────────

// Lightweight markdown styling for assistant commentary (defined below,
// after split_lines).
static std::string render_markdown(const std::string& text);

// ── print functions ───────────────────────────────────────────────────────────

// ● (white) — AI commentary / explanation
void Agent::print_commentary(const std::string& text) {
    std::cout << "\n"
              << clr::white << clr::bold << "● " << clr::reset
              << render_markdown(text) << "\n";
    rc::push_event({{"type", "text"}, {"text", text}});
}

// ● (red) — failure
void Agent::print_failure(const std::string& msg) {
    std::cout << clr::red << clr::bold << "● " << clr::reset
              << clr::red << msg << clr::reset << "\n";
    rc::push_event({{"type", "error"}, {"text", msg}});
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

// ── markdown renderer ────────────────────────────────────────────────────────

// Apply inline styling: **bold** and `inline code`.
static std::string render_inline(const std::string& line) {
    static const std::regex bold_re("\\*\\*(.+?)\\*\\*");
    static const std::regex code_re("`([^`]+?)`");

    std::string out = std::regex_replace(line, bold_re,
        std::string(clr::bold) + "$1" + clr::reset);
    out = std::regex_replace(out, code_re,
        std::string(clr::cyan) + "$1" + clr::reset);
    return out;
}

// Render a small subset of markdown (bold, inline code, fenced code blocks,
// ATX headers) using ANSI styling, for printing assistant commentary.
static std::string render_markdown(const std::string& text) {
    auto lines = split_lines(text);
    std::ostringstream out;
    bool in_code_block = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];

        if (line.starts_with("```")) {
            in_code_block = !in_code_block;
            continue; // drop the fence line itself
        }

        if (in_code_block) {
            out << clr::dim << "  │ " << clr::reset << clr::cyan << line << clr::reset;
        } else {
            // ATX header: 1-6 '#' followed by a space (avoids `#include <...>`).
            size_t hashes = 0;
            while (hashes < line.size() && hashes < 6 && line[hashes] == '#') ++hashes;
            if (hashes > 0 && hashes < line.size() && line[hashes] == ' ') {
                std::string title = line.substr(hashes + 1);
                out << clr::bold << title << clr::reset;
            } else {
                out << render_inline(line);
            }
        }

        if (i + 1 < lines.size()) out << "\n";
    }

    return out.str();
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

    if (name == "create_file") {
        std::string path = args.value("path", "?");
        if (is_error) {
            print_failure(result);
        } else {
            std::cout << clr::green << clr::bold << "● " << clr::reset
                      << clr::bold << "Create file " << clr::reset
                      << clr::dim << "[" << clr::reset
                      << path
                      << clr::dim << "]" << clr::reset << "\n";
        }

    } else if (name == "write_file") {
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

    } else if (name == "search_files") {
        std::string pat = args.value("pattern", "?");
        if (is_error) {
            print_failure(result);
        } else {
            print_info_tool("Search", pat);
            print_preview(result, 8);
        }

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

    rc::push_event({{"type", "tool"}, {"name", name}, {"args", args}, {"result", result}});
}

// ── environment info (OS, distro, package manager) ────────────────────────────

static std::string detect_os_info() {
    struct utsname uts{};
    uname(&uts);

    std::string pretty_name, id, id_like;
    std::ifstream os_release("/etc/os-release");
    if (os_release) {
        auto unquote = [](std::string s) {
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                s = s.substr(1, s.size() - 2);
            return s;
        };
        std::string line;
        while (std::getline(os_release, line)) {
            if      (line.starts_with("PRETTY_NAME=")) pretty_name = unquote(line.substr(12));
            else if (line.starts_with("ID="))          id          = unquote(line.substr(3));
            else if (line.starts_with("ID_LIKE="))     id_like     = unquote(line.substr(8));
        }
    }

    static constexpr std::pair<std::string_view, std::string_view> PKG_MGRS[] = {
        {"arch", "pacman"}, {"manjaro", "pacman"},
        {"debian", "apt"},  {"ubuntu", "apt"},   {"mint", "apt"},
        {"fedora", "dnf"},  {"rhel", "dnf"},     {"centos", "dnf"},
        {"suse", "zypper"}, {"alpine", "apk"},   {"void", "xbps"},
        {"gentoo", "emerge"}, {"nixos", "nix"},
    };

    std::string pkg_mgr;
    for (const auto& [key, mgr] : PKG_MGRS) {
        if (id.find(key) != std::string::npos || id_like.find(key) != std::string::npos) {
            pkg_mgr = mgr;
            break;
        }
    }

    std::string sysname = uts.sysname;
    std::string distro  = sysname == "Darwin" ? "macOS"
                         : !pretty_name.empty() ? pretty_name
                         : !id.empty()          ? id
                         : sysname;
    if (pkg_mgr.empty() && distro == "macOS") pkg_mgr = "brew";

    std::string info = std::format("\n\n## Environment\nOS: {} ({} {}, {})",
                                    distro, sysname, uts.release, uts.machine);
    if (!pkg_mgr.empty())
        info += std::format("\nLikely package manager: {}", pkg_mgr);
    if (const char* shell = getenv("SHELL"); shell && *shell)
        info += std::format("\nShell: {}", shell);
    info += "\nUse this to pick commands that are likely to exist on this system "
            "(e.g. the right package manager for installs). If unsure, check with "
            "`which`/`command -v` rather than assuming.";

    return info;
}

static const std::string& os_info() {
    static std::string info = detect_os_info();
    return info;
}

// ── core ──────────────────────────────────────────────────────────────────────

Agent::Agent(const Config& cfg)
    : config_(cfg), client_(cfg) {
    history_.push_back({"system", cfg.system_prompt + os_info(), {}, {}});
}

Agent::Agent(const Config& cfg, std::vector<Message> history)
    : config_(cfg), client_(cfg),
      history_(std::move(history)) {
    // Ensure system prompt is always first and up to date
    if (history_.empty() || history_[0].role != "system")
        history_.insert(history_.begin(), {"system", cfg.system_prompt + os_info(), {}, {}});
    else
        history_[0].content = cfg.system_prompt + os_info();
}

void Agent::clear_history() {
    history_.clear();
    history_.push_back({"system", config_.system_prompt + os_info(), {}, {}});
    std::cout << clr::dim << "  " << lang::S().history_cleared << "\n" << clr::reset;
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

// ── random thinking word ──────────────────────────────────────────────────────

static std::string random_thinking() {
    static constexpr std::string_view EN[] = {
        "Thinking",     "Puzzling",     "Pondering",    "Scheming",
        "Hallucinating","Dreaming",     "Cooking",      "Brewing",
        "Calculating",  "Meditating",   "Contemplating","Reasoning",
        "Imagining",    "Plotting",     "Wondering",    "Deliberating",
        "Summoning",    "Wrangling",    "Vibing",       "Crunching",
        "Manifesting",  "Conjuring",    "Mulling",      "Stewing",
        "Debugging",    "Procrastinating","Philosophizing","Overfitting",
        "Suffering",    "Caffeinating", "Tokenizing",   "Hallucinating harder",
        "Doomscrolling","Overthinking", "Yapping",      "Spiraling",
        "Rubber-ducking","Googling it", "Side-questing","Spinning up",
    };
    static constexpr std::string_view RU[] = {
        "Думаю",        "Соображаю",    "Варю",         "Колдую",
        "Галлюцинирую", "Мечтаю",       "Готовлю",      "Завариваю",
        "Считаю",       "Медитирую",    "Размышляю",    "Рассуждаю",
        "Воображаю",    "Строю планы",  "Гадаю",        "Взвешиваю",
        "Призываю",     "Борюсь",       "Вибрирую",     "Перемалываю",
        "Манифестирую", "Торможу",      "Жую",          "Тупею",
        "Дебажу",       "Прокрастинирую","Философствую","Переобучаюсь",
        "Страдаю",      "Кофеинируюсь", "Токенизирую",  "Галлюцинирую ещё сильнее",
        "Скроллю ленту","Передумываю",  "Болтаю",       "Впадаю в спираль",
        "Объясняю уточке","Гуглю",      "Ухожу в побочный квест","Раскручиваюсь",
        "Передёргиваю", "Закрываю",
    };
    static constexpr std::string_view DE[] = {
        "Denke",        "Grüble",       "Sinne",        "Plane",
        "Halluziniere", "Träume",       "Koche",        "Braue",
        "Berechne",     "Meditiere",    "Überlege",     "Schlussfolgere",
        "Fantasiere",   "Schmede",      "Wundere mich", "Abwäge",
        "Beschwöre",    "Ringe",        "Vibe",         "Verarbeite",
        "Manifestiere", "Zaubers",      "Kaue",         "Schmorre",
        "Debugge",      "Prokrastiniere","Philosophiere","Überanpasse",
        "Leide",        "Koffeiniere",  "Tokenisiere",  "Halluziniere intensiver",
        "Scrolle durch den Feed","Denke zu viel nach","Quatsche","Drehe durch",
        "Erkläre es der Gummiente","Google es","Mache eine Nebenquest","Fahre hoch",
    };
    static std::mt19937 rng{std::random_device{}()};
    switch (lang::current()) {
        case lang::Code::Ru: {
            static std::uniform_int_distribution<size_t> d{0, std::size(RU) - 1};
            return std::string(RU[d(rng)]);
        }
        case lang::Code::De: {
            static std::uniform_int_distribution<size_t> d{0, std::size(DE) - 1};
            return std::string(DE[d(rng)]);
        }
        default: {
            static std::uniform_int_distribution<size_t> d{0, std::size(EN) - 1};
            return std::string(EN[d(rng)]);
        }
    }
}

// ── random done verb ──────────────────────────────────────────────────────────

static std::string random_verb() {
    static constexpr std::string_view EN[] = {
        "Churned",    "Cooked",      "Sautéed",     "Brewed",
        "Cogitated",  "Pondered",    "Distilled",   "Conjured",
        "Summoned",   "Wrangled",    "Synthesized", "Computed",
        "Baked",      "Marinated",   "Fermented",   "Processed",
        "Contemplated","Deliberated","Calculated",  "Forged",
        "Simulated",  "Meditated",   "Reasoned",    "Deduced",
        "Extrapolated","Hallucinated","Schemed",     "Devised",
        "Mulled",     "Stewed",      "Roasted",     "Smoked",
        "Debugged",   "Suffered",    "Overcomplicated","Philosophized",
        "Procrastinated","Tokenized","Vibed",       "Caffeinated",
        "Doomscrolled","Overthought","Yapped",      "Spiraled",
        "Rubber-ducked","Googled it","Side-quested","Spun up",
    };
    static constexpr std::string_view RU[] = {
        "Сварил",       "Приготовил",  "Обжарил",     "Заварил",
        "Поразмыслил",  "Взвесил",     "Перегнал",    "Призвал",
        "Вызвал",       "Выкрутился",  "Синтезировал","Посчитал",
        "Испёк",        "Замариновал", "Перебродил",  "Обработал",
        "Созерцал",     "Взвесил",     "Вычислил",    "Выковал",
        "Смоделировал", "Помедитировал","Осмыслил",   "Вывел",
        "Экстраполировал","Нагалюцинировал","Схитрил","Придумал",
        "Пожевал",      "Потомил",     "Прожарил",    "Накурился",
        "Задебажил",    "Пострадал",   "Усложнил",    "Пофилософствовал",
        "Прокрастинировал","Токенизировал","Завибрировал","Кофеинировался",
        "Проскроллил ленту","Передумал","Наболтал",   "Впал в спираль",
        "Объяснил уточке","Погуглил",  "Сходил в побочный квест","Раскрутился",
    };
    static constexpr std::string_view DE[] = {
        "Gebrutzelt",   "Gekocht",     "Sautiert",    "Gebraut",
        "Gegrübelt",    "Abgewogen",   "Destilliert", "Beschworen",
        "Herbeigerufen","Gerungen",    "Synthetisiert","Berechnet",
        "Gebacken",     "Mariniert",   "Vergoren",    "Verarbeitet",
        "Kontempliert", "Deliberiert", "Kalkuliert",  "Geschmiedet",
        "Simuliert",    "Meditiert",   "Geschlussfolgert","Deduziert",
        "Extrapoliert", "Halluziniert","Geschachert", "Erdacht",
        "Durchgekaut",  "Geschmort",   "Geröstet",    "Verraucht",
        "Debuggt",      "Gelitten",    "Überkompliziert","Philosophiert",
        "Prokrastiniert","Tokenisiert","Gevibt",      "Koffeiniert",
        "Durch den Feed gescrollt","Zu viel nachgedacht","Gequatscht","Durchgedreht",
        "Der Gummiente erklärt","Gegoogelt","Nebenquest gemacht","Hochgefahren",
    };
    static std::mt19937 rng{std::random_device{}()};
    switch (lang::current()) {
        case lang::Code::Ru: {
            static std::uniform_int_distribution<size_t> d{0, std::size(RU) - 1};
            return std::string(RU[d(rng)]);
        }
        case lang::Code::De: {
            static std::uniform_int_distribution<size_t> d{0, std::size(DE) - 1};
            return std::string(DE[d(rng)]);
        }
        default: {
            static std::uniform_int_distribution<size_t> d{0, std::size(EN) - 1};
            return std::string(EN[d(rng)]);
        }
    }
}

// ── spinner ───────────────────────────────────────────────────────────────────

static void run_spinner(std::atomic<bool>& done,
                        std::chrono::steady_clock::time_point start,
                        const std::string& word,
                        std::stop_token st) {
    constexpr std::string_view frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};

    // Print initial spinner line; every frame overwrites it with \r
    std::cout << std::flush;

    int i = 0;
    while (!done.load() && !st.stop_requested()) {
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "\r\033[K  " << clr::dim
                  << frames[i % 10] << " " << word << "… "
                  << std::format("{:.1f}s", elapsed) << clr::reset
                  << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++i;
    }
    // Erase spinner line
    std::cout << "\r\033[K" << std::flush;
}

// ── chat request result (filled in by a detachable worker thread) ──────────────

struct ChatResult {
    nlohmann::json     response;
    std::exception_ptr error;
    std::atomic<bool>  done{false};
};

// ── run ───────────────────────────────────────────────────────────────────────

void Agent::run(const std::string& user_message) {
    history_.push_back({"user", user_message, {}, {}});
    rc::push_event({{"type", "user"}, {"text", user_message}});

    nlohmann::json tool_schemas = tools::get_schemas();
    auto run_start = std::chrono::steady_clock::now();

    int total_prompt     = 0;
    int total_completion = 0;

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
        nlohmann::json response;
        bool interrupted = false;
        ui::clear_interrupted();
        try {
            std::atomic<bool> done{false};
            auto think_start = std::chrono::steady_clock::now();
            std::string word = random_thinking();
            std::jthread spinner([&done, think_start, word](std::stop_token st) {
                run_spinner(done, think_start, word, st);
            });

            // Run the request on a worker thread so Ctrl+C can abandon it
            // without blocking on the (uncancellable) blocking HTTP call.
            // Everything the worker touches is copied by value, so it stays
            // valid even if we detach it and return early.
            auto result = std::make_shared<ChatResult>();
            std::thread worker([client = client_, history = history_,
                                 tools = tool_schemas, result]() mutable {
                try {
                    result->response = client.chat(history, tools);
                } catch (...) {
                    result->error = std::current_exception();
                }
                result->done.store(true);
            });

            while (!result->done.load()) {
                if (ui::interrupted()) { interrupted = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            done = true;

            if (interrupted) {
                worker.detach();
            } else {
                worker.join();
                if (result->error) std::rethrow_exception(result->error);
                response = std::move(result->response);
            }
        } catch (const std::exception& e) {
            print_failure(e.what());
            return;
        }

        if (interrupted) {
            ui::clear_interrupted();
            std::cout << clr::dim << "\n  " << lang::S().agent_interrupted
                      << "\n\n" << clr::reset;
            rc::push_event({{"type", "status"}, {"text", lang::S().agent_interrupted}});
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

        std::string status = std::format("{} for {:.1f}s", random_verb(), total_sec);
        if (total_prompt > 0 || total_completion > 0) {
            status += std::format("  ·  {} prompt + {} completion = {} tokens",
                total_prompt, total_completion, total_prompt + total_completion);
        }

        std::cout << clr::dim << "  " << status << clr::reset << "\n\n";
        rc::push_event({{"type", "status"}, {"text", status}});
        return;
    }

    print_failure(std::format("Reached max iterations ({})", MAX_ITERATIONS));
}
