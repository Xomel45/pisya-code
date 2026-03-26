#include "ui.h"
#include <algorithm>
#include <iostream>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

// ── terminal raw mode ─────────────────────────────────────────────────────────

namespace {

constexpr int KEY_UP    = 1000;
constexpr int KEY_DOWN  = 1001;
constexpr int KEY_LEFT  = 1002;
constexpr int KEY_RIGHT = 1003;
constexpr int KEY_HOME  = 1004;
constexpr int KEY_END   = 1005;
constexpr int KEY_ENTER = '\r';

termios g_old_termios{};

void set_raw(bool enable) {
    if (enable) {
        tcgetattr(STDIN_FILENO, &g_old_termios);
        termios raw = g_old_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    } else {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_termios);
    }
}

int read_key() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
    if (c != '\033') return static_cast<int>(static_cast<unsigned char>(c));

    char seq[3] = {};
    if (read(STDIN_FILENO, &seq[0], 1) <= 0) return '\033';
    if (read(STDIN_FILENO, &seq[1], 1) <= 0) return '\033';
    if (seq[0] == '[') {
        if (seq[1] == 'A') return KEY_UP;
        if (seq[1] == 'B') return KEY_DOWN;
        if (seq[1] == 'C') return KEY_RIGHT;
        if (seq[1] == 'D') return KEY_LEFT;
        if (seq[1] == 'H') return KEY_HOME;
        if (seq[1] == 'F') return KEY_END;
        if (seq[1] >= '1' && seq[1] <= '8') {
            read(STDIN_FILENO, &seq[2], 1);
            if (seq[1] == '1' && seq[2] == '~') return KEY_HOME;
            if (seq[1] == '4' && seq[2] == '~') return KEY_END;
        }
    }
    return '\033';
}

// ── UTF-8 helpers ─────────────────────────────────────────────────────────────

// Number of continuation bytes after a lead byte
int utf8_extra(unsigned char c) {
    if (c < 0x80) return 0;
    if (c < 0xE0) return 1;
    if (c < 0xF0) return 2;
    return 3;
}

// Read a complete UTF-8 char into buf (lead byte already read as k).
// Returns the char as string.
std::string read_utf8(int lead) {
    std::string s;
    s += static_cast<char>(lead);
    int extra = utf8_extra(static_cast<unsigned char>(lead));
    for (int i = 0; i < extra; ++i) {
        char cont = 0;
        if (read(STDIN_FILENO, &cont, 1) <= 0) break;
        s += cont;
    }
    return s;
}

// Display width of a UTF-8 string (codepoints, no CJK wide chars)
int utf8_len(const std::string& s) {
    int len = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++len; // count only lead bytes
    return len;
}

// Byte index of the N-th codepoint in s (N can equal utf8_len for end)
int utf8_byte_offset(const std::string& s, int codepoint_idx) {
    int cp = 0, bi = 0;
    while (bi < static_cast<int>(s.size()) && cp < codepoint_idx) {
        unsigned char c = s[bi];
        bi += 1 + utf8_extra(c);
        ++cp;
    }
    return bi;
}

// ── terminal size ─────────────────────────────────────────────────────────────

int term_width() {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return static_cast<int>(ws.ws_col);
    return 80;
}

// ── border line ───────────────────────────────────────────────────────────────

std::string hline(int w) {
    std::string s;
    s.reserve(static_cast<size_t>(w) * 3);
    for (int i = 0; i < w; ++i) s += "─";
    return s;
}

// ANSI helpers
constexpr auto RST  = "\033[0m";
constexpr auto DIM  = "\033[2m";
constexpr auto BOLD = "\033[1m";
constexpr auto GRN  = "\033[32m";
constexpr auto RED  = "\033[31m";
constexpr auto YEL  = "\033[33m";
constexpr auto WHT  = "\033[97m";

void draw_menu(const std::vector<std::string>& opts, int sel) {
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        if (i == sel)
            std::cout << "  " << GRN << "❯" << RST << " " << BOLD << WHT
                      << opts[i] << RST << "\n";
        else
            std::cout << "    " << DIM << opts[i] << RST << "\n";
    }
}

} // namespace

// ── ui::select ────────────────────────────────────────────────────────────────

