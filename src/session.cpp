#include "session.h"
#include "../third_party/json.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using json   = nlohmann::json;

// ── paths ─────────────────────────────────────────────────────────────────────

std::string Session::sessions_dir() {
    const char* home = getenv("HOME");
    return std::string(home ? home : ".") + "/.pisya/sessions";
}

static std::string session_path(const std::string& id) {
    return Session::sessions_dir() + "/" + id + ".json";
}

// ── serialization helpers ─────────────────────────────────────────────────────

static json message_to_json(const Message& m) {
    json j;
    j["role"]    = m.role;
    j["content"] = m.content;
    if (!m.tool_call_id.empty())          j["tool_call_id"] = m.tool_call_id;
    if (!m.tool_calls.is_null() && !m.tool_calls.empty())
                                          j["tool_calls"]   = m.tool_calls;
    return j;
}

static Message message_from_json(const json& j) {
    Message m;
    m.role    = j.value("role",    "");
    m.content = j.value("content", "");
    if (j.contains("tool_call_id")) m.tool_call_id = j["tool_call_id"].get<std::string>();
    if (j.contains("tool_calls"))   m.tool_calls   = j["tool_calls"];
    return m;
}

// ── create ────────────────────────────────────────────────────────────────────

Session Session::create(const std::string& model) {
    // ID = YYYY-MM-DD_HH-MM-SS
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);

    Session s;
    s.id      = buf;
    s.created = buf;
    s.model   = model;
    return s;
}

// ── save ──────────────────────────────────────────────────────────────────────

void Session::save() const {
    fs::create_directories(sessions_dir());

    json j;
    j["id"]      = id;
    j["created"] = created;
    j["model"]   = model;
    j["messages"] = json::array();
    for (const auto& m : messages)
        j["messages"].push_back(message_to_json(m));

    std::ofstream f(session_path(id));
    f << j.dump(2);
}

// ── load ──────────────────────────────────────────────────────────────────────

Session Session::load(const std::string& id) {
    std::ifstream f(session_path(id));
    if (!f) throw std::runtime_error("Session '" + id + "' not found");

    json j = json::parse(f);
    Session s;
    s.id      = j.value("id",      id);
    s.created = j.value("created", "");
    s.model   = j.value("model",   "");
    for (const auto& m : j["messages"])
        s.messages.push_back(message_from_json(m));
    return s;
}

// ── list_all ──────────────────────────────────────────────────────────────────

std::vector<SessionMeta> Session::list_all() {
    std::vector<SessionMeta> result;
    std::string dir = sessions_dir();
    if (!fs::exists(dir)) return result;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        try {
            std::ifstream f(entry.path());
            json j = json::parse(f);

            SessionMeta m;
            m.id        = j.value("id",      "");
            m.created   = j.value("created", "");
            m.model     = j.value("model",   "");

            // Count messages (excluding system)
            for (const auto& msg : j["messages"])
                if (msg.value("role","") != "system") ++m.msg_count;

            // First user message as preview
            for (const auto& msg : j["messages"]) {
                if (msg.value("role","") == "user") {
                    std::string content = msg.value("content","");
                    m.preview = content.size() > 50
                        ? content.substr(0, 50) + "…"
                        : content;
                    break;
                }
            }

            result.push_back(std::move(m));
        } catch (...) {}
    }

    // Sort newest first (IDs are timestamp strings → lexicographic = chronological)
    std::sort(result.begin(), result.end(),
              [](const SessionMeta& a, const SessionMeta& b) {
                  return a.id > b.id;
              });
    return result;
}
