#pragma once
#include <iostream>
#include <vector>
#include "kernel_module_kit_umbrella.h"

#ifndef MY_TASK_COMM_LEN
#define MY_TASK_COMM_LEN 16
#endif

class PatchBase {
public:
	PatchBase(uint32_t task_struct_offset_cred, uint32_t cred_euid_offset, uint32_t comm_offset, const std::string& whitelist_comm_name);
	PatchBase(const PatchBase& other);
	~PatchBase();
protected:
	uint32_t m_task_struct_offset_cred = 0;
	uint32_t m_cred_euid_offset = 0;
	uint32_t m_comm_offset = 0;
	std::string m_whitelist_comm_name;

	void emit_check_current_allow_visible_to_x10(asmjit::a64::Assembler* a);

	KModErr patch_kernel_before_hook(uint64_t kaddr, const asmjit::a64::Assembler* a);
	KModErr patch_kernel_after_hook(uint64_t kaddr, const asmjit::a64::Assembler* a);
};
