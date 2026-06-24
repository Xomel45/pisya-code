#pragma once
#include <string>

namespace lang {

enum class Code { En, Ru, De };

struct Strings {
    // ui::select / read_input
    const char* custom_answer;
    const char* cancelled;
    const char* hint_bar;

    // ui::ask_permission
    const char* danger_title;
    const char* perm_allow;
    const char* perm_allow_session; // category appended in parens by caller
    const char* perm_deny;
    const char* perm_prompt;

    // agent
    const char* history_cleared;
    const char* agent_interrupted;

    // main — session picker
    const char* resumed;
    const char* no_sessions;
    const char* resume_which;
    const char* session_load_fail;
    const char* starting_fresh;
    const char* bye;
    const char* cmd_hint;
    const char* session_label;
    const char* help_text;

    // feedback
    const char* feedback_prefix;   // "How is "
    const char* feedback_suffix;   // " doing this session?"
    const char* feedback_optional;
    const char* fb_bad;
    const char* fb_fine;
    const char* fb_good;
    const char* fb_vg;
    const char* fb_dismiss;
    const char* fb_bad_msg;
    const char* fb_fine_msg;
    const char* fb_good_msg;
    const char* fb_vg_msg;

    // /language
    const char* lang_prompt;
    const char* lang_saved;

    // tools — write/bash/path
    const char* overwrite_prompt;       // "File already exists — overwrite?"
    const char* run_cmd;                // "Execute" (bash non-dangerous prefix)
    const char* outside_project_prompt; // "Outside project dir — allow this once?"

    // /rc — remote control
    const char* rc_started;
    const char* rc_security_note;
    const char* rc_stopped;
    const char* rc_not_running;
    const char* rc_failed;

    // -api — external API setup
    const char* api_url_prompt;
    const char* api_key_prompt;
    const char* api_model_prompt;
    const char* api_saved;

    // /provider — provider picker
    const char* provider_prompt;
    const char* provider_local;
    const char* provider_custom;
    const char* host_prompt;
    const char* port_prompt;
};

void           set(Code c);
void           set(const std::string& code); // "en" / "ru" / "de"
Code           current();
std::string    code_str();                   // "en" / "ru" / "de"
const Strings& S();

} // namespace lang
