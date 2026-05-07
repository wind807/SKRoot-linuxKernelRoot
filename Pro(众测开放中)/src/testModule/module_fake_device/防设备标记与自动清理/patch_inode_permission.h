#pragma once
#include <iostream>
#include <set>
#include "patch_base.h"

struct InodePermissionPatchOffsets {
	uint32_t inode_i_ino = 0;
	uint32_t inode_i_sb = 0;
	uint32_t super_block_s_dev = 0;
};

class PatchInodePermission : public PatchBase {
public:
	PatchInodePermission(const PatchBase& patch_base, uint64_t inode_permission);
	~PatchInodePermission();

	KModErr patch_inode_permission(uint64_t target_i_ino, uint32_t target_s_dev, uint64_t control_kaddr, const InodePermissionPatchOffsets& off);
private:
	uint64_t m_inode_permission = 0;
};