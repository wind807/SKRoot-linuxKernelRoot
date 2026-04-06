
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <chrono>
#include <ctime>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>

#include "kernel_module_kit_umbrella.h"

#include "su_interactive.h"
#include "idle_killer.h"
#include "json_helper.h"
#include "url_encode_utils.h"

#define MAX_QUICK_ACTIONS 10
#define RECORD_CMD_LEN 100

namespace fs = std::filesystem;

int skroot_module_main(const char* root_key, const char* module_private_dir) { return 0; }

static void append_or_move_to_back(std::vector<std::string>& vec, const std::string& target) {
    auto it = std::find(vec.begin(), vec.end(), target);
    if (it != vec.end()) {
        if (it + 1 != vec.end()) std::rotate(it, it + 1, vec.end());
    } else {
        vec.push_back(target);
    }
}

static void add_quick_actions(const std::vector<std::string>& cmd) {
    if (!cmd.size()) return;
    std::string json;
    kernel_module::read_string_disk_storage("quick_actions", json);
    std::vector<std::string> cmd_arr = parse_json(json);
    for(auto & c : cmd) {
        std::vector<char> encoded_buf(c.size() * 3 + 1, '\0');
        url_encode(c.c_str(), encoded_buf.data());
        std::string encoded_str(encoded_buf.data(), std::strlen(encoded_buf.data()));
        append_or_move_to_back(cmd_arr, encoded_str);
    }
    while (cmd_arr.size() > MAX_QUICK_ACTIONS) {
        cmd_arr.erase(cmd_arr.begin());
    }
    json = json_array_from_set(cmd_arr);
    kernel_module::write_string_disk_storage("quick_actions", json.c_str());
}

static std::string read_quick_actions_json() {
    std::string json;
    kernel_module::read_string_disk_storage("quick_actions", json);
    if(json.empty()) json = "[]";
    return json;
}

std::string module_on_install(const char* root_key, const char* module_private_dir) {
    add_quick_actions({"getenforce", "ls /", "id"});
    return "";
}


static bool pid_alive(pid_t pid) {
    if (pid <= 1) return false;
    if (::kill(pid, 0) == 0) return true;
    return errno == EPERM;
}

static void wait_parent_exit(const std::string& root_key, pid_t ppid) {
    timespec ts{0, 500 * 1000 * 1000}; // 500ms
    while (getppid() != 1 && pid_alive(ppid)) {
        skroot_env::get_root(root_key.c_str());
        nanosleep(&ts, nullptr);
    }
}

// WebUI HTTP服务器回调函数
class MyWebHttpHandler : public kernel_module::WebUIHttpHandler {
public:
    void onPrepareCreate(const char* root_key, const char* module_private_dir, uint32_t port) override {
        m_root_key = root_key;
        m_su_interactive.start();
        fork_sh_process_daemon();
        m_idle_killer.start(std::chrono::seconds(30), [this] {
            _exit(0);
        });
    }

    bool handlePost(CivetServer* server, struct mg_connection* conn, const std::string& path, const std::string& body) override {
        //printf("[module_hide_sh_exec] POST request\nPath: %s\nBody: %s\n", path.c_str(), body.c_str());
        std::string resp;
        if(path == "/sendCommand") resp = handle_send_command(body);
        else if(path == "/getNewOutput") resp = handle_get_new_output(body);
        else if(path == "/getQuickActions") resp = handle_get_quick_actions(body);
        else if(path == "/listDir") resp = handle_list_dir(body);
        else if(path == "/getAutoTasks") resp = handle_get_auto_tasks();
        else if(path == "/saveAutoTasks") resp = handle_save_auto_tasks(body);

        kernel_module::webui::send_text(conn, 200, resp);
        return true;
    }

    ServerExitAction onBeforeServerExit() override { return ServerExitAction::KeepRunning; }

private:
    void fork_sh_process_daemon() {
        pid_t shell_pid = m_su_interactive.get_shell_pid();
        pid_t ppid = getpid();
        pid_t child = fork();
        if(child == 0) {
            if (setsid() < 0) {
                setpgid(0, 0); 
            }
            signal(SIGPIPE, SIG_IGN);
            wait_parent_exit(m_root_key, ppid);
            printf("[module_hide_sh_exec] kill sh!\n");
            kill(shell_pid, SIGKILL);
            _exit(0);
        }
    }

