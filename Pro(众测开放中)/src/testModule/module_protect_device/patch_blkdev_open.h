#pragma once
#include <iostream>
#include <set>
#include "patch_base.h"

struct DevNodeInfo {
    char name[256] = {0};
    uint32_t kernel_rdev = 0;
};

struct BlkdevOpenPatchOffsets {
    uint32_t file_f_mode = 0;
    uint32_t inode_i_rdev = 0;
    uint32_t bdev_bd_partno = 0;
	struct {
 		uint32_t bdev_bd_part = 0;
   		uint32_t bdev_bd_disk = 0;
    	uint32_t gendisk_part0 = 0;
	} _4_14_linux;
};

class PatchBlkdevOpen : public PatchBase {
public:
	PatchBlkdevOpen(const PatchBase& patch_base, uint64_t blkdev_open);
	~PatchBlkdevOpen();

	KModErr patch_blkdev_open(const std::vector<DevNodeInfo> & protect_dev, const std::string& test_comm_name, uint64_t control_kaddr, const BlkdevOpenPatchOffsets& off);
private:
	uint64_t m_blkdev_open = 0;
};