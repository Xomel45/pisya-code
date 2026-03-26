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

    // main — session picker
    const char* resumed;
    const char* no_sessions;
    const char* resume_which;
    const char* session_load_fail;
    const char* starting_fresh;
    const char* bye;
    const char* cmd_hint;
    const char* session_label;

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
};

void           set(Code c);
void           set(const std::string& code); // "en" / "ru" / "de"
Code           current();
std::string    code_str();                   // "en" / "ru" / "de"
const Strings& S();

} // namespace lang
