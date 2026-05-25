#include "persist_dir_perm_manager.h"

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "fd_guard.h"
#include "kernel_struct_path_helper.h"
#include "process_utils.h"

using namespace asmjit;
using namespace asmjit::a64;
using namespace asmjit::a64::Predicate;

#define	EACCES		13	/* Permission denied */

#define IOP_FASTPERM	0x0001
#define IOP_LOOKUP	0x0002
#define IOP_NOFOLLOW	0x0004
#define IOP_XATTR	0x0008
#define IOP_DEFAULT_READLINK	0x0010

namespace {
void emit_check_current_comm_name_to_x10(Assembler* a, uint32_t comm_offset, const std::string& comm_name) {
	uint64_t comm_name_arr[2] = {0};
	size_t copy_len = std::min(comm_name.length(), (size_t)(MY_TASK_COMM_LEN - 1));
	memcpy(comm_name_arr, comm_name.c_str(), copy_len);
	
	Label label_end = a->newLabel();

	a->mov(x10, Imm(0));
	kernel_module::export_symbol::get_current(a, x11);
	a->cbz(x11, label_end);

	// 比较下进程名，放行白名单进程名。
	aarch64_asm_mov_w(a, w12, comm_offset);
	a->add(x11, x11, x12); // 指针往后推
	a->ldr(x12, ptr(x11, 0));
	aarch64_asm_mov_x(a, x13, comm_name_arr[0]);
	a->cmp(x12, x13);
    a->b(CondCode::kNE, label_end);

	a->ldr(x12, ptr(x11, 8));
    aarch64_asm_mov_x(a, x13, comm_name_arr[1]);
    a->cmp(x12, x13);
    a->b(CondCode::kNE, label_end);
	a->mov(x10, Imm(1));
	a->bind(label_end);
}

} // namespace

std::vector<uint8_t> PersistDirPermManager::generate_permission_fn_bytes(uint64_t control_kaddr, uint32_t permission_kcfi, uint32_t comm_offset, const std::string& test_comm_name) {
    aarch64_asm_ctx asm_ctx = init_aarch64_asm();
    auto a = asm_ctx.assembler();
    Label L_denied = a->newLabel();
	a->embed(reinterpret_cast<const uint8_t*>(&permission_kcfi), sizeof(permission_kcfi));
    aarch64_asm_bit_jc(a);

    aarch64_asm_mov_x(a, x10, control_kaddr);
    a->ldrb(w10, ptr(x10));
    a->cbnz(w10, L_denied);
    
    emit_check_current_comm_name_to_x10(a, comm_offset, test_comm_name);
	a->cbnz(x10, L_denied); // 测试程序，主动拦截。

    a->mov(x0, xzr);
    a->ret(x30);

    a->bind(L_denied);
    a->mov(x0, Imm(-EACCES));
    a->ret(x30);
    return aarch64_asm_to_bytes(a);
}

