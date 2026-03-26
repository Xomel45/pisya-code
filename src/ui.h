#pragma once
#include <string>
#include <vector>

namespace ui {

// Arrow-key selection menu.
// Always appends "Свой ответ..." as last option (unless already present).
// Returns selected string, or typed custom string, or "" if cancelled.
std::string select(const std::string& prompt,
                   const std::vector<std::string>& options);

// Result of a permission prompt
enum class Perm { Allow, AllowSession, Deny };

// Show permission dialog for a shell command.
// category — human-readable type ("удаление", "скачивание", etc.)
Perm ask_permission(const std::string& cmd, const std::string& category);

// Detect dangerous category from a shell command string.
// Returns "" for safe/unknown commands.
std::string danger_category(const std::string& cmd);

// Multi-line expandable input box with ─ borders and ❯ cursor.
// Supports history (up/down), left/right cursor, UTF-8.
// Returns submitted text, or "" on Ctrl+C / Ctrl+D.
std::string read_input();

} // namespace ui
