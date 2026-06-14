#include "rc.h"
#include "../third_party/httplib.h"
#include <arpa/inet.h>
#include <condition_variable>
#include <deque>
#include <ifaddrs.h>
#include <iomanip>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <random>
#include <sstream>
#include <sys/random.h>
#include <thread>

namespace {

constexpr size_t MAX_EVENTS = 300;

std::mutex              g_mutex;
std::condition_variable g_cv;

std::unique_ptr<httplib::Server> g_server;
std::thread                      g_thread;
std::string                      g_token;
std::string                      g_host_ip;
int                               g_port = 0;

// Recent events kept for SSE replay. g_events[i] corresponds to global
// sequence number (g_events_base + i); g_events_total is the next sequence
// number to be assigned.
std::deque<std::string> g_events;
size_t                   g_events_base  = 0;
size_t                   g_events_total = 0;

// Messages typed on the phone, consumed by ui::read_input().
std::deque<std::string> g_input_queue;

// Pending select()/permission prompts.
struct PromptState { int choice = -1; };
std::map<int, PromptState> g_prompts;
int                         g_next_prompt_id = 1;

// Caller must hold g_mutex.
void push_event_locked(const nlohmann::json& event) {
    g_events.push_back(event.dump());
    ++g_events_total;
    if (g_events.size() > MAX_EVENTS) {
        g_events.pop_front();
        ++g_events_base;
    }
    g_cv.notify_all();
}

std::string gen_token() {
    unsigned char buf[16];
    if (getrandom(buf, sizeof(buf), 0) != static_cast<ssize_t>(sizeof(buf))) {
        std::random_device rd;
        for (auto& b : buf) b = static_cast<unsigned char>(rd());
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : buf) oss << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

// Constant-time comparison to avoid LAN timing side-channels on the token check.
bool token_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::string detect_lan_ip() {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) return "localhost";

    std::string result = "localhost";
    for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (std::string(ifa->ifa_name) == "lo") continue;

        char buf[INET_ADDRSTRLEN];
        auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
        if (std::string(buf) == "127.0.0.1") continue;

        result = buf;
        break;
    }
    freeifaddrs(ifaddr);
    return result;
}

// ── embedded chat UI ─────────────────────────────────────────────────────────

