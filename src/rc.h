#pragma once
#include "../third_party/json.hpp"
#include <string>
#include <vector>

// Remote control: a small HTTP/SSE server serving a single-page chat UI so
// the session can be driven from a phone on the same LAN.
namespace rc {

// Start the server if not already running (idempotent).
// Returns the URL to open on the phone (with token), or "" on failure.
std::string start();

// Stop the server if running.
void stop();

bool active();

// Push a JSON event to all connected clients (assistant text, tool output,
// status lines, ...). No-op if the server isn't running.
void push_event(const nlohmann::json& event);

// Register a pending select()/permission prompt. Returns its id, or -1 if
// the server isn't running.
int push_prompt(const std::string& title, const std::vector<std::string>& options);

// Non-blocking: returns the chosen option index for `id`, or -1 if the
// phone hasn't answered yet.
int poll_choice(int id);

// Marks the prompt as resolved so connected clients dismiss it.
void resolve_prompt(int id);

// Non-blocking: pops the next queued message sent from the phone, or "" if
// none is pending.
std::string poll_input();

} // namespace rc
