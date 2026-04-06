#pragma once
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <utility>

#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "kernel_module_kit_umbrella.h"
#include "boot_session_utils.h"
#include "file_utils.h"

namespace fs = std::filesystem;

static inline const fs::path kPersistDataDir = "/mnt/vendor/persist/data";

static inline constexpr const char* kBindWorkDirName   = ".persist_bind_work";
static inline constexpr const char* kBindItemsDirName  = "items";
static inline constexpr const char* kBindListFileName  = "mount.list";
static inline constexpr const char* kBindBootIdName    = "boot_id";

namespace __detail {

static inline bool mkdirs_safe(const fs::path& p, mode_t mode = 0700) {
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        if (!fs::create_directories(p, ec) || ec) {
            printf("mkdirs_safe create_directories_failed: path=%s err=%d msg=%s\n",
                   p.c_str(), ec.value(), ec.message().c_str());
            return false;
        }
    }

    if (!fs::is_directory(p, ec) || ec) {
        printf("mkdirs_safe not_directory: path=%s err=%d msg=%s\n",
               p.c_str(), ec.value(), ec.message().c_str());
        return false;
    }

    if (::chmod(p.c_str(), mode) != 0) {
        printf("mkdirs_safe chmod_failed: path=%s mode=%04o errno=%d msg=%s\n",
               p.c_str(), mode & 07777, errno, std::strerror(errno));
        return false;
    }
    return true;
}

static inline bool remove_all_safe(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
    if (ec) {
        printf("remove_all_safe failed: path=%s err=%d msg=%s\n",
               p.c_str(), ec.value(), ec.message().c_str());
        return false;
    }
    return true;
}

static inline bool chmod_nofollow(const fs::path& p, mode_t mode) {
    struct stat st{};
    if (::lstat(p.c_str(), &st) != 0) {
        if (errno == ENOENT) return true;
        printf("chmod_nofollow lstat_failed: path=%s errno=%d msg=%s\n",
               p.string().c_str(), errno, std::strerror(errno));
        return false;
    }

    if (S_ISLNK(st.st_mode)) return true;

    if (::fchmodat(AT_FDCWD, p.c_str(), mode, 0) != 0) {
        printf("chmod_nofollow fchmodat_failed: path=%s mode=%04o errno=%d msg=%s\n",
               p.string().c_str(), mode & 07777, errno, std::strerror(errno));
        return false;
    }
    return true;
}

static inline bool chown_nofollow(const fs::path& p, uid_t uid, gid_t gid) {
    struct stat st{};
    if (::lstat(p.c_str(), &st) != 0) {
        if (errno == ENOENT) return true;
        printf("chown_nofollow lstat_failed: path=%s errno=%d msg=%s\n",
               p.string().c_str(), errno, std::strerror(errno));
        return false;
    }

    if (S_ISLNK(st.st_mode)) return true;

    if (::fchownat(AT_FDCWD, p.c_str(), uid, gid, 0) != 0) {
        printf("chown_nofollow fchownat_failed: path=%s uid=%u gid=%u errno=%d msg=%s\n",
               p.string().c_str(),
               static_cast<unsigned>(uid),
               static_cast<unsigned>(gid),
               errno,
               std::strerror(errno));
        return false;
    }
    return true;
}

static inline fs::path get_bind_work_dir(const fs::path& empty_root_dir) {
    return empty_root_dir / kBindWorkDirName;
}

static inline fs::path get_bind_items_dir(const fs::path& empty_root_dir) {
    return get_bind_work_dir(empty_root_dir) / kBindItemsDirName;
}

static inline fs::path get_mount_list_file(const fs::path& empty_root_dir) {
    return get_bind_work_dir(empty_root_dir) / kBindListFileName;
}

static inline fs::path get_boot_id_file(const fs::path& empty_root_dir) {
    return get_bind_work_dir(empty_root_dir) / kBindBootIdName;
}

