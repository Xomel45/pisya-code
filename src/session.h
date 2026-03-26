#pragma once
#include "ai_client.h"
#include <string>
#include <vector>

struct SessionMeta {
    std::string id;
    std::string created;   // ISO datetime
    std::string model;
    int         msg_count = 0;
    std::string preview;   // first user message (truncated)
};

struct Session {
    std::string          id;
    std::string          created;
    std::string          model;
    std::vector<Message> messages;

    void save() const;

    static Session      load(const std::string& id);
    static Session      create(const std::string& model);

    static std::vector<SessionMeta> list_all();   // newest first
    static std::string              sessions_dir();
};