const char* PAGE_HTML = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>pisya remote</title>
<style>
  :root { --orange:#ff8700; --bg:#0d0d0d; --dim:#888; --fg:#e6e6e6; }
  * { box-sizing: border-box; }
  html,body { margin:0; height:100%; background:var(--bg); color:var(--fg);
              font-family: ui-monospace, "SF Mono", Menlo, Consolas, monospace; }
  body { display:flex; flex-direction:column; height:100vh; }
  header { padding:10px 14px; border-bottom:1px solid #333; display:flex; align-items:center; gap:8px; }
  header .dot { width:8px; height:8px; border-radius:50%; background:#3c3; }
  header b { color: var(--orange); }
  #log { flex:1; overflow-y:auto; padding:10px 14px; display:flex; flex-direction:column; gap:10px; }
  .msg { white-space:pre-wrap; word-break:break-word; line-height:1.4; font-size:14px; }
  .msg.user .label { color:var(--orange); font-weight:bold; }
  .msg.text .label { color:#fff; font-weight:bold; }
  .msg.error { color:#f55; }
  .msg.status { color:var(--dim); font-size:12px; }
  .msg.tool { color:var(--dim); font-size:12px; border-left:2px solid #444; padding-left:8px; }
  .msg.tool summary { cursor:pointer; color:#9cf; }
  .msg pre { background:#161616; padding:6px 8px; border-radius:4px; overflow-x:auto; white-space:pre-wrap; }
  .msg code { background:#161616; padding:1px 4px; border-radius:3px; }
  .prompt { border:1px solid var(--orange); border-radius:6px; padding:8px; }
  .prompt .opts { display:flex; gap:6px; flex-wrap:wrap; margin-top:8px; }
  .prompt button { background:#222; color:#fff; border:1px solid #555; border-radius:4px;
                    padding:6px 10px; font-family:inherit; font-size:13px; cursor:pointer; }
  .prompt.resolved button { opacity:.4; pointer-events:none; }
  #bar { display:flex; gap:8px; padding:10px; border-top:1px solid #333; }
  #bar textarea { flex:1; resize:none; background:#161616; color:#fff; border:1px solid #444;
                  border-radius:6px; padding:8px; font-family:inherit; font-size:14px; height:40px; }
  #bar button { background:var(--orange); color:#000; border:none; border-radius:6px;
                 padding:0 16px; font-weight:bold; cursor:pointer; }
</style>
</head>
<body>
<header><span class="dot"></span><b>pisya</b> <span style="color:var(--dim)">remote</span></header>
<div id="log"></div>
<div id="bar">
  <textarea id="input" placeholder="Message..." rows="1"></textarea>
  <button id="send">Send</button>
</div>
<script>
const TOKEN = "__TOKEN__";
const log   = document.getElementById('log');
const input = document.getElementById('input');

function escapeHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function renderMD(text) {
  const lines = text.split('\n');
  let html = '', inCode = false;
  for (const line of lines) {
    if (line.startsWith('```')) { inCode = !inCode; html += inCode ? '<pre><code>' : '</code></pre>'; continue; }
    if (inCode) { html += escapeHtml(line) + '\n'; continue; }
    const m = line.match(/^(#{1,6}) (.*)/);
    if (m) { html += '<b>' + escapeHtml(m[2]) + '</b><br>'; continue; }
    let l = escapeHtml(line);
    l = l.replace(/\*\*(.+?)\*\*/g, '<b>$1</b>');
    l = l.replace(/`([^`]+?)`/g, '<code>$1</code>');
    html += l + '<br>';
  }
  return html;
}

function addMsg(cls, html) {
  const div = document.createElement('div');
  div.className = 'msg ' + cls;
  div.innerHTML = html;
  log.appendChild(div);
  log.scrollTop = log.scrollHeight;
  return div;
}

function toolLabel(ev) {
  const a = ev.args || {};
  switch (ev.name) {
    case 'read_file':    return 'Read ' + (a.path || '');
    case 'create_file':  return 'Create ' + (a.path || '');
    case 'write_file':   return 'Write ' + (a.path || '');
    case 'edit_file':    return 'Edit ' + (a.path || '');
    case 'list_dir':     return 'List ' + (a.path || '.');
    case 'glob_files':   return 'Glob ' + (a.pattern || '');
    case 'search_files': return 'Search ' + (a.pattern || '');
    case 'bash':         return '$ ' + (a.command || '');
    case 'ask_user':     return 'Ask: ' + (a.question || '');
    default:             return ev.name;
  }
}

function respond(id, choice, div) {
  div.classList.add('resolved');
  fetch('/api/respond', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({token: TOKEN, id, choice})
  });
}

function handleEvent(ev) {
  switch (ev.type) {
    case 'user':
      addMsg('user', '<span class="label">You</span><br>' + escapeHtml(ev.text));
      break;
    case 'text':
      addMsg('text', '<span class="label">●</span> ' + renderMD(ev.text));
      break;
    case 'error':
      addMsg('error', escapeHtml(ev.text));
      break;
    case 'status':
      addMsg('status', escapeHtml(ev.text));
      break;
    case 'tool': {
      const result = (ev.result || '').slice(0, 2000);
      addMsg('tool', '<details><summary>' + escapeHtml(toolLabel(ev)) +
             '</summary><pre>' + escapeHtml(result) + '</pre></details>');
      break;
    }
    case 'prompt': {
      const div = addMsg('prompt', '');
      div.dataset.promptId = ev.id;
      let html = '<div>' + escapeHtml(ev.title) + '</div><div class="opts">';
      (ev.options || []).forEach((o, i) => {
        html += '<button data-choice="' + i + '">' + escapeHtml(o) + '</button>';
      });
      html += '</div>';
      div.innerHTML = html;
      div.querySelectorAll('button').forEach(btn => {
        btn.addEventListener('click', () => respond(ev.id, parseInt(btn.dataset.choice, 10), div));
      });
      break;
    }
    case 'prompt_resolved': {
      const el = log.querySelector('.prompt[data-prompt-id="' + ev.id + '"]');
      if (el) el.classList.add('resolved');
      break;
    }
  }
}

function send() {
  const text = input.value.trim();
  if (!text) return;
  input.value = '';
  fetch('/api/input', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({token: TOKEN, text})
  });
}

document.getElementById('send').addEventListener('click', send);
input.addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(); }
});

const es = new EventSource('/api/stream?token=' + encodeURIComponent(TOKEN));
es.onmessage = e => {
  try { handleEvent(JSON.parse(e.data)); } catch (err) { console.error(err); }
};
</script>
</body>
</html>
)HTML";

// ── auth helper ───────────────────────────────────────────────────────────────

std::string token_from_request(const httplib::Request& req) {
    std::string t = req.get_param_value("token");
    if (!t.empty()) return t;
    try {
        auto j = nlohmann::json::parse(req.body);
        return j.value("token", std::string());
    } catch (...) {
        return "";
    }
}

bool authorized(const httplib::Request& req, httplib::Response& res) {
    if (!token_equal(token_from_request(req), g_token)) {
        res.status = 403;
        res.set_content("Forbidden", "text/plain");
        return false;
    }
    return true;
}

// ── routes ───────────────────────────────────────────────────────────────────

void setup_routes(httplib::Server& svr) {
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        if (!authorized(req, res)) return;
        std::string page = PAGE_HTML;
        if (auto pos = page.find("__TOKEN__"); pos != std::string::npos)
            page.replace(pos, std::string("__TOKEN__").size(), g_token);
        res.set_content(page, "text/html; charset=utf-8");
    });

    svr.Get("/api/stream", [](const httplib::Request& req, httplib::Response& res) {
        if (!authorized(req, res)) return;

        size_t next;
        { std::lock_guard<std::mutex> lock(g_mutex); next = g_events_base; }

        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider("text/event-stream",
            [next](size_t /*offset*/, httplib::DataSink& sink) mutable -> bool {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_cv.wait_for(lock, std::chrono::seconds(15),
                    [&] { return next < g_events_total || !g_server; });

                if (!g_server || !sink.is_writable()) return false;

                std::string chunk;
                if (next < g_events_total) {
                    size_t start = std::max(next, g_events_base);
                    for (size_t i = start; i < g_events_total; ++i)
                        chunk += "data: " + g_events[i - g_events_base] + "\n\n";
                    next = g_events_total;
                } else {
                    chunk = ": ping\n\n";
                }
                lock.unlock();
                return sink.write(chunk.data(), chunk.size());
            });
    });

    svr.Post("/api/input", [](const httplib::Request& req, httplib::Response& res) {
        if (!authorized(req, res)) return;
        try {
            auto j = nlohmann::json::parse(req.body);
            std::string text = j.value("text", std::string());
            if (!text.empty()) {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_input_queue.push_back(std::move(text));
            }
        } catch (...) {}
        res.set_content("ok", "text/plain");
    });

    svr.Post("/api/respond", [](const httplib::Request& req, httplib::Response& res) {
        if (!authorized(req, res)) return;
        try {
            auto j      = nlohmann::json::parse(req.body);
            int id      = j.value("id", -1);
            int choice  = j.value("choice", -1);
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_prompts.find(id);
            if (it != g_prompts.end() && it->second.choice == -1 && choice >= 0)
                it->second.choice = choice;
        } catch (...) {}
        res.set_content("ok", "text/plain");
    });
}

} // namespace

// ── public API ───────────────────────────────────────────────────────────────

std::string rc::start() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_server) {
        g_token  = gen_token();
        g_server = std::make_unique<httplib::Server>();
        setup_routes(*g_server);

        int port = g_server->bind_to_any_port("0.0.0.0");
        if (port <= 0) {
            g_server.reset();
            return "";
        }
        g_port    = port;
        g_host_ip = detect_lan_ip();

        httplib::Server* srv = g_server.get();
        g_thread = std::thread([srv] { srv->listen_after_bind(); });
    }

    return "http://" + g_host_ip + ":" + std::to_string(g_port) + "/?token=" + g_token;
}

void rc::stop() {
    std::unique_ptr<httplib::Server> srv;
    std::thread                      th;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_server) return;
        srv = std::move(g_server);
        th  = std::move(g_thread);
    }
    g_cv.notify_all();
    srv->stop();
    if (th.joinable()) th.join();
}

bool rc::active() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_server != nullptr;
}

void rc::push_event(const nlohmann::json& event) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_server) return;
    push_event_locked(event);
}

int rc::push_prompt(const std::string& title, const std::vector<std::string>& options) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_server) return -1;

    int id = g_next_prompt_id++;
    g_prompts[id] = {};
    push_event_locked({{"type", "prompt"}, {"id", id}, {"title", title}, {"options", options}});
    return id;
}

int rc::poll_choice(int id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_prompts.find(id);
    return it == g_prompts.end() ? -1 : it->second.choice;
}

void rc::resolve_prompt(int id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_server) return;
    g_prompts.erase(id);
    push_event_locked({{"type", "prompt_resolved"}, {"id", id}});
}

std::string rc::poll_input() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_input_queue.empty()) return "";
    std::string s = std::move(g_input_queue.front());
    g_input_queue.pop_front();
    return s;
}
