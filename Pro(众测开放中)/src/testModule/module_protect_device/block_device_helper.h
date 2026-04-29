#pragma once
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <fcntl.h>
#include <limits.h>
#include <filesystem>
#include <set>

namespace block_device_helper {

#define KERNEL_MINORBITS 20
#define KERNEL_MINORMASK ((1u << KERNEL_MINORBITS) - 1)

struct DevNodeInfo {
    char name[256] = {0};
    char path[512] = {0};

    dev_t original_rdev = 0;
    uint32_t kernel_rdev = 0;
};

static inline uint32_t user_rdev_to_kernel_dev(dev_t rdev) {
    uint32_t maj = major(rdev);
    uint32_t min = minor(rdev);
    return ((maj << KERNEL_MINORBITS) | (min & KERNEL_MINORMASK));
}

static inline bool is_white_name(const char* name) {
    return strncmp(name, "zram", 4) == 0 ||
           strcmp(name, "userdata") == 0 ||
           strcmp(name, "cache") == 0 ||
           strcmp(name, "logfs") == 0 ||
           strcmp(name, "logdump") == 0 ||
           strcmp(name, "rawdump") == 0 ||
           strcmp(name, "ramdump") == 0 ||
           strcmp(name, "xbl_sc_logs") == 0 ||
           strncmp(name, "xbl_ramdump_", 12) == 0 ||
           strcmp(name, "qmcs") == 0;
}

static inline bool read_sysfs_dev(const std::string& dev_file_path, dev_t& out_rdev) {
    std::ifstream file(dev_file_path);
    if (!file.is_open()) return false;

    std::string line;
    if (std::getline(file, line)) {
        unsigned int maj = 0;
        unsigned int min = 0;

        if (sscanf(line.c_str(), "%u:%u", &maj, &min) == 2) {
            out_rdev = makedev(maj, min);
            return true;
        }
    }

    return false;
}

static inline bool get_parent_disk_rdev(dev_t child_rdev, dev_t& parent_rdev) {
    unsigned int maj = major(child_rdev);
    unsigned int min = minor(child_rdev);

    char link_path[128] = {0};
    snprintf(link_path, sizeof(link_path), "/sys/dev/block/%u:%u", maj, min);

    char real_path[PATH_MAX] = {0};
    if (!realpath(link_path, real_path)) {
        return false;
    }

    std::string sys_path = real_path;

    // 有 partition 文件，说明当前是分区，parent 才是母盘
    std::string partition_file = sys_path + "/partition";
    if (access(partition_file.c_str(), F_OK) == 0) {
        std::string parent_dev_file = std::filesystem::path(sys_path).parent_path().string() + "/dev";

        return read_sysfs_dev(parent_dev_file, parent_rdev);
    }

    // 没有 partition 文件，说明当前可能已经是整盘/虚拟块设备
    return read_sysfs_dev(sys_path + "/dev", parent_rdev);
}

static inline bool get_block_name_by_rdev(dev_t rdev, std::string& out_name) {
    unsigned int maj = major(rdev);
    unsigned int min = minor(rdev);

    char sys_link[128] = {0};
    snprintf(sys_link, sizeof(sys_link), "/sys/dev/block/%u:%u", maj, min);

    char real_path[PATH_MAX] = {0};
    if (!realpath(sys_link, real_path)) {
        return false;
    }

    const char* base = strrchr(real_path, '/');
    if (!base || !base[1]) {
        return false;
    }

    out_name = base + 1;
    return true;
}

static inline bool find_devnode_by_rdev(dev_t target_rdev, std::string& out_path) {
    DIR* dir = opendir("/dev/block");
    if (!dir) return false;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        std::string path = std::string("/dev/block/") + ent->d_name;

        struct stat st{};
        if (stat(path.c_str(), &st) == 0 &&
            S_ISBLK(st.st_mode) &&
            st.st_rdev == target_rdev) {
            out_path = path;
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

static inline bool collect_target_partitions(std::vector<DevNodeInfo>& partitions) {
    DIR* dir = opendir("/dev/block/bootdevice/by-name/");
    if (!dir) {
        printf("collect_target_partitions: opendir failed\n");
        return false;
    }

    std::set<dev_t> added_partition_rdevs;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char* name = ent->d_name;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (is_white_name(name)) continue;

        std::string full_path = std::string("/dev/block/bootdevice/by-name/") + name;

        struct stat st{};
        if (stat(full_path.c_str(), &st) != 0 || !S_ISBLK(st.st_mode)) continue;

        if (added_partition_rdevs.find(st.st_rdev) != added_partition_rdevs.end()) continue;
        added_partition_rdevs.insert(st.st_rdev);

        DevNodeInfo info{};
        strncpy(info.name, name, sizeof(info.name) - 1);
        snprintf(info.path, sizeof(info.path), "%s", full_path.c_str());
        info.original_rdev = st.st_rdev;

        partitions.push_back(info);

        printf("add partition: %s -> %s (%u:%u)\n",
               info.name,
               info.path[0] ? info.path : "<no-path>",
               major(info.original_rdev),
               minor(info.original_rdev));
    }

    closedir(dir);
    return !partitions.empty();
}

static inline std::string strip_slot_suffix(const char* name) {
    std::string s = name ? name : "";

    // system_a -> system
    // vendor_b -> vendor
    if (s.size() > 2) {
        char c1 = s[s.size() - 2];
        char c2 = s[s.size() - 1];

        if (c1 == '_' && (c2 == 'a' || c2 == 'b')) {
            s.resize(s.size() - 2);
        }
    }

    return s;
}

static inline bool is_protected_mapper_name(const char* name) {
    if (!name || !name[0]) return false;

    if (strstr(name, "cow") ||
        strstr(name, "snapshot") ||
        strstr(name, "scratch") ||
        strstr(name, "userdata") ||
        strstr(name, "metadata") ||
        strstr(name, "default-key") ||
        strstr(name, "user")) {
        return false;
    }

    std::string base = strip_slot_suffix(name);

    if (base == "system" ||
        base == "system_ext" ||
        base == "vendor" ||
        base == "product" ||
        base == "odm" ||
        base == "vendor_dlkm" ||
        base == "odm_dlkm" ||
        base == "system_dlkm") {
        return true;
    }

    // OPPO / OnePlus / realme 等厂商常见扩展
    if (base.rfind("my_", 0) == 0) {
        return true;
    }

    return false;
}

static inline bool collect_mapper_partitions(std::vector<DevNodeInfo>& mapper_parts) {
    DIR* dir = opendir("/dev/block/mapper/");
    if (!dir) {
        printf("collect_mapper_partitions: /dev/block/mapper not found\n");
        return false;
    }

    std::set<dev_t> added_rdevs;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char* name = ent->d_name;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (!is_protected_mapper_name(name)) continue;

        std::string full_path = std::string("/dev/block/mapper/") + name;

        struct stat st{};
        if (stat(full_path.c_str(), &st) != 0) {
            printf("collect_mapper_partitions: stat failed, path=%s errno=%d(%s)\n", full_path.c_str(), errno, strerror(errno));
            continue;
        }

        if (!S_ISBLK(st.st_mode)) {
            printf("collect_mapper_partitions: not block dev, path=%s\n", full_path.c_str());
            continue;
        }

        if (added_rdevs.find(st.st_rdev) != added_rdevs.end()) continue;
        added_rdevs.insert(st.st_rdev);

        DevNodeInfo info{};
        snprintf(info.name, sizeof(info.name), "%s", name);
        snprintf(info.path, sizeof(info.path), "%s", full_path.c_str());
        info.original_rdev = st.st_rdev;

        mapper_parts.push_back(info);

        printf("add mapper: %s -> %s (%u:%u)\n",
               info.name,
               info.path[0] ? info.path : "<no-path>",
               major(info.original_rdev),
               minor(info.original_rdev));
    }

    closedir(dir);
    return !mapper_parts.empty();
}

static inline void resolve_parent_disks(const std::vector<DevNodeInfo>& partitions,
                                        std::vector<DevNodeInfo>& parents) {
    std::set<dev_t> added_parent_rdevs;

    for (const auto& child : partitions) {
        dev_t parent_rdev{};

        if (!get_parent_disk_rdev(child.original_rdev, parent_rdev)) continue;

        if (added_parent_rdevs.find(parent_rdev) != added_parent_rdevs.end()) continue;

        DevNodeInfo parent_info{};

        std::string disk_name;
        std::string dev_path;

        // 例如 parent_rdev -> /sys/dev/block/8:0 -> basename sda
        if (get_block_name_by_rdev(parent_rdev, disk_name)) {
            dev_path = "/dev/block/" + disk_name;
        }

        // 验证 /dev/block/sda 这类路径是否真实存在，且 rdev 是否一致
        struct stat st{};
        if (dev_path.empty() ||
            stat(dev_path.c_str(), &st) != 0 ||
            !S_ISBLK(st.st_mode) ||
            st.st_rdev != parent_rdev) {

            // fallback：扫描 /dev/block，按 st_rdev 找真实节点
            if (!find_devnode_by_rdev(parent_rdev, dev_path)) {
                dev_path.clear();
            }
        }

        if (!disk_name.empty()) {
            snprintf(parent_info.name, sizeof(parent_info.name), "%s", disk_name.c_str());
        } else {
            snprintf(parent_info.name, sizeof(parent_info.name), "%u:%u", major(parent_rdev), minor(parent_rdev));
        }

        if (!dev_path.empty()) {
            snprintf(parent_info.path, sizeof(parent_info.path), "%s", dev_path.c_str());
        } else {
            parent_info.path[0] = '\0';
        }

        parent_info.original_rdev = parent_rdev;

        parents.push_back(parent_info);
        added_parent_rdevs.insert(parent_rdev);

        printf("add parent disk: %s -> %s (%u:%u)\n",
               parent_info.name,
               parent_info.path[0] ? parent_info.path : "<no-path>",
               major(parent_rdev),
               minor(parent_rdev));
    }
}

static inline bool build_protected_device_list(std::vector<DevNodeInfo>& out_dev_list) {
    out_dev_list.clear();

    std::vector<DevNodeInfo> partitions;
    std::vector<DevNodeInfo> mapper_parts;
    std::vector<DevNodeInfo> parent_disks;

    if (!collect_target_partitions(partitions)) {
        printf("build_protected_device_list: no vulnerable partitions found.\n");
        return false;
    }

    // 收集 /dev/block/mapper/system/vendor/product 等 dm 逻辑分区
    collect_mapper_partitions(mapper_parts);

    resolve_parent_disks(partitions, parent_disks);

    out_dev_list.insert(out_dev_list.end(), partitions.begin(), partitions.end());
    out_dev_list.insert(out_dev_list.end(), mapper_parts.begin(), mapper_parts.end());
    out_dev_list.insert(out_dev_list.end(), parent_disks.begin(), parent_disks.end());

    for (auto& node : out_dev_list) {
        node.kernel_rdev = user_rdev_to_kernel_dev(node.original_rdev);
    }

    printf("build_protected_device_list: Success! Protected %zu partitions, %zu mapper devices and %zu parent disks.\n",
           partitions.size(),
           mapper_parts.size(),
           parent_disks.size());

    return true;
}

static inline bool verify_protected_devices_not_writable(const std::vector<DevNodeInfo>& dev_list) {
    bool ok = true;

    for (const auto& dev : dev_list) {
        if (!dev.path[0]) {
            printf("verify_protect: skip no path, name=%s rdev=%u:%u\n", dev.name, major(dev.original_rdev), minor(dev.original_rdev));
            continue;
        }

        errno = 0;
        int fd = open(dev.path, O_WRONLY | O_CLOEXEC);

        if (fd >= 0) {
            printf("verify_protect: FAILED, writable open allowed, name=%s path=%s\n", dev.name, dev.path);
            close(fd);
            ok = false;
            continue;
        }

        if (errno != EPERM) {
            printf("verify_protect: unexpected fail, name=%s path=%s errno=%d(%s)\n", dev.name, dev.path, errno, strerror(errno));
            ok = false;
        }
    }

    return ok;
}

} // namespace block_device_helper