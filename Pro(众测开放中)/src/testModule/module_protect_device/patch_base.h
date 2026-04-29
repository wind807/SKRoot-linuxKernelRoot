#pragma once
#include <iostream>
#include <vector>
#include "kernel_module_kit_umbrella.h"
#ifndef MY_TASK_COMM_LEN
#define MY_TASK_COMM_LEN 16
#endif

class PatchBase {
public:
	PatchBase(uint32_t comm_offset);
	PatchBase(const PatchBase& other);
	~PatchBase();
protected:
	void emit_check_current_comm_name_to_x10(Assembler* a, const std::string& comm_name);
	KModErr patch_kernel_before_hook(uint64_t kaddr, const asmjit::a64::Assembler* a);
	KModErr patch_kernel_after_hook(uint64_t kaddr, const asmjit::a64::Assembler* a);

private:
	uint32_t m_comm_offset = 0;
};
