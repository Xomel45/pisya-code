#include "agent.h"
#include "ai_client.h"
#include "config.h"
#include "lang.h"
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
#include <unistd.h>

namespace clr {
    constexpr auto reset  = "\033[0m";
    constexpr auto bold   = "\033[1m";
    constexpr auto orange = "\033[38;5;208m";
    constexpr auto dim    = "\033[2m";
    constexpr auto white  = "\033[97m";
    constexpr auto green  = "\033[32m";
}

// ── git checkpoint ────────────────────────────────────────────────────────────

static void git_init_if_needed() {
    // Already inside a git repo — do nothing
    if (std::system("git rev-parse --git-dir >/dev/null 2>&1") == 0) return;

    std::system("git init -q 2>/dev/null");
    std::system("git add -A 2>/dev/null");
    std::system("git commit -q -m \"pisya: initial state\" 2>/dev/null");
}

// Sanitize user message for use as a git commit message subject line.
static std::string sanitize_for_git(const std::string& msg) {
    std::string out;
    out.reserve(msg.size());
    for (char c : msg) {
        if (c == '\n' || c == '\r') { out += ' '; }
        else if (c == '"' || c == '\\' || c == '`' || c == '$') { /* strip */ }
        else out += c;
    }
    if (out.size() > 60) { out.resize(60); out += "…"; }
    return out;
}

static void git_checkpoint(const std::string& user_msg) {
    std::string subject = sanitize_for_git(user_msg);
    std::string cmd = "git add -A && git commit -q -m \"pisya: " + subject + "\" 2>/dev/null";
    std::system(cmd.c_str());
}

static std::string get_username() {
    if (const char* u = getenv("USER"); u && *u) return u;
    if (const char* h = getenv("HOME"); h && *h)
        return std::filesystem::path(h).filename().string();
    return "friend";
}

static void print_banner(const Config& cfg, const std::string& username,
                          const std::string& model_extra) {
    std::cout << "\n"
              << clr::orange << clr::bold
              << R"(  ____  _                    ____          _      )" << "\n"
              << R"( |  _ \(_)___ _   _  __ _   / ___|___   __| | ___ )" << "\n"
              << R"( | |_) | / __| | | |/ _` | | |   / _ \ / _` |/ _ \)" << "\n"
              << R"( |  __/| \__ \ |_| | (_| | | |__| (_) | (_| |  __|)" << "\n"
              << R"( |_|   |_|___/\__, |\__,_|  \____\___/ \__,_|\___|)" << "\n"
              << R"(              |___/                                 )" << "\n"
              << clr::reset
              << clr::dim << "                                              v0.1.1" << clr::reset << "\n"
              << "\n"
              << "  " << clr::dim << "Local AI coding assistant" << clr::reset
              << "  " << clr::white << clr::bold << "Hey, " << username << "!" << clr::reset
              << "\n\n";
    std::cout << clr::dim;
    cfg.print(model_extra);
    std::cout << clr::reset
              << "\n"
              << clr::dim
              << "  " << lang::S().cmd_hint << "\n"
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

    json state = json::object();
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
    const auto& L = lang::S();
    std::cout << clr::white << clr::bold << "● " << clr::reset
              << L.feedback_prefix << model << L.feedback_suffix << " "
              << clr::dim << L.feedback_optional << clr::reset << "\n";

    std::string choice = ui::select("", {L.fb_bad, L.fb_fine, L.fb_good, L.fb_vg, L.fb_dismiss});

    if (choice == L.fb_dismiss || choice.empty()) return;
    if      (choice == L.fb_bad)  std::cout << clr::dim   << "  " << L.fb_bad_msg  << "\n" << clr::reset;
    else if (choice == L.fb_fine) std::cout << clr::dim   << "  " << L.fb_fine_msg << "\n" << clr::reset;
    else if (choice == L.fb_good) std::cout << clr::green << "  " << L.fb_good_msg << "\n" << clr::reset;
    else if (choice == L.fb_vg)   std::cout << clr::green << "  " << L.fb_vg_msg   << "\n" << clr::reset;
    std::cout << "\n";
}

// Pick a session interactively when --resume is given without an ID
static std::string pick_session() {
    auto sessions = Session::list_all();
    if (sessions.empty()) {
        std::cout << clr::dim << "  " << lang::S().no_sessions << "\n\n" << clr::reset;
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
              << lang::S().resume_which << "\n";
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
    if (getuid() == 0) {
        std::cerr << "\n"
                  << "\033[31m\033[1m" << "  ✗ " << "\033[0m"
                  << "\033[1m" << "Запуск от root запрещён." << "\033[0m" << "\n"
                  << "\033[2m"  << "  Не надо быть дегенератом. Запусти как обычный пользователь.\n"
                  << "\033[0m" << "\n";
        return 1;
    }

    Config cfg      = Config::load();
    lang::set(cfg.lang);
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

    // Fetch model info from server (best-effort — silent on failure)
    std::string model_extra;
    {
        AIClient tmp(cfg.host, cfg.port, cfg.model);
        if (auto info = tmp.fetch_model_info()) {
            if (!info->param_size.empty())   model_extra  = info->param_size;
            if (!info->quantization.empty()) model_extra += "  " + info->quantization;
        }
    }

    git_init_if_needed();
    print_banner(cfg, username, model_extra);

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
                std::cout << clr::green << "  ● " << lang::S().resumed << ": " << clr::reset
                          << session.id << clr::dim
                          << std::format("  ({} messages)\n\n", user_msgs)
                          << clr::reset;
            } catch (const std::exception& e) {
                const auto& L = lang::S();
                std::cout << clr::dim << "  " << L.session_load_fail
                          << e.what() << L.starting_fresh << "\n\n" << clr::reset;
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
            std::cout << lang::S().bye << ", " << username << "!\n";
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
            std::cout << clr::dim << "  " << lang::S().session_label
                      << session.id << clr::reset << "\n\n";
            continue;
        }
        if (input == "/language") {
            std::string choice = ui::select(lang::S().lang_prompt,
                                            {"English", "Русский", "Deutsch"});
            if      (choice == "English") cfg.lang = "en";
            else if (choice == "Русский") cfg.lang = "ru";
            else if (choice == "Deutsch") cfg.lang = "de";
            else continue;
            lang::set(cfg.lang);
            cfg.save();
            std::cout << clr::dim << "  " << lang::S().lang_saved
                      << clr::reset << "\n\n";
            continue;
        }

        agent.run(input);

        // Auto-save after every response
        session.messages = agent.get_history();
        session.model    = cfg.model;
        session.save();

        git_checkpoint(input);
    }

    return 0;
}