std::string ui::select(const std::string& prompt,
                        const std::vector<std::string>& options) {
    std::vector<std::string> opts = options;
    bool has_custom = std::ranges::any_of(opts, [](const std::string& o) {
        return o == "Свой ответ...";
    });
    if (!has_custom) opts.push_back("Свой ответ...");

    int n   = static_cast<int>(opts.size());
    int sel = 0;

    std::cout << "\n  " << BOLD << WHT << prompt << RST << "\n";
    draw_menu(opts, sel);

    set_raw(true);

    while (true) {
        int k = read_key();

        if (k == KEY_UP)   sel = (sel - 1 + n) % n;
        else if (k == KEY_DOWN) sel = (sel + 1) % n;
        else if (k == KEY_ENTER || k == '\n') {
            set_raw(false);

            // Clear menu lines
            std::cout << "\033[" << n << "A\033[J";

            if (sel == n - 1 && !has_custom) {
                // Custom text input
                std::cout << "  " << GRN << "❯" << RST << " ";
                std::string custom;
                std::getline(std::cin, custom);
                std::cout << "\n";
                return custom;
            }

            std::cout << "  " << GRN << "❯" << RST << " "
                      << BOLD << opts[sel] << RST << "\n\n";
            return opts[sel];
        } else if (k == '\033' || k == 'q') {
            set_raw(false);
            std::cout << "\033[" << n << "A\033[J"
                      << "  " << DIM << "(отменено)" << RST << "\n\n";
            return "";
        }

        // Redraw
        std::cout << "\033[" << n << "A";
        draw_menu(opts, sel);
        std::cout.flush();
    }
}

// ── ui::danger_category ───────────────────────────────────────────────────────

std::string ui::danger_category(const std::string& cmd) {
    // Helpers
    auto starts = [&](const std::string& prefix) {
        return cmd.starts_with(prefix);
    };
    auto contains = [&](const std::string& sub) {
        return cmd.find(sub) != std::string::npos;
    };

    if (contains("sudo"))                                      return "sudo";
    if (starts("rm ") || starts("rm\t") ||
        contains(" rm ") || contains(" rmdir"))                return "удаление файлов";
    if (starts("curl ") || starts("wget "))                    return "скачивание из сети";
    if (starts("apt")  || starts("apt-get") ||
        starts("yum")  || starts("dnf")     ||
        starts("pacman") || starts("pip")   ||
        starts("pip3") || starts("npm")     ||
        starts("cargo install") || starts("brew"))             return "пакетный менеджер";
    if (starts("sh ") || starts("bash ") ||
        starts("zsh ") || starts("python ") ||
        starts("python3 ") || starts("node ") ||
        starts("ruby ") || starts("perl "))                    return "выполнение скрипта";
    if (starts("dd ") || starts("mkfs") ||
        starts("fdisk") || starts("parted"))                   return "дисковая операция";
    if (starts("chmod") || starts("chown"))                    return "права доступа";
    if (contains("> /") || contains(">> /"))                   return "запись в системный файл";

    return ""; // safe / unknown
}

// ── ui::ask_permission ────────────────────────────────────────────────────────

ui::Perm ui::ask_permission(const std::string& cmd, const std::string& category) {
    std::cout << "\n  " << YEL << BOLD << "⚠ " << RST
              << YEL << "Опасная операция" << RST;
    if (!category.empty())
        std::cout << DIM << " — " << category << RST;
    std::cout << "\n"
              << DIM << "  $ " << RST << cmd << "\n";

    std::vector<std::string> opts = {
        "Разрешить",
        "Разрешить для всей сессии (" + category + ")",
        "Отмена"
    };

    // Reuse select() but map result back to enum
    std::string choice = ui::select("Что делаем?", opts);
    // Remove the custom answer that select() always appends
    if (choice == "Разрешить")                              return Perm::Allow;
    if (choice.starts_with("Разрешить для всей сессии"))   return Perm::AllowSession;
    return Perm::Deny;
}

// ── ui::read_input ────────────────────────────────────────────────────────────

