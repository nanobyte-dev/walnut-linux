// SPDX-License-Identifier: GPL-2.0-only
/*
 * ps3guard_sim: ps3disk's write guard (packages/patched/kernel/linux/patches/local/0045) in vm-e2e.sh's x86 KVM guest, which
 * has no PS3 storage. Two bio-based disks named like the PS3's, each over a virtio disk:
 *
 *   ps3da  region 0 over da= (default /dev/vdb): read-only
 *   ps3db  region 1 over db= (default /dev/vda): writes only inside the ranges sealed in
 *          /sys/block/ps3db/write_guard/
 *
 * What the PS3 kernel will run is taken from the patch unchanged (extract.sh: ps3disk_guard.h,
 * guard-struct.inc, guard-patch.inc): struct ps3disk_guard, the region -> policy mapping, BLKROSET's
 * hook and the whole sysfs interface (policy, refused, ranges, sealed, nr_ranges, nr_sectors; the
 * seal and its module pin), behind the guest kernel's real sysfs (a write cut at its 4 KiB page).
 * The check per write is ps3disk_guard_refusal()'s decision on each bio here (ps3disk is blk-mq and
 * checks the request and each of its bios; a bio-based disk sees the bios one by one). Allowed bios
 * go on to the virtio disk unchanged: the sectors of ps3dX are those of the disk below.
 *
 * Test use only: built against the guest's (the host's) kernel by vm-e2e.sh, loaded in its guests.
 */
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>

#define PS3DISK_MAX_NUM_REGS	8
#define PS3DISK_MINORS		16

/* the pieces of ps3disk and the PS3 system bus that the patch's code touches */
struct ps3_system_bus_device {
	struct device core;
	void *drvdata;
};

struct ps3_storage_device {
	struct ps3_system_bus_device sbd;
	u64 blk_size;
};

static void *ps3_system_bus_get_drvdata(struct ps3_system_bus_device *d)
{
	return d->drvdata;
}

#include "ps3disk_guard.h"
#include "guard-struct.inc"

/* ps3disk's private data: only the guards here */
struct ps3disk_private {
	struct ps3disk_guard guard[PS3DISK_MAX_NUM_REGS];
};

#include "guard-patch.inc"

static char *da = "/dev/vdb", *db = "/dev/vda";
module_param(da, charp, 0444);
MODULE_PARM_DESC(da, "the disk under ps3da (region 0, read-only)");
module_param(db, charp, 0444);
MODULE_PARM_DESC(db, "the disk under ps3db (region 1, whitelist)");

static struct ps3_storage_device sim_dev = { .sbd.core.init_name = "ps3guard_sim", .blk_size = 512 };
static struct ps3disk_private sim_priv;
static int sim_major;
static struct {
	struct gendisk *disk;
	struct file *lower;
} sim[2];

static void sim_submit_bio(struct bio *bio)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	unsigned int idx = ps3disk_region_idx(disk);
	struct ps3disk_guard *g = ps3disk_guard_of(disk);
	enum req_op op = bio_op(bio);
	/* a read, or a flush without data (bio-based: an empty write with REQ_PREFLUSH) */
	bool harmless = op == REQ_OP_READ ||
		((op == REQ_OP_FLUSH || (op == REQ_OP_WRITE && (bio->bi_opf & REQ_PREFLUSH))) &&
		 !bio_sectors(bio));
	const char *why = NULL;

	switch (ps3disk_guard_decide(g->policy, smp_load_acquire(&g->sealed), harmless)) {
	case PS3DISK_GUARD_ALLOW:
		break;
	case PS3DISK_GUARD_CHECK_RANGES:
		if (!ps3disk_guard_contains(g->ranges, g->nr_ranges, bio->bi_iter.bi_sector,
					    bio_sectors(bio)))
			why = "outside the registered ranges";
		break;
	case PS3DISK_GUARD_REFUSE_UNSEALED:
		why = "no ranges sealed";
		break;
	default:
		why = "read-only region";
	}
	if (why) {
		atomic_long_inc(&g->refused);
		if (__ratelimit(&g->rs))
			pr_warn("ps3guard_sim %s: write guard refused %s of %u sectors at %llu: %s\n",
				disk->disk_name, blk_op_str(op), bio_sectors(bio),
				(unsigned long long)bio->bi_iter.bi_sector, why);
		bio_io_error(bio);
		return;
	}
	bio_set_dev(bio, file_bdev(sim[idx].lower));
	submit_bio_noacct(bio);
}