    std::string handle_send_command(const std::string& body) {
        m_idle_killer.touch();
        if(body.empty() || body == "su") return "OK";
        m_su_interactive.sendLine(body);
        if (!body.empty() && body.length() < RECORD_CMD_LEN) {
            add_quick_actions({body});
        }
        return "OK";
    }

    std::string handle_get_new_output(const std::string& body) {
        m_idle_killer.touch();
        return m_su_interactive.takeOutput();
    }

    std::string handle_get_quick_actions(const std::string& body) {
        m_idle_killer.touch();
        return read_quick_actions_json();
    }

    std::string handle_list_dir(const std::string& body) {
        m_idle_killer.touch();
        std::string dir = body.empty() ? "/" : body;
        std::error_code ec;
        fs::path target(dir);
        if (!fs::exists(target, ec) || ec) return "[]";
        if (!fs::is_directory(target, ec) || ec) return "[]";
        struct FileItem {
            std::string name;
            bool is_dir = false;
            std::string date;
            std::string time;
            uint64_t size = 0;
        };
        std::vector<FileItem> items;
        for (fs::directory_iterator it(target, fs::directory_options::skip_permission_denied, ec);
            !ec && it != fs::directory_iterator();
            it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            const auto& entry = *it;
            FileItem item;
            item.name = entry.path().filename().string();

            std::error_code sub_ec;
            item.is_dir = entry.is_directory(sub_ec);
            if (sub_ec) {
                sub_ec.clear();
                item.is_dir = false;
            }
            struct stat st{};
            if (::lstat(entry.path().c_str(), &st) == 0) {
                if (!item.is_dir) item.size = static_cast<uint64_t>(st.st_size);
                format_local_time(st.st_mtime, item.date, item.time);
            } else {
                item.size = 0;
                item.date = "";
                item.time = "";
            }
            items.push_back(std::move(item));
        }
        std::sort(items.begin(), items.end(), [](const FileItem& a, const FileItem& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir; // 目录在前
            return a.name < b.name; // 同类按名字升序
        });
        std::string json = "[";
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& f = items[i];
            if (i != 0) json += ",";
            json += "{";
            json += "\"name\":\"" + json_escape(f.name) + "\",";
            json += "\"isDir\":" + std::string(f.is_dir ? "true" : "false") + ",";
            json += "\"date\":\"" + json_escape(f.date) + "\",";
            json += "\"time\":\"" + json_escape(f.time) + "\",";
            json += "\"size\":" + std::to_string(f.size);
            json += "}";
        }
        json += "]";
        return json;
    }

    std::string handle_get_auto_tasks() {
        std::string json;
        kernel_module::read_string_disk_storage("auto_tasks", json);
        if(json.empty()) json = "[]";
        return json;
    }

    std::string handle_save_auto_tasks(const std::string& body) {
        kernel_module::write_string_disk_storage("auto_tasks", body.c_str());
        return "OK";
    }

private:
    std::string m_root_key;
    SuInteractive m_su_interactive;
    IdleKiller m_idle_killer;
};

// SKRoot 模块名片
SKROOT_MODULE_NAME("隐蔽的系统终端")
SKROOT_MODULE_VERSION("3.0.1")
SKROOT_MODULE_DESC("提供独立隐蔽的 sh 执行通道，彻底替代终端类 App，避免终端类 App 带来的特征暴露。")
SKROOT_MODULE_AUTHOR("SKRoot")
SKROOT_MODULE_UUID32("zse9vkTjLjWXbafvx8Mlh1MTf8SMTUEL")
SKROOT_MODULE_ON_INSTALL(module_on_install)
SKROOT_MODULE_WEB_UI(MyWebHttpHandler)
SKROOT_MODULE_UPDATE_JSON("https://abcz316.github.io/SKRoot-linuxKernelRoot/module_hide_sh_exec/update.json")