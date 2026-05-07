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
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "kernel_module_kit_umbrella.h"
#include "patch_inode_permission.h"

namespace fs = std::filesystem;
uint64_t g_persist_dir_lock_control_kaddr = 0;

namespace _deatil {
    static const char* kPersistDir = "/mnt/vendor/persist/data";

    #define KERNEL_MINORBITS 20
    #define KERNEL_MINORMASK ((1u << KERNEL_MINORBITS) - 1)

    static uint32_t user_rdev_to_kernel_dev(dev_t rdev) {
        uint32_t maj = major(rdev);
        uint32_t min = minor(rdev);
        return ((maj << KERNEL_MINORBITS) | (min & KERNEL_MINORMASK));
    }

    static KModErr patch_kernel_handler(uint64_t control_kaddr) {
        uint64_t inode_permission = 0;
        RETURN_IF_ERROR(kernel_module::kallsyms_lookup_name("inode_permission", inode_permission));
        printf("inode_permission addr: %p\n", (void*)inode_permission);

        InodePermissionPatchOffsets off = {};
        RETURN_IF_ERROR(kernel_module::get_inode_i_ino_offset(off.inode_i_ino));
        printf("i_ino offset: 0x%x\n", off.inode_i_ino);

        RETURN_IF_ERROR(kernel_module::get_inode_i_sb_offset(off.inode_i_sb));
        printf("i_sb offset: 0x%x\n", off.inode_i_sb);

        RETURN_IF_ERROR(kernel_module::get_super_block_s_dev_offset(off.super_block_s_dev));
        printf("s_dev offset: 0x%x\n", off.super_block_s_dev);

        struct stat st;
        if (stat(kPersistDir, &st) != 0) {
            printf("stat failed: %s\n", kPersistDir);
            return KModErr::ERR_MODULE_PARAM;
        }
        printf("persist i_ino: %llu\n", (unsigned long long)st.st_ino);
        printf("persist s_dev: %llu\n", (unsigned long long)st.st_dev); 

        PatchBase patchBase;
        PatchInodePermission patchInodePermission(patchBase, inode_permission);
        KModErr err = patchInodePermission.patch_inode_permission(st.st_ino, user_rdev_to_kernel_dev(st.st_dev), control_kaddr, off);
        printf("patch inode_permission addr: %p ret: %s\n", (void*)inode_permission, to_string(err).c_str());
        RETURN_IF_ERROR(err);
        return KModErr::OK;
    }

    static uint64_t alloc_control_kaddr() {
        uint64_t control_kaddr = 0;
        if(is_ok(kernel_module::alloc_kernel_mem(1, control_kaddr))) {
            kernel_module::fill00_kernel_mem(control_kaddr, 1);
        }
        return control_kaddr;
    }

}

static bool persist_dir_init() {
    g_persist_dir_lock_control_kaddr = _deatil::alloc_control_kaddr();
    if(!g_persist_dir_lock_control_kaddr) {
        printf("alloc control kaddr failed\n");
        return false;
    }
    KModErr err = _deatil::patch_kernel_handler(g_persist_dir_lock_control_kaddr);
    printf("patch_kernel_handler ret: %s\n", to_string(err).c_str());
    if (!is_ok(err)) {
        printf("patch_kernel_handler failed\n");
        return false;
    }
    return true;
}

static inline bool persist_dir_lock() {
    if(!g_persist_dir_lock_control_kaddr) return false;
    char ch = '\x01';
    kernel_module::write_kernel_mem(g_persist_dir_lock_control_kaddr, &ch, 1);
    printf("persist dir lock!\n");
    return true;
}

static bool persist_dir_unlock() {
    if(!g_persist_dir_lock_control_kaddr) return false;
    char ch = '\x00';
    kernel_module::write_kernel_mem(g_persist_dir_lock_control_kaddr, &ch, 1);
    printf("persist dir unlock!\n");
    return true;
}
