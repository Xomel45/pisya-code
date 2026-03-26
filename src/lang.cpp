#include "lang.h"

namespace lang {

static constexpr Strings EN = {
    .custom_answer      = "Custom answer...",
    .cancelled          = "(cancelled)",
    .hint_bar           = "\u23f5\u23f5 enter to submit   shift+enter \u2014 new line   ctrl+c \u2014 cancel",
    .danger_title       = "Dangerous operation",
    .perm_allow         = "Allow",
    .perm_allow_session = "Allow for this session",
    .perm_deny          = "Cancel",
    .perm_prompt        = "What do we do?",
    .history_cleared    = "[history cleared]",
    .resumed            = "Resumed",
    .no_sessions        = "No saved sessions found. Starting a new one.",
    .resume_which       = "Resume which session?",
    .session_load_fail  = "Could not load session: ",
    .starting_fresh     = " \u2014 starting fresh.",
    .bye                = "Bye",
    .cmd_hint           = "/clear \u2014 clear history | /config \u2014 show config | /language \u2014 change language | /exit \u2014 quit",
    .session_label      = "Session: ",
    .feedback_prefix    = "How is ",
    .feedback_suffix    = " doing this session?",
    .feedback_optional  = "(optional)",
    .fb_bad             = "Bad",
    .fb_fine            = "Fine",
    .fb_good            = "Good",
    .fb_vg              = "Very Good",
    .fb_dismiss         = "Dismiss",
    .fb_bad_msg         = "Got it. Hope things improve.",
    .fb_fine_msg        = "Noted. Let's get to work.",
    .fb_good_msg        = "Glad to hear it!",
    .fb_vg_msg          = "Let's keep it that way!",
    .lang_prompt        = "Choose language",
    .lang_saved         = "Language saved.",
    .overwrite_prompt        = "File already exists — overwrite?",
    .run_cmd                 = "Execute",
    .outside_project_prompt  = "Path is outside the project directory — allow this once?",
};

static constexpr Strings RU = {
    .custom_answer      = "\u0421\u0432\u043e\u0439 \u043e\u0442\u0432\u0435\u0442...",
    .cancelled          = "(\u043e\u0442\u043c\u0435\u043d\u0435\u043d\u043e)",
    .hint_bar           = "\u23f5\u23f5 \u043e\u0442\u043f\u0440\u0430\u0432\u0438\u0442\u044c \u043d\u0430 enter   shift+enter \u2014 \u043d\u043e\u0432\u0430\u044f \u0441\u0442\u0440\u043e\u043a\u0430   ctrl+c \u2014 \u043e\u0442\u043c\u0435\u043d\u0430",
    .danger_title       = "\u041e\u043f\u0430\u0441\u043d\u0430\u044f \u043e\u043f\u0435\u0440\u0430\u0446\u0438\u044f",
    .perm_allow         = "\u0420\u0430\u0437\u0440\u0435\u0448\u0438\u0442\u044c",
    .perm_allow_session = "\u0420\u0430\u0437\u0440\u0435\u0448\u0438\u0442\u044c \u0434\u043b\u044f \u0432\u0441\u0435\u0439 \u0441\u0435\u0441\u0441\u0438\u0438",
    .perm_deny          = "\u041e\u0442\u043c\u0435\u043d\u0430",
    .perm_prompt        = "\u0427\u0442\u043e \u0434\u0435\u043b\u0430\u0435\u043c?",
    .history_cleared    = "[\u0438\u0441\u0442\u043e\u0440\u0438\u044f \u043e\u0447\u0438\u0449\u0435\u043d\u0430]",
    .resumed            = "\u0412\u043e\u0437\u043e\u0431\u043d\u043e\u0432\u043b\u0435\u043d\u043e",
    .no_sessions        = "\u0421\u043e\u0445\u0440\u0430\u043d\u0451\u043d\u043d\u044b\u0445 \u0441\u0435\u0441\u0441\u0438\u0439 \u043d\u0435\u0442. \u041d\u0430\u0447\u0438\u043d\u0430\u0435\u043c \u043d\u043e\u0432\u0443\u044e.",
    .resume_which       = "\u041f\u0440\u043e\u0434\u043e\u043b\u0436\u0438\u0442\u044c \u043a\u0430\u043a\u0443\u044e \u0441\u0435\u0441\u0441\u0438\u044e?",
    .session_load_fail  = "\u041d\u0435 \u0443\u0434\u0430\u043b\u043e\u0441\u044c \u0437\u0430\u0433\u0440\u0443\u0437\u0438\u0442\u044c \u0441\u0435\u0441\u0441\u0438\u044e: ",
    .starting_fresh     = " \u2014 \u043d\u0430\u0447\u0438\u043d\u0430\u0435\u043c \u0437\u0430\u043d\u043e\u0432\u043e.",
    .bye                = "\u041f\u043e\u043a\u0430",
    .cmd_hint           = "/clear \u2014 \u043e\u0447\u0438\u0441\u0442\u0438\u0442\u044c \u0438\u0441\u0442\u043e\u0440\u0438\u044e | /config \u2014 \u043a\u043e\u043d\u0444\u0438\u0433 | /language \u2014 \u044f\u0437\u044b\u043a | /exit \u2014 \u0432\u044b\u0445\u043e\u0434",
    .session_label      = "\u0421\u0435\u0441\u0441\u0438\u044f: ",
    .feedback_prefix    = "\u041a\u0430\u043a ",
    .feedback_suffix    = " \u0441\u043f\u0440\u0430\u0432\u043b\u044f\u0435\u0442\u0441\u044f \u0432 \u044d\u0442\u043e\u0439 \u0441\u0435\u0441\u0441\u0438\u0438?",
    .feedback_optional  = "(\u043d\u0435\u043e\u0431\u044f\u0437\u0430\u0442\u0435\u043b\u044c\u043d\u043e)",
    .fb_bad             = "\u041f\u043b\u043e\u0445\u043e",
    .fb_fine            = "\u041d\u043e\u0440\u043c\u0430\u043b\u044c\u043d\u043e",
    .fb_good            = "\u0425\u043e\u0440\u043e\u0448\u043e",
    .fb_vg              = "\u041e\u0447\u0435\u043d\u044c \u0445\u043e\u0440\u043e\u0448\u043e",
    .fb_dismiss         = "\u041f\u0440\u043e\u043f\u0443\u0441\u0442\u0438\u0442\u044c",
    .fb_bad_msg         = "\u041f\u043e\u043d\u044f\u043b. \u041d\u0430\u0434\u0435\u0435\u043c\u0441\u044f, \u0441\u0442\u0430\u043d\u0435\u0442 \u043b\u0443\u0447\u0448\u0435.",
    .fb_fine_msg        = "\u041f\u0440\u0438\u043d\u044f\u0442\u043e. \u0420\u0430\u0431\u043e\u0442\u0430\u0435\u043c.",
    .fb_good_msg        = "\u0420\u0430\u0434\u044b \u0441\u043b\u044b\u0448\u0430\u0442\u044c!",
    .fb_vg_msg          = "\u0422\u0430\u043a \u0438 \u043f\u0440\u043e\u0434\u043e\u043b\u0436\u0438\u043c!",
    .lang_prompt        = "\u0412\u044b\u0431\u0435\u0440\u0438 \u044f\u0437\u044b\u043a",
    .lang_saved         = "\u042f\u0437\u044b\u043a \u0441\u043e\u0445\u0440\u0430\u043d\u0451\u043d.",
    .overwrite_prompt        = "\u0424\u0430\u0439\u043b \u0443\u0436\u0435 \u0441\u0443\u0449\u0435\u0441\u0442\u0432\u0443\u0435\u0442 \u2014 \u043f\u0435\u0440\u0435\u0437\u0430\u043f\u0438\u0441\u0430\u0442\u044c?",
    .run_cmd                 = "\u0412\u044b\u043f\u043e\u043b\u043d\u0438\u0442\u044c",
    .outside_project_prompt  = "\u041f\u0443\u0442\u044c \u0437\u0430 \u043f\u0440\u0435\u0434\u0435\u043b\u0430\u043c\u0438 \u043f\u0440\u043e\u0435\u043a\u0442\u0430 \u2014 \u0440\u0430\u0437\u0440\u0435\u0448\u0438\u0442\u044c \u043e\u0434\u0438\u043d \u0440\u0430\u0437?",
};

static constexpr Strings DE = {
    .custom_answer      = "Eigene Antwort...",
    .cancelled          = "(abgebrochen)",
    .hint_bar           = "\u23f5\u23f5 Enter zum Senden   Shift+Enter \u2014 neue Zeile   Strg+C \u2014 abbrechen",
    .danger_title       = "Gef\u00e4hrliche Operation",
    .perm_allow         = "Erlauben",
    .perm_allow_session = "F\u00fcr diese Sitzung erlauben",
    .perm_deny          = "Abbrechen",
    .perm_prompt        = "Was tun wir?",
    .history_cleared    = "[Verlauf gel\u00f6scht]",
    .resumed            = "Fortgesetzt",
    .no_sessions        = "Keine gespeicherten Sitzungen. Neue Sitzung wird gestartet.",
    .resume_which       = "Welche Sitzung fortsetzen?",
    .session_load_fail  = "Sitzung konnte nicht geladen werden: ",
    .starting_fresh     = " \u2014 starte neu.",
    .bye                = "Tsch\u00fcss",
    .cmd_hint           = "/clear \u2014 Verlauf l\u00f6schen | /config \u2014 Konfiguration | /language \u2014 Sprache | /exit \u2014 beenden",
    .session_label      = "Sitzung: ",
    .feedback_prefix    = "Wie macht sich ",
    .feedback_suffix    = " in dieser Sitzung?",
    .feedback_optional  = "(optional)",
    .fb_bad             = "Schlecht",
    .fb_fine            = "Ok",
    .fb_good            = "Gut",
    .fb_vg              = "Sehr gut",
    .fb_dismiss         = "\u00dcberspringen",
    .fb_bad_msg         = "Verstanden. Hoffentlich wird es besser.",
    .fb_fine_msg        = "Notiert. Weiter geht's.",
    .fb_good_msg        = "Sch\u00f6n zu h\u00f6ren!",
    .fb_vg_msg          = "So soll es bleiben!",
    .lang_prompt        = "Sprache w\u00e4hlen",
    .lang_saved         = "Sprache gespeichert.",
    .overwrite_prompt        = "Datei existiert bereits \u2014 \u00fcberschreiben?",
    .run_cmd                 = "Ausf\u00fchren",
    .outside_project_prompt  = "Pfad au\u00dferhalb des Projektverzeichnisses \u2014 einmalig erlauben?",
};

static Code g_code = Code::En;

void set(Code c) { g_code = c; }

void set(const std::string& code) {
    if      (code == "ru") g_code = Code::Ru;
    else if (code == "de") g_code = Code::De;
    else                   g_code = Code::En;
}

Code current() { return g_code; }

std::string code_str() {
    switch (g_code) {
        case Code::Ru: return "ru";
        case Code::De: return "de";
        default:       return "en";
    }
}

const Strings& S() {
    switch (g_code) {
        case Code::Ru: return RU;
        case Code::De: return DE;
        default:       return EN;
    }
}

} // namespace lang