static inline bool write_mount_list_line(std::ofstream& out,
                                         const fs::path& target_path,
                                         const fs::path& puppet_path) {
    out << target_path.string() << '\t' << puppet_path.string() << '\n';
    return out.good();
}

static inline std::vector<std::pair<fs::path, fs::path>> read_mount_list(const fs::path& mount_list_file) {
    std::vector<std::pair<fs::path, fs::path>> result;

    std::ifstream in(mount_list_file, std::ios::binary);
    if (!in.is_open()) return result;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const size_t pos = line.find('\t');
        if (pos == std::string::npos) continue;

        fs::path target_path = line.substr(0, pos);
        fs::path puppet_path = line.substr(pos + 1);
        result.emplace_back(std::move(target_path), std::move(puppet_path));
    }
    return result;
}

static inline bool copy_mode_owner_from_ref(const fs::path& ref_path, const fs::path& dst_path) {
    struct stat st{};
    if (::lstat(ref_path.c_str(), &st) != 0) {
        printf("copy_mode_owner_from_ref lstat_failed: ref=%s errno=%d msg=%s\n",
               ref_path.c_str(), errno, std::strerror(errno));
        return false;
    }

    if (S_ISLNK(st.st_mode)) return true;

    if (!chown_nofollow(dst_path, st.st_uid, st.st_gid)) return false;
    if (!chmod_nofollow(dst_path, static_cast<mode_t>(st.st_mode & 07777))) return false;
    return true;
}

static inline bool copy_selinux_context_from_ref(const fs::path& ref_path, const fs::path& dst_path) {
    char buf[1024] = {0};
    const ssize_t n = ::lgetxattr(ref_path.c_str(), "security.selinux", buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("copy_selinux_context_from_ref lgetxattr_skip: ref=%s errno=%d msg=%s\n",
               ref_path.c_str(), errno, std::strerror(errno));
        return true;
    }

    if (::lsetxattr(dst_path.c_str(), "security.selinux", buf, static_cast<size_t>(n), 0) != 0) {
        printf("copy_selinux_context_from_ref lsetxattr_skip: dst=%s errno=%d msg=%s\n",
               dst_path.c_str(), errno, std::strerror(errno));
        return true;
    }

    return true;
}

