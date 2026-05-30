#pragma once
#include <iostream>
#include <set>
#include "patch_base.h"

struct InodePatchOffsets {
	uint32_t comm_offset = 0;
	uint32_t inode_i_ino = 0;
	uint32_t inode_i_sb = 0;
	uint32_t super_block_s_dev = 0;
};

class PatchSelinuxInodePermission : public PatchBase {
public:
	PatchSelinuxInodePermission(const PatchBase& patch_base, uint64_t selinux_inode_permission);
	~PatchSelinuxInodePermission();

	KModErr patch_selinux_inode_permission(uint64_t target_i_ino, uint32_t target_s_dev, uint64_t control_kaddr, const std::string& test_comm, const InodePatchOffsets& off);
private:
	uint64_t m_selinux_inode_permission = 0;
};