#include "patch_base.h"
using namespace asmjit;
using namespace asmjit::a64;
using namespace asmjit::a64::Predicate;

PatchBase::PatchBase(uint32_t comm_offset) : m_comm_offset(comm_offset) {}

PatchBase::PatchBase(const PatchBase& other) : m_comm_offset(other.m_comm_offset) {}

PatchBase::~PatchBase() {}


void PatchBase::emit_check_current_comm_name_to_x10(Assembler* a, const std::string& comm_name) {
	uint64_t comm_name_arr[2] = {0};
	size_t copy_len = std::min(comm_name.length(), (size_t)(MY_TASK_COMM_LEN - 1));
	memcpy(comm_name_arr, comm_name.c_str(), copy_len);
	
	Label label_end = a->newLabel();

	a->mov(x10, Imm(0));
	kernel_module::export_symbol::get_current(a, x11);
	a->cbz(x11, label_end);

	// 比较下进程名，放行白名单进程名。
	aarch64_asm_mov_w(a, w12, m_comm_offset);
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

KModErr PatchBase::patch_kernel_before_hook(uint64_t kaddr, const Assembler* a) {
	std::vector<uint8_t> bytes = aarch64_asm_to_bytes(a);
	RETURN_IF_ERROR(kernel_module::install_kernel_function_before_hook(kaddr, bytes));
	return KModErr::OK;
}

KModErr PatchBase::patch_kernel_after_hook(uint64_t kaddr, const Assembler* a) {
	std::vector<uint8_t> bytes = aarch64_asm_to_bytes(a);
	RETURN_IF_ERROR(kernel_module::install_kernel_function_after_hook(kaddr, bytes));
	return KModErr::OK;
}