static inline bool create_empty_puppet_like(const fs::path& ref_path, const fs::path& puppet_path) {
    struct stat st{};
    if (::lstat(ref_path.c_str(), &st) != 0) {
        printf("create_empty_puppet_like lstat_failed: ref=%s errno=%d msg=%s\n",
               ref_path.c_str(), errno, std::strerror(errno));
        return false;
    }

    if (S_ISLNK(st.st_mode)) {
        printf("create_empty_puppet_like skip_symlink: ref=%s\n", ref_path.c_str());
        return true;
    }

    std::error_code ec;
    fs::remove_all(puppet_path, ec);
    ec.clear();

    if (S_ISDIR(st.st_mode)) {
        if (!fs::create_directories(puppet_path, ec) || ec) {
            printf("create_empty_puppet_like mkdir_failed: path=%s err=%d msg=%s\n",
                   puppet_path.c_str(), ec.value(), ec.message().c_str());
            return false;
        }
    } else {
        if (!mkdirs_safe(puppet_path.parent_path(), 0700)) return false;

        int fd = ::open(puppet_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
        if (fd < 0) {
            printf("create_empty_puppet_like create_file_failed: path=%s errno=%d msg=%s\n",
                   puppet_path.c_str(), errno, std::strerror(errno));
            return false;
        }
        ::close(fd);
    }

    if (!copy_mode_owner_from_ref(ref_path, puppet_path)) return false;
    copy_selinux_context_from_ref(ref_path, puppet_path);
    return true;
}

static inline bool do_bind_mount_private(const fs::path& source_path, const fs::path& target_path) {
    if (::mount(source_path.c_str(), target_path.c_str(), nullptr, MS_BIND, nullptr) != 0) {
        printf("do_bind_mount_private mount_bind_failed: src=%s dst=%s errno=%d msg=%s\n",
               source_path.c_str(), target_path.c_str(), errno, std::strerror(errno));
        return false;
    }

    if (::mount(nullptr, target_path.c_str(), nullptr, MS_PRIVATE, nullptr) != 0) {
        printf("do_bind_mount_private mount_private_failed: dst=%s errno=%d msg=%s\n",
               target_path.c_str(), errno, std::strerror(errno));
    }

    return true;
}

static inline bool do_umount_lazy(const fs::path& target_path) {
    if (::umount2(target_path.c_str(), MNT_DETACH) != 0) {
        if (errno == EINVAL || errno == ENOENT) return true;
        printf("do_umount_lazy failed: target=%s errno=%d msg=%s\n",
               target_path.c_str(), errno, std::strerror(errno));
        return false;
    }
    return true;
}

static inline bool cleanup_bind_state_files(const fs::path& empty_root_dir) {
    const fs::path work_dir   = get_bind_work_dir(empty_root_dir);
    const fs::path items_dir  = get_bind_items_dir(empty_root_dir);
    const fs::path list_file  = get_mount_list_file(empty_root_dir);
    const fs::path boot_id    = get_boot_id_file(empty_root_dir);

    bool ok = true;
    if (!remove_all_safe(items_dir)) ok = false;

    std::error_code ec;
    fs::remove(list_file, ec);
    fs::remove(boot_id, ec);
    fs::remove(work_dir, ec);

    return ok;
}

static inline bool is_state_stale_after_reboot(const fs::path& empty_root_dir) {
    const fs::path list_file    = get_mount_list_file(empty_root_dir);
    const fs::path boot_id_file = get_boot_id_file(empty_root_dir);

    std::error_code ec;
    if (!fs::exists(list_file, ec) || ec) return false;

    const std::string current_boot_id = boot_session_utils::read_boot_session();
    const std::string saved_boot_id   = file_utils::read_text_file_trim(boot_id_file);

    if (current_boot_id.empty() || saved_boot_id.empty()) {
        printf("is_state_stale_after_reboot boot_id_missing: current_empty=%d saved_empty=%d\n",
               current_boot_id.empty() ? 1 : 0,
               saved_boot_id.empty() ? 1 : 0);
        return true;
    }

    if (current_boot_id != saved_boot_id) {
        printf("is_state_stale_after_reboot changed: current=%s saved=%s\n",
               current_boot_id.c_str(), saved_boot_id.c_str());
        return true;
    }

    return false;
}

} // namespace __detail

static inline bool persist_data_unlock(const fs::path& empty_root_dir) {
    if (empty_root_dir.empty()) {
        printf("persist_data_unlock empty_root_dir_empty\n");
        return false;
    }

    const fs::path items_dir = __detail::get_bind_items_dir(empty_root_dir);
    const fs::path list_file = __detail::get_mount_list_file(empty_root_dir);

    const auto mounts = __detail::read_mount_list(list_file);

    bool ok = true;
    for (auto it = mounts.rbegin(); it != mounts.rend(); ++it) {
        const fs::path& target_path = it->first;
        if (!__detail::do_umount_lazy(target_path)) {
            ok = false;
        }
    }

    if (!__detail::remove_all_safe(items_dir)) ok = false;

    std::error_code ec;
    fs::remove(list_file, ec);
    fs::remove(__detail::get_boot_id_file(empty_root_dir), ec);
    fs::remove(__detail::get_bind_work_dir(empty_root_dir), ec);

    printf("persist_data_unlock %s: root=%s empty_root=%s\n",
           ok ? "ok" : "partial_failed",
           kPersistDataDir.c_str(),
           empty_root_dir.c_str());
    return ok;
}

