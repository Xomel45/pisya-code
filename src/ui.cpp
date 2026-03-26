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

// Draw the widget. Returns number of content lines drawn.
int draw_widget(const std::string& text, int cursor_cp, int W) {
    int avail  = W - 3;
    int nlines = content_lines(text, W);

    // Top border
    std::cout << hline(W) << "\n";

    // Content lines
    // We'll iterate codepoint by codepoint filling lines
    int cp = 0;
    int bi = 0;
    int total_cp = utf8_len(text);

    for (int ln = 0; ln < nlines || (ln == 0 && total_cp == 0); ++ln) {
        if (ln == 0) std::cout << GRN << "❯" << RST << "  ";
        else         std::cout << "   ";

        int line_cp = 0;
        int line_start_bi = bi;
        while (line_cp < avail && bi < static_cast<int>(text.size())) {
            unsigned char c = text[bi];
            int extra = utf8_extra(c);
            for (int e = 0; e <= extra && bi < static_cast<int>(text.size()); ++e, ++bi)
                std::cout << text[bi];
            ++cp;
            ++line_cp;
        }
        (void)line_start_bi;
        std::cout << "\n";
        if (total_cp == 0) break;
    }

    // Bottom border
    std::cout << hline(W) << "\n";

    // Hint bar
    std::cout << "  " << DIM
              << "⏵⏵ отправить на enter"
              << "   shift+enter — новая строка"
              << "   ctrl+c — отмена"
              << RST;
    std::cout.flush();

    // Position terminal cursor correctly
    // Calculate which line and column the cursor is on
    int cursor_line = (cursor_cp == 0) ? 0 : cursor_cp / avail;
    int cursor_col  = 3 + cursor_cp % avail;
    // We are currently at: 1(top) + nlines(content) + 1(bottom) + 1(hint) lines below start
    int total_lines_drawn = 1 + nlines + 1 + 1;
    int lines_from_cursor_to_bottom = (1 + nlines - 1 - cursor_line) + 2; // bottom + hint
    if (lines_from_cursor_to_bottom > 0)
        std::cout << "\033[" << lines_from_cursor_to_bottom << "A";
    std::cout << "\r";
    if (cursor_col > 0)
        std::cout << "\033[" << cursor_col << "C";
    std::cout.flush();

    return total_lines_drawn;
}

void clear_widget(int total_lines) {
    // Go to start of widget, clear everything down
    std::cout << "\r\033[" << (total_lines - 1) << "B" // go to last line
              << "\r\033[" << total_lines << "A"        // go up to first line
              << "\033[J";                              // clear to end
}

} // namespace

std::string ui::read_input() {
    std::string buf;
    int cursor_cp = 0; // cursor position in codepoints

    int hist_idx = -1; // -1 = current input, >= 0 = history entry
    std::string saved_buf; // save current input when browsing history

    set_raw(true);

    int W = term_width();
    int prev_total = draw_widget(buf, cursor_cp, W);

    while (true) {
        W = term_width(); // re-read on each keypress (handles resize)
        int k = read_key();

        if (k == KEY_ENTER || k == '\n') {
            // Submit
            set_raw(false);
            clear_widget(prev_total);
            // Print submitted text dimly above response
            std::cout << GRN << "❯" << RST << "  " << DIM << buf << RST << "\n\n";
            if (!buf.empty()) g_history.push_back(buf);
            return buf;

        } else if (k == 3 || k == 4) { // Ctrl+C / Ctrl+D
            set_raw(false);
            clear_widget(prev_total);
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
        clear_widget(prev_total);
        prev_total = draw_widget(buf, cursor_cp, W);
    }
}
