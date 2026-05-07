#pragma once
#include <iostream>
#include <vector>
#include "kernel_module_kit_umbrella.h"

#ifndef MY_TASK_COMM_LEN
#define MY_TASK_COMM_LEN 16
#endif

class PatchBase {
public:
	PatchBase();
	PatchBase(const PatchBase& other);
	~PatchBase();
protected:
	KModErr patch_kernel_before_hook(uint64_t kaddr, const asmjit::a64::Assembler* a);
	KModErr patch_kernel_after_hook(uint64_t kaddr, const asmjit::a64::Assembler* a);
};
