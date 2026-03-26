#define CPPHTTPLIB_NO_EXCEPTIONS
#include "ai_client.h"
#include "../third_party/httplib.h"
#include <format>
#include <iostream>
#include <stdexcept>

AIClient::AIClient(const std::string& host, int port, const std::string& model)
    : host_(host), port_(port), model_(model) {}

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
        throw std::runtime_error(std::format(
            "Connection failed to {}:{} — is the AI server running?",
            host_, port_));
    }
    if (res->status != 200) {
        throw std::runtime_error(std::format(
            "AI server returned HTTP {} : {}", res->status, res->body));
    }

    return nlohmann::json::parse(res->body);
}