static inline bool persist_data_lock(const fs::path& empty_root_dir) {
    if (empty_root_dir.empty()) {
        printf("persist_data_lock empty_root_dir_empty\n");
        return false;
    }

    struct stat root_st{};
    if (::lstat(kPersistDataDir.c_str(), &root_st) != 0) {
        if (errno == ENOENT) return true;
        printf("persist_data_lock target_lstat_failed: path=%s errno=%d msg=%s\n",
               kPersistDataDir.c_str(), errno, std::strerror(errno));
        return false;
    }

    if (!S_ISDIR(root_st.st_mode)) {
        printf("persist_data_lock target_not_dir: path=%s\n", kPersistDataDir.c_str());
        return false;
    }

    if (__detail::is_state_stale_after_reboot(empty_root_dir)) {
        __detail::cleanup_bind_state_files(empty_root_dir);
    }

    // 同一次开机里的残留状态、上次部分挂载失败的状态，也统一先清掉
    persist_data_unlock(empty_root_dir);

    const fs::path work_dir   = __detail::get_bind_work_dir(empty_root_dir);
    const fs::path items_dir  = __detail::get_bind_items_dir(empty_root_dir);
    const fs::path list_file  = __detail::get_mount_list_file(empty_root_dir);
    const fs::path boot_file  = __detail::get_boot_id_file(empty_root_dir);

    if (!__detail::mkdirs_safe(work_dir, 0700)) return false;
    if (!__detail::mkdirs_safe(items_dir, 0700)) return false;

    std::ofstream out(list_file, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        printf("persist_data_lock open_mount_list_failed: path=%s\n", list_file.c_str());
        return false;
    }

    std::error_code ec;
    fs::directory_iterator it(kPersistDataDir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        printf("persist_data_lock iterator_init_failed: root=%s err=%d msg=%s\n",
               kPersistDataDir.c_str(), ec.value(), ec.message().c_str());
        return false;
    }

    size_t index = 0;
    size_t bind_ok_count = 0;

    for (; it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) {
            printf("persist_data_lock iterator_increment_failed: root=%s err=%d msg=%s\n",
                   kPersistDataDir.c_str(), ec.value(), ec.message().c_str());
            return false;
        }

        const fs::path orig_item = it->path();
        const std::string item_name = orig_item.filename().string();

        if (item_name.empty()) continue;
        //if (item_name.rfind("DdHd", 0) == 0) continue;

        const fs::path puppet_path = items_dir / (std::to_string(index) + "_" + item_name);

        if (!__detail::create_empty_puppet_like(orig_item, puppet_path)) {
            printf("persist_data_lock create_empty_puppet_failed: orig=%s puppet=%s\n",
                   orig_item.c_str(), puppet_path.c_str());
            ++index;
            continue;
        }

        if (!__detail::do_bind_mount_private(puppet_path, orig_item)) {
            printf("persist_data_lock bind_failed: orig=%s puppet=%s\n",
                   orig_item.c_str(), puppet_path.c_str());
            ++index;
            continue;
        }

        if (!__detail::write_mount_list_line(out, orig_item, puppet_path)) {
            printf("persist_data_lock write_mount_list_failed: target=%s puppet=%s\n",
                   orig_item.c_str(), puppet_path.c_str());
            __detail::do_umount_lazy(orig_item);
            out.clear();
            ++index;
            continue;
        }

        ++bind_ok_count;
        ++index;
    }

    out.close();

    const std::string boot_id = boot_session_utils::read_boot_session();
    if (!boot_id.empty()) {
        file_utils::write_text_file(boot_file.string().c_str(), boot_id);
    } else {
        printf("persist_data_lock boot_id_empty\n");
    }

    printf("persist_data_lock ok: root=%s empty_root=%s bind_ok_count=%zu\n",
           kPersistDataDir.c_str(),
           empty_root_dir.c_str(),
           bind_ok_count);
    return true;
}