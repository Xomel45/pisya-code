#define CPPHTTPLIB_NO_EXCEPTIONS
#include "ai_client.h"
#include "lang.h"
#include "../third_party/httplib.h"
#include <format>
#include <random>
#include <stdexcept>
#include <string_view>

static std::string random_conn_error(const std::string& host, int port) {
    static constexpr std::string_view EN[] = {
        "Can't reach {}:{} — did you forget to start Ollama?",
        "{}:{} is not responding — is the AI server running?",
        "Hello? {}:{}? Anyone home?",
        "{}:{} ghosted us — server down?",
        "No one's picking up at {}:{} — start the AI server first.",
        "{}:{} is giving us the silent treatment.",
        "The void at {}:{} stares back — server not found.",
        "{}:{} — connection refused. Classic.",
        "Knocked on {}:{}, nobody answered.",
        "{}:{} is off the grid. Launch the server and try again.",
    };
    static constexpr std::string_view RU[] = {
        "{}:{} не отвечает — ты запустил Ollama?",
        "Стучимся в {}:{}, а там тишина. Сервер живой?",
        "Алло, {}:{}? Есть кто дома?",
        "{}:{} нас проигнорировал — сервер упал?",
        "{}:{} молчит как партизан. Запусти сервер.",
        "{}:{} — соединение отклонено. Ожидаемо.",
        "В {}:{} никто не берёт трубку.",
        "{}:{} ушёл в офлайн. Сначала запусти сервер.",
        "Постучались в {}:{} — никого нет.",
        "{}:{} недоступен. Ollama запущен?",
    };
    static constexpr std::string_view DE[] = {
        "{}:{} antwortet nicht — hast du Ollama gestartet?",
        "Niemand zuhause bei {}:{}? Server läuft?",
        "Hallo? {}:{}? Ist da jemand?",
        "{}:{} hat uns ignoriert — Server abgestürzt?",
        "{}:{} schweigt wie ein Grab. Starte den Server.",
        "{}:{} — Verbindung verweigert. Natürlich.",
        "Bei {}:{} hebt niemand ab.",
        "{}:{} ist offline. Erst den Server starten.",
        "An {}:{} geklopft — keine Antwort.",
        "{}:{} nicht erreichbar. Läuft Ollama?",
    };

    static std::mt19937 rng{std::random_device{}()};

    std::string_view fmt;
    switch (lang::current()) {
        case lang::Code::Ru: {
            static std::uniform_int_distribution<size_t> d{0, std::size(RU) - 1};
            fmt = RU[d(rng)]; break;
        }
        case lang::Code::De: {
            static std::uniform_int_distribution<size_t> d{0, std::size(DE) - 1};
            fmt = DE[d(rng)]; break;
        }
        default: {
            static std::uniform_int_distribution<size_t> d{0, std::size(EN) - 1};
            fmt = EN[d(rng)]; break;
        }
    }
    return std::vformat(fmt, std::make_format_args(host, port));
}

AIClient::AIClient(const std::string& host, int port, const std::string& model)
    : host_(host), port_(port), model_(model) {}

std::optional<AIClient::ModelInfo> AIClient::fetch_model_info() const {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(5);
    cli.set_write_timeout(5);

    nlohmann::json body = {{"name", model_}};
    auto res = cli.Post("/api/show", body.dump(), "application/json");

    if (!res || res->status != 200) return std::nullopt;

    try {
        auto j       = nlohmann::json::parse(res->body);
        auto details = j.value("details", nlohmann::json::object());
        ModelInfo info;
        info.param_size   = details.value("parameter_size",    "");
        info.quantization = details.value("quantization_level", "");
        if (info.param_size.empty() && info.quantization.empty())
            return std::nullopt;
        return info;
    } catch (...) {
        return std::nullopt;
    }
}

nlohmann::json AIClient::build_request(const std::vector<Message>& messages,
                                        const nlohmann::json& tools) const {
    using json = nlohmann::json;

    json msgs = json::array();
    for (const auto& m : messages) {
        json obj;
        obj["role"]    = m.role;
        obj["content"] = m.content;

        if (!m.tool_call_id.empty())
            obj["tool_call_id"] = m.tool_call_id;

        if (!m.tool_calls.is_null() && !m.tool_calls.empty())
            obj["tool_calls"] = m.tool_calls;

        msgs.push_back(obj);
    }

    json req;
    req["model"]    = model_;
    req["messages"] = msgs;
    req["tools"]    = tools;
    return req;
}

nlohmann::json AIClient::chat(const std::vector<Message>& messages,
                               const nlohmann::json& tools) {
    httplib::Client cli(host_, port_);
    cli.set_read_timeout(300); // 5 min — large models can be slow
    cli.set_write_timeout(30);

    nlohmann::json req = build_request(messages, tools);
    std::string body   = req.dump();

    auto res = cli.Post("/v1/chat/completions", body, "application/json");

    if (!res) {
        throw std::runtime_error(random_conn_error(host_, port_));
    }
    if (res->status != 200) {
        throw std::runtime_error(std::format(
            "AI server returned HTTP {} : {}", res->status, res->body));
    }

    return nlohmann::json::parse(res->body);
}
