#pragma once
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/prctl.h>
#include "random_utils.h"

namespace process_utils {
template <class Fn>
static pid_t fork_delayed_task(unsigned delay_sec, Fn&& fn) {
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

static std::string reset_random_process_name() {
	std::vector<uint8_t> random_name = random_utils::generate_unique_non_zero_bytes(MY_TASK_COMM_LEN - 1);
	random_name.push_back(0);
	if (prctl(PR_SET_NAME, random_name.data(), 0, 0, 0)) return "";
    char verify_name[MY_TASK_COMM_LEN] = {0};
    if (prctl(PR_GET_NAME, verify_name, 0, 0, 0)) return "";
    if (strncmp(reinterpret_cast<const char*>(random_name.data()), verify_name, MY_TASK_COMM_LEN) != 0) {
        return "";
    }
    return verify_name;
}

} // namespace process_utils