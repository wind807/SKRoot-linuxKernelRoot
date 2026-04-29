#include "patch_blkdev_open.h"
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <cstring>
#include <string>
#include <vector>
#include <fcntl.h>

#include "kernel_module_kit_umbrella.h"
#include "process_utils.h"

#define KERNEL_MINORBITS 20
#define KERNEL_MINORMASK ((1u << KERNEL_MINORBITS) - 1)

static uint32_t user_rdev_to_kernel_dev(dev_t rdev) {
    unsigned int maj = major(rdev);
    unsigned int min = minor(rdev);
    return ((maj << KERNEL_MINORBITS) | (min & KERNEL_MINORMASK));
}

static bool is_white_name(const char* name) {
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

static bool scan_block_devices(std::vector<DevNodeInfo> & out_dev_list) {
    DIR* dir = opendir("/dev/block/bootdevice/by-name/");
    if (!dir) {
        printf("scan_block_devices: opendir failed\n");
        return false;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char* name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (is_white_name(name)) continue;
        std::string full_path = std::string("/dev/block/bootdevice/by-name/") + name;
        struct stat st{};
        if (stat(full_path.c_str(), &st) != 0) {
            printf("scan_block_devices: stat failed, path=%s\n", full_path.c_str());
            continue;
        }
        if (!S_ISBLK(st.st_mode)) {
            printf("scan_block_devices: not block dev, path=%s\n", full_path.c_str());
            continue;
        }
        DevNodeInfo info{};
        strncpy(info.name, name, sizeof(info.name) - 1);
        info.name[sizeof(info.name) - 1] = '\0';
        info.kernel_rdev = user_rdev_to_kernel_dev(st.st_rdev);
        out_dev_list.push_back(info);
        //printf("scan_block_devices: add name=%s kernel_rdev=%d\n", info.name, info.kernel_rdev);
    }
    closedir(dir);
    if (out_dev_list.empty()) {
        printf("scan_block_devices: no valid block device found\n");
        return false;
    }
    printf("scan_block_devices: success, count=%zu\n", out_dev_list.size());
    return true;
}

static bool verify_protected_devices_not_writable(const std::vector<DevNodeInfo>& dev_list) {
    bool ok = true;
    for (const auto& dev : dev_list) {
        std::string path = std::string("/dev/block/bootdevice/by-name/") + dev.name;
        errno = 0;
        int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            // 能以写方式打开，说明 blkdev_open 没拦住，这是失败
            printf("verify_protect: FAILED, writable open allowed, name=%s path=%s\n", dev.name, path.c_str());
            close(fd);
            ok = false;
            continue;
        }
        // 预期应该失败：EPERM 
        // printf("verify_protect: blocked, name=%s path=%s errno=%d(%s)\n", dev.name, path.c_str(), errno, strerror(errno));
    }
    return ok;
}

// 开始修补内核
static KModErr patch_kernel_handler(const std::vector<DevNodeInfo>& dev_list, const std::string& test_comm_name, uint64_t control_kaddr) {
    uint64_t blkdev_open_sym = 0;
    RETURN_IF_ERROR(kernel_module::kallsyms_lookup_name("blkdev_open", blkdev_open_sym));
    printf("blkdev_open(sym), addr: %p\n", (void*)blkdev_open_sym);

    uint32_t comm_offset = 0;
    RETURN_IF_ERROR(kernel_module::get_task_struct_comm_offset(comm_offset));
    printf("comm offset: 0x%x\n", comm_offset);

    BlkdevOpenPatchOffsets off = {};
    RETURN_IF_ERROR(kernel_module::get_file_f_mode_offset(off.file_f_mode));
    printf("f_mode offset: 0x%x\n", off.file_f_mode);

    RETURN_IF_ERROR(kernel_module::get_inode_i_rdev_offset(off.inode_i_rdev));
    printf("i_rdev offset: 0x%x\n", off.inode_i_rdev);

    if (kernel_module::is_kernel_version_less("4.14.0")) {
        RETURN_IF_ERROR(kernel_module::get_block_device_bd_part_offset(off._4_14_linux.bdev_bd_part));
        printf("bd_part offset: 0x%x\n", off._4_14_linux.bdev_bd_part);
        
        RETURN_IF_ERROR(kernel_module::get_block_device_bd_disk_offset(off._4_14_linux.bdev_bd_disk));
        printf("bd_disk offset: 0x%x\n", off._4_14_linux.bdev_bd_disk);
        
        RETURN_IF_ERROR(kernel_module::get_block_device_bd_disk_offset(off._4_14_linux.gendisk_part0));
        printf("part0 offset: 0x%x\n", off._4_14_linux.gendisk_part0);
    } else {
        RETURN_IF_ERROR(kernel_module::get_block_device_bd_partno_offset(off.bdev_bd_partno));
        printf("bd_partno offset: 0x%x\n", off.bdev_bd_partno);
    }

    PatchBase patchBase(comm_offset);
    PatchBlkdevOpen patchBlkdevOpen(patchBase, blkdev_open_sym);
    KModErr err = patchBlkdevOpen.patch_blkdev_open(dev_list, test_comm_name, control_kaddr, off);
    printf("patch blkdev_open addr: %p ret: %s\n", (void*)blkdev_open_sym, to_string(err).c_str());
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

// SKRoot模块入口函数
int skroot_module_main(const char* root_key, const char* module_private_dir) {
    std::string new_comm = process_utils::reset_random_process_name();
    if(new_comm.empty()) {
        printf("reset process name failed\n");
        return -1;
    }
    uint64_t control_kaddr = alloc_control_kaddr();
    if(!control_kaddr) {
        printf("alloc control kaddr failed\n");
        return -1;
    }

    std::vector<DevNodeInfo> dev_list;
    if (!scan_block_devices(dev_list)) {
        printf("scan block devices failed\n");
        return -1;
    }
    KModErr err = patch_kernel_handler(dev_list, new_comm, control_kaddr);
    printf("patch_kernel_handler ret: %s\n", to_string(err).c_str());
    if (!is_ok(err)) {
        printf("patch_kernel_handler failed\n");
        return -1;
    }
    if (!verify_protected_devices_not_writable(dev_list)) {
        printf("patch blkdev_open verify failed: protected block device still writable-openable\n");
        return -1;
    }
    printf("verify protect: success\n");
    process_utils::fork_delayed_task(20, [=] {
        char ch = '\x01';
        kernel_module::write_kernel_mem(control_kaddr, &ch, 1);
        printf("[module_protect_device] enable switch\n");
    });
    return 0;
}

// SKRoot 模块名片
SKROOT_MODULE_NAME("防格机 (防变黑砖)")
SKROOT_MODULE_VERSION("1.0.0")
SKROOT_MODULE_DESC("内核级防黑砖保护，拦截恶意程序对核心分区的写入，防止设备变黑砖。需提前手动备份/mnt/vendor/persist/，双清后可恢复正常开机。")
SKROOT_MODULE_AUTHOR("SKRoot")
SKROOT_MODULE_UUID32("BbA6MdJYoS2ggs58zU327m5ufBihCOrS")
SKROOT_MODULE_UPDATE_JSON("https://abcz316.github.io/SKRoot-linuxKernelRoot/module_protect_device/update.json")