KModErr PersistDirPermManager::patch_kernel_handler(uint64_t control_kaddr) {
    std::string new_comm = process_utils::reset_random_process_name();
    uint32_t comm_offset = 0;
    RETURN_IF_ERROR(kernel_module::get_task_struct_comm_offset(comm_offset));
    printf("comm offset: 0x%x\n", comm_offset);
    
    uint32_t permission_kcfi = 0;
    RETURN_IF_ERROR(kernel_module::get_inode_operations_permission_kcfi_hash(permission_kcfi));

    std::vector<uint8_t> permission_fn_bytes = generate_permission_fn_bytes(control_kaddr, permission_kcfi, comm_offset, new_comm);
    uint64_t permission_fn_head_kaddr = 0;
    uint64_t permission_fn_entry_kaddr = 0;
    RETURN_IF_ERROR(kernel_module::alloc_kernel_mem(permission_fn_bytes.size(), permission_fn_head_kaddr));
    RETURN_IF_ERROR(kernel_module::write_kernel_mem(permission_fn_head_kaddr, permission_fn_bytes.data(), permission_fn_bytes.size()));
    RETURN_IF_ERROR(kernel_module::set_kernel_memory_protection(permission_fn_head_kaddr, permission_fn_bytes.size(), kernel_module::KernMemProt::KMP_X));
    permission_fn_entry_kaddr = permission_fn_head_kaddr + 4;

    const char* persist_dir = nullptr;
    int fd = -1;
    for (const char* dir : kPersistDirs) {
        fd = ::open(dir, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            persist_dir = dir;
            break;
        }
        printf("open failed: %s, errno=%d\n", dir, errno);
    }
    if (fd < 0 || !persist_dir) {
        printf("open all persist dirs failed\n");
        return KModErr::ERR_MODULE_OPEN_DIR;
    }
    fd_guard f(fd);
    scoped_kpath kpath;
    RETURN_IF_ERROR(acquire_kpath(persist_dir, kpath));

    uint32_t d_inode_offset = 0;
    RETURN_IF_ERROR(kernel_module::get_dentry_d_inode_offset(d_inode_offset));

    uint64_t inode_ptr = 0;
    RETURN_IF_ERROR(kernel_module::read_kernel_mem(kpath.raw.dentry + d_inode_offset,&inode_ptr, sizeof(inode_ptr)));
    printf("inode_ptr: %lx\n", inode_ptr);

    uint32_t i_op_offset = 0;
    RETURN_IF_ERROR(kernel_module::get_inode_i_op_offset(i_op_offset));
    printf("i_op_offset: %u\n", i_op_offset);

    uint64_t i_op_ptr = 0;
    RETURN_IF_ERROR(kernel_module::read_kernel_mem(inode_ptr + i_op_offset, &i_op_ptr, sizeof(i_op_ptr)));
    printf("i_op_ptr: %lx\n", i_op_ptr);

    uint32_t inode_operations_permission_offset = 0;
    RETURN_IF_ERROR(kernel_module::get_inode_operations_permission_offset(inode_operations_permission_offset));
    printf("inode_operations_permission_offset: %u\n", inode_operations_permission_offset);

    uint64_t fake_i_op_kaddr = 0;
    RETURN_IF_ERROR(kernel_module::alloc_kernel_mem(1024, fake_i_op_kaddr));
    RETURN_IF_ERROR(kernel_module::fill00_kernel_mem(fake_i_op_kaddr, 1024));

    std::vector<uint8_t> i_op_backup(512);
    RETURN_IF_ERROR(kernel_module::read_kernel_mem(i_op_ptr, i_op_backup.data(), i_op_backup.size()));
    RETURN_IF_ERROR(kernel_module::write_kernel_mem(fake_i_op_kaddr, i_op_backup.data(), i_op_backup.size()));
    RETURN_IF_ERROR(kernel_module::write_kernel_mem(fake_i_op_kaddr + inode_operations_permission_offset, &permission_fn_entry_kaddr, sizeof(permission_fn_entry_kaddr)));
    printf("fake_i_op_kaddr: %lx\n", fake_i_op_kaddr);

    RETURN_IF_ERROR(kernel_module::write_kernel_rw_mem_atomic64(inode_ptr + i_op_offset, fake_i_op_kaddr));

    uint32_t i_opflags_offset = 0;
    RETURN_IF_ERROR(kernel_module::get_inode_i_opflags_offset(i_opflags_offset));
    printf("i_opflags_offset: %u\n", i_opflags_offset);

    uint16_t i_opflags = 0;
    RETURN_IF_ERROR(kernel_module::read_kernel_mem(inode_ptr + i_opflags_offset, &i_opflags, sizeof(i_opflags)));
    printf("i_opflags before: 0x%x\n", i_opflags);

    i_opflags &= ~IOP_FASTPERM;
    RETURN_IF_ERROR(kernel_module::write_kernel_mem(inode_ptr + i_opflags_offset, &i_opflags, sizeof(i_opflags)));
    printf("i_opflags after: 0x%x\n", i_opflags);

    if (!verify_persist_not_open(persist_dir)) {
        printf("verify failed: persist dir still openable\n");
        return KModErr::ERR_MODULE_OPEN_DIR;
    }
    printf("verify persist dir: success\n");
    return KModErr::OK;
}

uint64_t PersistDirPermManager::alloc_control_kaddr() {
    uint64_t control_kaddr = 0;
    if (is_ok(kernel_module::alloc_kernel_mem(1, control_kaddr))) {
        kernel_module::fill00_kernel_mem(control_kaddr, 1);
    }
    return control_kaddr;
}

bool PersistDirPermManager::init() {
    if (m_control_kaddr) {
        printf("PersistDirPermManager already initialized, control_kaddr=%lx\n", m_control_kaddr);
        return true;
    }

    m_control_kaddr = alloc_control_kaddr();
    if (!m_control_kaddr) {
        printf("alloc control kaddr failed\n");
        return false;
    }

    KModErr err = patch_kernel_handler(m_control_kaddr);
    printf("patch_kernel_handler ret: %s\n", to_string(err).c_str());

    if (!is_ok(err)) {
        printf("patch_kernel_handler failed\n");
        // kernel_module::free_kernel_mem(m_control_kaddr);
        m_control_kaddr = 0;
        return false;
    }

    return true;
}

bool PersistDirPermManager::lock() {
    return set_locked(true);
}

bool PersistDirPermManager::unlock() {
    return set_locked(false);
}

bool PersistDirPermManager::set_locked(bool locked) {
    if (!m_control_kaddr) {
        printf("PersistDirPermManager not initialized\n");
        return false;
    }
    char ch = locked ? '\x01' : '\x00';
    KModErr err = kernel_module::write_kernel_mem(m_control_kaddr, &ch, sizeof(ch));
    if (!is_ok(err)) {
        printf("write lock control failed: %s\n", to_string(err).c_str());
        return false;
    }
    printf("persist dir %s!\n", locked ? "lock" : "unlock");
    return true;
}

bool PersistDirPermManager::verify_persist_not_open(const char* persist_dir) {
    errno = 0;
    int fd = open(persist_dir, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        printf("verify_protect: FAILED, open allowed, path=%s\n", persist_dir);
        close(fd);
        return false;
    }
    if (errno != EACCES) {
        printf("verify_protect: unexpected fail, path=%s errno=%d(%s)\n", persist_dir, errno, strerror(errno));
        return false;
    }
    return true;
}