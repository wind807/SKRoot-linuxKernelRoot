#pragma once

#include <cstdint>
#include <vector>

#include "kernel_module_kit_umbrella.h"

static constexpr const char* kPersistDirs[] = {
    "/mnt/vendor/persist/data",
    "/mnt/vendor/nvdata",
};

class PersistDirPermManager {
public:
    bool init();
    bool lock();
    bool unlock();
    uint64_t control_kaddr();
private:
    std::vector<uint8_t> generate_permission_fn_bytes(uint64_t control_kaddr, uint32_t permission_kcfi, uint32_t comm_offset, const std::string& test_comm_name);
    KModErr patch_kernel_handler(uint64_t control_kaddr);
    uint64_t alloc_control_kaddr();
    bool set_locked(bool locked);
    bool verify_persist_not_open(const char* persist_dir);

private:
    uint64_t m_control_kaddr = 0;
};