static const struct block_device_operations sim_fops = {
	.owner		= THIS_MODULE,
	.submit_bio	= sim_submit_bio,
	.set_read_only	= ps3disk_set_read_only,
};

static int sim_add(unsigned int idx, const char *path)
{
	struct queue_limits lim = {
		.logical_block_size	= 512,
		.features		= BLK_FEAT_WRITE_CACHE | BLK_FEAT_FUA,
	};
	struct ps3disk_guard *g = &sim_priv.guard[idx];
	struct gendisk *disk;
	int err;

	sim[idx].lower = bdev_file_open_by_path(path, BLK_OPEN_READ | BLK_OPEN_WRITE, &sim[idx], NULL);
	if (IS_ERR(sim[idx].lower)) {
		err = PTR_ERR(sim[idx].lower);
		sim[idx].lower = NULL;
		pr_err("ps3guard_sim: %s: %d\n", path, err);
		return err;
	}
	if (bdev_logical_block_size(file_bdev(sim[idx].lower)) != 512) {
		pr_err("ps3guard_sim: %s: not 512-byte blocks\n", path);
		return -EINVAL;
	}
	/* as ps3disk_probe(): the policy from the region index, set before the disk is visible */
	g->policy = ps3disk_region_policy(idx);
	mutex_init(&g->mutex);
	ratelimit_state_init(&g->rs, 5 * HZ, 10);

	disk = blk_alloc_disk(&lim, NUMA_NO_NODE);
	if (IS_ERR(disk))
		return PTR_ERR(disk);
	disk->major = sim_major;
	disk->first_minor = idx * PS3DISK_MINORS;
	disk->minors = 1;
	disk->flags |= GENHD_FL_NO_PART;
	disk->fops = &sim_fops;
	disk->private_data = &sim_dev;
	snprintf(disk->disk_name, DISK_NAME_LEN, "ps3d%c", 'a' + idx);
	set_capacity(disk, bdev_nr_sectors(file_bdev(sim[idx].lower)));
	if (g->policy == PS3DISK_GUARD_READONLY)
		set_disk_ro(disk, true);
	err = device_add_disk(NULL, disk, ps3disk_disk_groups);
	if (err) {
		put_disk(disk);
		return err;
	}
	sim[idx].disk = disk;
	pr_info("ps3guard_sim: %s (region %u) over %s, %llu sectors: write guard: %s\n", disk->disk_name, idx,
		path, (unsigned long long)get_capacity(disk),
		g->policy == PS3DISK_GUARD_READONLY ? "read-only" :
		g->policy == PS3DISK_GUARD_WHITELIST ?
		"writes only inside the ranges sealed in write_guard/" : "writable");
	return 0;
}

static void sim_remove(void)
{
	for (int i = 1; i >= 0; i--) {
		if (sim[i].disk) {
			del_gendisk(sim[i].disk);
			put_disk(sim[i].disk);
			sim[i].disk = NULL;
		}
		if (sim[i].lower) {
			fput(sim[i].lower);
			sim[i].lower = NULL;
		}
		kvfree(sim_priv.guard[i].ranges);
		sim_priv.guard[i].ranges = NULL;
	}
}

static int __init sim_init(void)
{
	int err;

	sim_dev.sbd.drvdata = &sim_priv;
	sim_major = register_blkdev(0, "ps3guard_sim");
	if (sim_major < 0)
		return sim_major;
	err = sim_add(0, da);
	if (!err)
		err = sim_add(1, db);
	if (err) {
		sim_remove();
		unregister_blkdev(sim_major, "ps3guard_sim");
	}
	return err;
}

static void __exit sim_exit(void)
{
	sim_remove();
	unregister_blkdev(sim_major, "ps3guard_sim");
}

module_init(sim_init);
module_exit(sim_exit);
MODULE_DESCRIPTION("ps3disk's write guard (local/0045) on virtio disks, for the PC's KVM tests");
MODULE_LICENSE("GPL");
