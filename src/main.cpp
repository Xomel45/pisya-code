#include "agent.h"
#include "config.h"
#include "session.h"
#include "ui.h"
#include "../third_party/json.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>

namespace clr {
    constexpr auto reset  = "\033[0m";
    constexpr auto bold   = "\033[1m";
    constexpr auto orange = "\033[38;5;208m";
    constexpr auto dim    = "\033[2m";
    constexpr auto white  = "\033[97m";
    constexpr auto green  = "\033[32m";
}

static std::string get_username() {
    if (const char* u = getenv("USER"); u && *u) return u;
    if (const char* h = getenv("HOME"); h && *h)
        return std::filesystem::path(h).filename().string();
    return "friend";
}

static void print_banner(const Config& cfg, const std::string& username) {
    std::cout << "\n"
              << clr::orange << clr::bold
              << R"(  ____  _                    ____          _      )" << "\n"
              << R"( |  _ \(_)___ _   _  __ _   / ___|___   __| | ___ )" << "\n"
              << R"( | |_) | / __| | | |/ _` | | |   / _ \ / _` |/ _ \)" << "\n"
              << R"( |  __/| \__ \ |_| | (_| | | |__| (_) | (_| |  __|)" << "\n"
              << R"( |_|   |_|___/\__, |\__,_|  \____\___/ \__,_|\___|)" << "\n"
              << R"(              |___/                                 )" << "\n"
              << clr::reset << "\n"
              << "  " << clr::dim << "Local AI coding assistant" << clr::reset
              << "  " << clr::white << clr::bold << "Hey, " << username << "!" << clr::reset
              << "\n\n";
    std::cout << clr::dim;
    cfg.print();
    std::cout << clr::reset
              << "\n"
              << clr::dim
              << "  Commands: /clear — clear history | /config — show config | /exit — quit\n"
              << clr::reset << "\n";
}

// ── feedback timer ────────────────────────────────────────────────────────────

static std::string feedback_path() {
    const char* home = getenv("HOME");
    return std::string(home ? home : ".") + "/.pisya/feedback.json";
}

static int64_t now_ts() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Returns true if it's time to show the feedback prompt.
// Side effect: creates/updates ~/.pisya/feedback.json.
static bool feedback_due() {
    using json = nlohmann::json;
    constexpr int64_t FIRST_DELAY =  6 * 3600;  // 6 hours
    constexpr int64_t REPEAT      = 48 * 3600;  // 48 hours

    std::string path = feedback_path();
    int64_t ts       = now_ts();

    json state;
    if (std::filesystem::exists(path)) {
        std::ifstream f(path);
        try { state = json::parse(f); } catch (...) {}
    }

    int64_t first_launch = state.value("first_launch", (int64_t)0);
    int64_t last_shown   = state.value("last_shown",   (int64_t)0);

    // First ever launch — record time, don't show yet
    if (first_launch == 0) {
        state["first_launch"] = ts;
        state["last_shown"]   = (int64_t)0;
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());
        std::ofstream f(path);
        f << state.dump(2);
        return false;
    }

    bool due = (last_shown == 0 && ts - first_launch >= FIRST_DELAY)
            || (last_shown >  0 && ts - last_shown    >= REPEAT);

    if (due) {
        state["last_shown"] = ts;
        std::ofstream f(path);
        f << state.dump(2);
    }
    return due;
}

static void show_feedback(const std::string& model) {
    std::cout << clr::white << clr::bold << "● " << clr::reset
              << std::format("How is {} doing this session? ", model)
              << clr::dim << "(optional)" << clr::reset << "\n";

    std::string choice = ui::select("", {"Bad", "Fine", "Good", "Very Good", "Dismiss"});

    if (choice == "Dismiss" || choice.empty()) return;
    if      (choice == "Bad")       std::cout << clr::dim   << "  Got it. Hope things improve.\n" << clr::reset;
    else if (choice == "Fine")      std::cout << clr::dim   << "  Noted. Let's get to work.\n"    << clr::reset;
    else if (choice == "Good")      std::cout << clr::green << "  Glad to hear it!\n"             << clr::reset;
    else if (choice == "Very Good") std::cout << clr::green << "  Let's keep it that way!\n"      << clr::reset;
    std::cout << "\n";
}

// Pick a session interactively when --resume is given without an ID
static std::string pick_session() {
    auto sessions = Session::list_all();
    if (sessions.empty()) {
        std::cout << clr::dim << "  No saved sessions found. Starting a new one.\n\n" << clr::reset;
        return "";
    }

    std::vector<std::string> options;
    for (const auto& s : sessions) {
        // "2026-03-26 14:32  [12 msgs]  write me a hello world..."
        std::string date = s.id.size() >= 16
            ? s.id.substr(0,10) + " " + s.id.substr(11,5)
            : s.id;
        options.push_back(std::format("{}  [{:>2} msgs]  {}",
            date, s.msg_count, s.preview));
    }

    std::cout << clr::white << clr::bold << "● " << clr::reset
              << "Resume which session?\n";
    std::string choice = ui::select("", options);

    if (choice.empty()) return ""; // cancelled

    // Match choice back to session ID
    for (int i = 0; i < (int)options.size(); ++i)
        if (options[i] == choice) return sessions[i].id;

    return ""; // "Свой ответ..." — user typed an ID directly
               // In that case choice IS the typed ID
               // Check if it matches any known session
    // (unreachable after the loop — if custom input, return it as-is)
}

int main(int argc, char* argv[]) {
    Config cfg      = Config::load();
    std::string username = get_username();

    // ── parse --resume ────────────────────────────────────────────────────────
    std::string resume_id;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--resume") {
            if (i + 1 < argc) resume_id = argv[++i];
            else               resume_id = "__pick__";
        }
    }

    print_banner(cfg, username);

    // ── load or create session ────────────────────────────────────────────────
    Session session;
    bool    resumed = false;

    if (!resume_id.empty()) {
        if (resume_id == "__pick__")
            resume_id = pick_session();

        if (!resume_id.empty()) {
            try {
                session = Session::load(resume_id);
                resumed = true;
                int user_msgs = 0;
                for (const auto& m : session.messages)
                    if (m.role == "user") ++user_msgs;
                std::cout << clr::green << "  ● Resumed: " << clr::reset
                          << session.id << clr::dim
                          << std::format("  ({} messages)\n\n", user_msgs)
                          << clr::reset;
            } catch (const std::exception& e) {
                std::cout << clr::dim << "  Could not load session: "
                          << e.what() << " — starting fresh.\n\n" << clr::reset;
            }
        }
    }

    if (!resumed) {
        session = Session::create(cfg.model);
        if (feedback_due()) show_feedback(cfg.model);
    }

    // ── build agent ───────────────────────────────────────────────────────────
    Agent agent = resumed
        ? Agent(cfg, session.messages)
        : Agent(cfg);

    // ── REPL ──────────────────────────────────────────────────────────────────
    while (true) {
        std::string input = ui::read_input();

        if (input.empty()) continue;

        if (input == "/exit" || input == "/quit") {
            std::cout << "Bye, " << username << "!\n";
            break;
        }
        if (input == "/clear") {
            agent.clear_history();
            session = Session::create(cfg.model);
            continue;
        }
        if (input == "/config") {
            cfg.print();
            continue;
        }
        if (input == "/session") {
            std::cout << clr::dim << "  Session: " << session.id << clr::reset << "\n\n";
            continue;
        }

        agent.run(input);

        // Auto-save after every response
        session.messages = agent.get_history();
        session.model    = cfg.model;
        session.save();
    }

    return 0;
}
