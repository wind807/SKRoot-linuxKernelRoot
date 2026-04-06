#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <errno.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
namespace fs = std::filesystem;

template <class Fn>
static pid_t spawn_delayed_task(unsigned delay_sec, Fn&& fn) {
    using F = std::decay_t<Fn>;
    F f = std::forward<Fn>(fn);

    pid_t pid = ::fork();
    if (pid < 0) {
        std::printf("fork failed: %s\n", std::strerror(errno));
        return -1;
    }
    if (pid == 0) {
        ::sleep(delay_sec);
        f();
        _exit(127);
    }
    return pid;
}

static bool is_all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}


static bool write_text_file(const std::string& path, const std::string& value) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        printf("open failed: %s\n", path.c_str());
        return false;
    }
    ofs << value;
    return ofs.good();
}

static bool read_text_file(const char* path, std::string& out) {
    out.clear();
    if (path == nullptr || *path == '\0') return false;
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return false;
    out.assign((std::istreambuf_iterator<char>(in)),
               std::istreambuf_iterator<char>());
    return in.good() || in.eof();
}

static bool file_exists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

static bool delete_path(const std::filesystem::path& dir_path) {
    std::error_code ec;
    std::filesystem::remove_all(dir_path, ec);
    return !file_exists(dir_path);
}

static void clear_directory_contents(const std::string& path) {
    std::error_code ec;
    
    // 检查路径是否存在且为目录
    if (fs::exists(path, ec) && fs::is_directory(path, ec)) {
        // 遍历目录内的所有子项目
        for (const auto& entry : fs::directory_iterator(path, ec)) {
            std::error_code temp_ec;
            // 对内部的每个子项执行 remove_all
            fs::remove_all(entry.path(), temp_ec);
            if (temp_ec) {
                printf("clear failed at %s : %s\n", entry.path().c_str(), temp_ec.message().c_str());
            }
        }
    } else {
        if (ec) {
            printf("access failed: %s , %s\n", path.c_str(), ec.message().c_str());
        }
    }
}