namespace {

// Input history (shared across calls)
std::vector<std::string> g_history;

// Compute how many display lines the content occupies given terminal width.
// Prefix for line 0: "❯  " (3 chars), for rest: "   " (3 chars).
int content_lines(const std::string& text, int W) {
    int available = W - 3; // same for first and continuation lines
    if (available <= 0) available = 1;
    int cps = utf8_len(text);
    if (cps == 0) return 1;
    return (cps + available - 1) / available;
}

// Draw the widget.
// Saves cursor position at widget start (\033[s) so clear_widget can restore it.
// Returns 0 (unused by caller, kept for API compat).
int draw_widget(const std::string& text, int cursor_cp, int W) {
    int avail    = W - 3;
    int nlines   = content_lines(text, W);
    int total_cp = utf8_len(text);

    // Save cursor position — clear_widget will restore here and clear down
    std::cout << "\033[s";

    // Top border
    std::cout << hline(W) << "\n";

    // Content lines
    int bi = 0;
    for (int ln = 0; ln < nlines || (ln == 0 && total_cp == 0); ++ln) {
        if (ln == 0) std::cout << GRN << "❯" << RST << "  ";
        else         std::cout << "   ";

        int line_cp = 0;
        while (line_cp < avail && bi < static_cast<int>(text.size())) {
            unsigned char c = text[bi];
            int extra = utf8_extra(c);
            for (int e = 0; e <= extra && bi < static_cast<int>(text.size()); ++e, ++bi)
                std::cout << text[bi];
            ++line_cp;
        }
        std::cout << "\n";
        if (total_cp == 0) break;
    }

    // Bottom border
    std::cout << hline(W) << "\n";

    // Hint bar (with \n so cursor lands on known line after widget)
    std::cout << "  " << DIM
              << "⏵⏵ отправить на enter"
              << "   shift+enter — новая строка"
              << "   ctrl+c — отмена"
              << RST << "\n";

    // Position terminal cursor at the text cursor location inside the widget
    // Layout: line 0 = top border, lines 1..nlines = content, nlines+1 = bottom, nlines+2 = hint
    // After hint \n cursor is at line nlines+3 (below widget).
    int cursor_line = (avail > 0) ? (cursor_cp / avail) : 0;
    int cursor_col  = 3 + (avail > 0 ? cursor_cp % avail : 0);
    // Lines to go up from below-widget to the correct content line:
    //   (nlines+3) - (1 + cursor_line) = nlines + 2 - cursor_line
    int up = nlines + 2 - cursor_line;
    if (up > 0) std::cout << "\033[" << up << "A";
    std::cout << "\r";
    if (cursor_col > 0) std::cout << "\033[" << cursor_col << "C";
    std::cout.flush();

    return 0;
}

void clear_widget(int /*unused*/) {
    // Restore to the position saved by draw_widget, clear everything below
    std::cout << "\033[u\033[J";
    std::cout.flush();
}

} // namespace

std::string ui::read_input() {
    std::string buf;
    int cursor_cp = 0; // cursor position in codepoints

    int hist_idx = -1; // -1 = current input, >= 0 = history entry
    std::string saved_buf; // save current input when browsing history

    set_raw(true);

    int W          = term_width();
    int prev_cursor = draw_widget(buf, cursor_cp, W); // cursor_at_line returned

    while (true) {
        W = term_width(); // re-read on each keypress (handles resize)
        int k = read_key();

        if (k == KEY_ENTER || k == '\n') {
            // Submit
            set_raw(false);
            clear_widget(prev_cursor);
            // Print submitted text dimly above response
            std::cout << GRN << "❯" << RST << "  " << DIM << buf << RST << "\n\n";
            if (!buf.empty()) g_history.push_back(buf);
            return buf;

        } else if (k == 3 || k == 4) { // Ctrl+C / Ctrl+D
            set_raw(false);
            clear_widget(prev_cursor);
            std::cout << DIM << "  (отменено)" << RST << "\n\n";
            return "";

        } else if (k == 127 || k == 8) { // Backspace
            if (cursor_cp > 0) {
                // Remove codepoint before cursor
                int before = utf8_byte_offset(buf, cursor_cp - 1);
                int at     = utf8_byte_offset(buf, cursor_cp);
                buf.erase(before, at - before);
                --cursor_cp;
            }

        } else if (k == KEY_LEFT) {
            if (cursor_cp > 0) --cursor_cp;

        } else if (k == KEY_RIGHT) {
            if (cursor_cp < utf8_len(buf)) ++cursor_cp;

        } else if (k == KEY_HOME) {
            cursor_cp = 0;

        } else if (k == KEY_END) {
            cursor_cp = utf8_len(buf);

        } else if (k == KEY_UP) {
            // Navigate history backwards
            if (g_history.empty()) continue;
            if (hist_idx == -1) {
                saved_buf = buf;
                hist_idx  = static_cast<int>(g_history.size()) - 1;
            } else if (hist_idx > 0) {
                --hist_idx;
            }
            buf       = g_history[hist_idx];
            cursor_cp = utf8_len(buf);

        } else if (k == KEY_DOWN) {
            if (hist_idx == -1) continue;
            if (hist_idx < static_cast<int>(g_history.size()) - 1) {
                ++hist_idx;
                buf = g_history[hist_idx];
            } else {
                hist_idx = -1;
                buf      = saved_buf;
            }
            cursor_cp = utf8_len(buf);

        } else if (k >= 0x20 && k < 0x80) {
            // ASCII printable
            int bi = utf8_byte_offset(buf, cursor_cp);
            buf.insert(bi, 1, static_cast<char>(k));
            ++cursor_cp;

        } else if (k >= 0xC0) {
            // UTF-8 multibyte lead byte
            std::string ch = read_utf8(k);
            int bi = utf8_byte_offset(buf, cursor_cp);
            buf.insert(bi, ch);
            ++cursor_cp;
        }

        // Redraw
        clear_widget(prev_cursor);
        prev_cursor = draw_widget(buf, cursor_cp, W);
    }
}
