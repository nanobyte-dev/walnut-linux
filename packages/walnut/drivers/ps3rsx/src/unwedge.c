// SPDX-License-Identifier: GPL-2.0
/*
 * In-place recovery after a graph exception (docs/DRIVER.md "Recovery", docs/RSX.md "Recovering
 * from a graph exception"). A trap stops the RSX: lv1 takes the exception, turns the FIFO's puller
 * and pusher off, clears PFIFO's and PGRAPH's interrupt enables and leaves our driver context with
 * broken state. Recovery is on by default (recovery=0 at load turns it off), once per boot:
 *  0. the crash record (below) and the faulting client: the commit the reference register hasn't
 *     reached (blame.h), never simply the last submitter; it is refused from now on (-ENODEV);
 *  1. reads first, through hcall 114 (lv1_map_physical_address_region) of three 4 KB register
 *     pages (0x28000002000, 0x28000003000: PFIFO; 0x28000400000: PGRAPH): nothing is written if
 *     PFIFO's CACHE1 holds channel 0 (ps3fb's). ps3fb is told to make no lv1 teardown and no
 *     handback from now on (ps3fb_gpu_recover_begin);
 *  2. PGRAPH FIFO (0x400720) := 1 if 0; PFIFO INTR_EN_0 (0x2140) := 0x017d1111 and NV40 PGRAPH
 *     INTR_EN (0x40013c) := 0x03111fd1, the healthy values, if 0 and nothing is pending;
 *  3. ps3fb forgets (leaks, never frees) the faulted driver context and allocates a new one; the
 *     GART is mapped onto it again;
 *  4. before its FIFO_SETUP: nouveau nv04_chan_stop's null context for the channel CACHE1 holds
 *     (never channel 0), dropping its stuck entries (PUSH1 := 0x1f);
 *  5. after it: CACHE1_HASH := 0 (nouveau nv04_fifo_pause), PUSH0 |= 1, PULL0 |= 1, CACHES := 1;
 *     the reset program runs. The first new context after a trap can fail its notifier lookup
 *     (method 0x180, 0x66604200): CACHE1 is parked off it, ps3fb frees it and allocates another
 *     (ps3fb_gpu_ctx_realloc, the recovery's only lv1_gpu_context_free), and the steps repeat;
 *  6. the new context's reports area is seeded from the old one and the clients' mappings of it
 *     are moved (mem.c), the vsync interrupt is moved, every client is told once (-ECANCELED, or
 *     -EIO without PS3_RECOVER), and a watchdog starts: lv1 takes the CACHE_ERRORs (software
 *     object methods) after a trap but leaves the puller off, so PULL0 := 1 when PULL0 bit 0 is
 *     clear and INTR_0 is 0 on two polls with the same CACHE1 GET, CACHE1 not holding channel 0;
 *     off at a new graph exception.
 * Registers are nouveau's (nvkm/engine/fifo/nv04.c, regsnv04.h; gr/nv40.c, gr/regs.h); each
 * access is logged and goes into the crash record. Never channel 0's state.
 * A second exception in the same boot, or a failed step, leaves display-only mode: no 3D until
 * reboot, KMS stays (dumb buffers and lv1 flips work on a wedged RSX), Mesa falls back to
 * software. Every exception gets a crash record (/sys/devices/platform/ps3rsx/crash_report, root
 * only) and a uevent (RSX_RECOVERED, RSX_RECOVERY_FAILED, RSX_DISPLAY_ONLY_FORCED) for ps3-system's
 * crash report service and ps3-desktop's dialog.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <drm/drm_print.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <linux/utsname.h>
#include <linux/vmalloc.h>
#include <asm/ps3.h>
#include <drm/drm_file.h>
#include <drm/drm_device.h>
#include <asm/lv1call.h>
#include <asm/ps3gpu.h>

#include "ps3rsx.h"

static bool recovery = true;
module_param(recovery, bool, 0444);
MODULE_PARM_DESC(recovery, "recover the GPU in place after a graph exception, once per boot (default on)");
static bool recovery_used;	/* one in-place recovery per boot (lv1's context limit) */
static char *recs;		/* the crash records (below) */
bool ps3rsx_test_allow_rt12;
#ifdef PS3RSX_TEST_WEDGE
/* only in the module for the debug kernel (debian/rules: PS3RSX_TEST_WEDGE=1 for a -debug release) */
module_param_named(test_allow_rt_enable_12, ps3rsx_test_allow_rt12, bool, 0600);
MODULE_PARM_DESC(test_allow_rt_enable_12, "regression tests of the recovery: the validator lets RT_ENABLE 0x12 through, which raises a graph exception (root only, off by default, cleared by every recovery)");
#endif

#define RSX_MMIO	0x28000000000ULL
/*
 * Every UW line goes to the kernel log and into the crash record being written (below), readable
 * as /sys/devices/platform/ps3rsx/crash_report (root only).
 */
static void rec_line(const char *line);
static __printf(1, 2) void uw_log(const char *fmt, ...)
{
	char line[240];
	va_list ap;

	va_start(ap, fmt);
	vscnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	pr_info("ps3rsx unwedge: %s\n", line);
	rec_line(line);
}
#define UW(fmt, ...)	uw_log(fmt, ##__VA_ARGS__)

static const u64 pages[3] = { 0x2000, 0x3000, 0x400000 };
static u64 page_lpar[3];
static void __iomem *page_va[3];
static bool active, mapped;
static struct delayed_work watchdog;
static unsigned long nudges;
static unsigned int exc_at;	/* ps3fb's exception count at recovery: a new one stops the watchdog */
static u32 last_get = ~0u;

static void __iomem *reg(unsigned long off)
{
	int i;

	for (i = 0; i < 3; i++)
		if ((off & ~0xfffUL) == pages[i] && page_va[i])
			return page_va[i] + (off & 0xfff);
	return NULL;
}

/* the recovery's register accesses, each logged */
static u32 rd(unsigned long off, const char *name)
{
	u32 v;

	v = in_be32(reg(off));
	UW("read32 %s (%#lx) = %#010x", name, off, v);
	return v;
}

static void wr(unsigned long off, const char *name, u32 v)
{
	out_be32(reg(off), v);
	UW("write32 %s (%#lx) := %#x", name, off, v);
}

bool ps3rsx_uw_active(void)
{
	return READ_ONCE(active);
}

static int map_pages(void)
{
	s64 s;
	int i;

	for (i = 0; i < 3; i++) {
		s = lv1_undocumented_function_114(RSX_MMIO + pages[i], 12, 0x1000, &page_lpar[i]);
		UW("lv1_map_physical_address_region(%#llx, 12, 0x1000) [hcall 114]: %lld",
		   RSX_MMIO + pages[i], s);
		if (s)
			return -EIO;
		page_va[i] = ioremap(page_lpar[i], 0x1000);
		if (!page_va[i])
			return -ENOMEM;
	}
	mapped = true;
	return 0;
}

static void unmap_pages(void)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (page_va[i])
			iounmap(page_va[i]);
		page_va[i] = NULL;
		if (page_lpar[i])
			lv1_undocumented_function_115(page_lpar[i]);
		page_lpar[i] = 0;
	}
	mapped = false;
}

/* step 4: CACHE1 to the null context before our FIFO_SETUP (never if it holds channel 0) */
static int ps3rsx_uw_pre_setup(void)
{
	u32 push1;

	if (!ps3rsx_uw_active())
		return 0;
	push1 = rd(0x3204, "NV03_PFIFO_CACHE1_PUSH1");
	if ((push1 & 0x1f) == 0x1f)
		return 0;
	if ((push1 & 0x1f) == 0) {
		UW("CACHE1 holds channel 0: no null swap");
		return -EBUSY;
	}
	wr(0x2500, "NV03_PFIFO_CACHES", 0);
	/* nouveau nv04_chan_stop reads the channel after CACHES := 0 */
	push1 = rd(0x3204, "NV03_PFIFO_CACHE1_PUSH1");
	if ((push1 & 0x1f) == 0) {
		wr(0x2500, "NV03_PFIFO_CACHES", 1);
		UW("CACHE1 switched to channel 0: no null swap");
		return -EBUSY;
	}
	if ((push1 & 0x1f) == 0x1f) {
		wr(0x2500, "NV03_PFIFO_CACHES", 1);
		return 0;
	}
	wr(0x3220, "NV04_PFIFO_CACHE1_DMA_PUSH", rd(0x3220, "NV04_PFIFO_CACHE1_DMA_PUSH") & ~1u);
	wr(0x3200, "NV03_PFIFO_CACHE1_PUSH0", 0);
	wr(0x3250, "NV04_PFIFO_CACHE1_PULL0", rd(0x3250, "NV04_PFIFO_CACHE1_PULL0") & ~1u);
	wr(0x3270, "NV03_PFIFO_CACHE1_GET", 0);
	wr(0x3210, "NV03_PFIFO_CACHE1_PUT", 0);
	wr(0x3204, "NV03_PFIFO_CACHE1_PUSH1", 0x1f);
	wr(0x3200, "NV03_PFIFO_CACHE1_PUSH0", 1);
	wr(0x3250, "NV04_PFIFO_CACHE1_PULL0", 1);
	wr(0x2500, "NV03_PFIFO_CACHES", 1);
	return 0;
}

/* step 5: after our FIFO_SETUP (CACHE1 holds no channel, or ours) */
static void ps3rsx_uw_post_setup(u32 chid)
{
	u32 push1;

	if (!ps3rsx_uw_active())
		return;
	push1 = rd(0x3204, "NV03_PFIFO_CACHE1_PUSH1");
	if ((push1 & 0x1f) != 0x1f && (push1 & 0x1f) != chid) {
		UW("CACHE1 holds channel %u, not ours (%u) or none: no write", push1 & 0x1f, chid);
		return;
	}
	wr(0x3258, "NV04_PFIFO_CACHE1_HASH", 0);
	wr(0x3200, "NV03_PFIFO_CACHE1_PUSH0", rd(0x3200, "NV03_PFIFO_CACHE1_PUSH0") | 1);
	wr(0x3250, "NV04_PFIFO_CACHE1_PULL0", rd(0x3250, "NV04_PFIFO_CACHE1_PULL0") | 1);
	if (rd(0x2500, "NV03_PFIFO_CACHES") != 1)
		wr(0x2500, "NV03_PFIFO_CACHES", 1);
}

/* step 6: the puller stopped by lv1 after a CACHE_ERROR it took: start it again */
/*
 * lv1 takes the CACHE_ERRORs after a trap but does only the first half of nouveau's
 * nv04_fifo_intr_cache_error() (DMA_PUSH := 0, PUSH0 off): it leaves the pusher, push access and
 * the puller off (seen on the PS3, 2026-11-01). The second half, as nouveau writes it: PUSH0 |= 1,
 * DMA_PUSH |= 1, PULL0 := 1. Only with INTR_0 0, the same CACHE1 GET on two polls, and CACHE1
 * not holding channel 0 (PUSH1 read last).
 */
static bool ps3rsx_uw_nudge(void)
{
	u32 pull0, intr, push1, get, push0, dpush;

	if (!ps3rsx_uw_active() || !mapped || ps3fb_gpu_exception_seen() != exc_at)
		return false;
	pull0 = in_be32(reg(0x3250));
	push0 = in_be32(reg(0x3200));
	dpush = in_be32(reg(0x3220));
	intr = in_be32(reg(0x2100));
	get = in_be32(reg(0x3270));
	if (((pull0 & 1) && (push0 & 1) && (dpush & 1)) || intr) {
		last_get = ~0u;
		return false;
	}
	/* stopped at the same entry for two polls: not the middle of lv1's own handler */
	if (get != last_get) {
		last_get = get;
		return false;
	}
	push1 = in_be32(reg(0x3204));	/* last, right before the writes */
	if ((push1 & 0x1f) == 0)
		return false;		/* channel 0 (ps3fb's) in CACHE1: hands off */
	if (!(push0 & 1))
		out_be32(reg(0x3200), push0 | 1);
	if (!(dpush & 1))
		out_be32(reg(0x3220), dpush | 1);
	if (!(pull0 & 1))
		out_be32(reg(0x3250), 1);
	last_get = ~0u;
	nudges++;
	pr_warn_ratelimited("ps3rsx unwedge: FIFO restarted (PULL0 %#x PUSH0 %#x DMA_PUSH %#x, channel %u, %lu so far)\n",
			    pull0, push0, dpush, push1 & 0x1f, nudges);
	return true;
}

static void watchdog_fn(struct work_struct *w)
{
	if (ps3fb_gpu_exception_seen() != exc_at) {
		UW("a new graph exception: watchdog off, no more nudges");
		return;
	}
	ps3rsx_uw_nudge();
	schedule_delayed_work(&watchdog, msecs_to_jiffies(20));
}

void ps3rsx_uw_start(void)
{
	if (!ps3rsx_uw_active())
		return;
	INIT_DELAYED_WORK(&watchdog, watchdog_fn);
	schedule_delayed_work(&watchdog, msecs_to_jiffies(20));
	UW("watchdog on (PULL0 every 20 ms)");
}

void ps3rsx_uw_stop(void)
{
	if (!ps3rsx_uw_active())
		return;
	cancel_delayed_work_sync(&watchdog);
	UW("watchdog off after %lu nudges; the pages stay mapped until module exit", nudges);
}

void ps3rsx_uw_exit(void)
{
	vfree(recs);
	recs = NULL;
	if (mapped)
		unmap_pages();
}

/* ====================================== crash record ===================================== */

#define REC_N		4		/* the last records, oldest dropped */
#define REC_SIZE	(64 * 1024)

static char *recs;			/* REC_N * REC_SIZE */
static size_t rec_len[REC_N];
static unsigned int rec_cur, rec_count;	/* the one being written; how many ever */
static bool rec_open;
static DEFINE_SPINLOCK(rec_lock);

static void rec_line(const char *line)
{
	unsigned long flags;
	size_t room;

	spin_lock_irqsave(&rec_lock, flags);
	if (recs && rec_open) {
		room = REC_SIZE - rec_len[rec_cur];
		if (room > 1)
			rec_len[rec_cur] += scnprintf(recs + rec_cur * REC_SIZE + rec_len[rec_cur],
						      room, "%s\n", line);
	}
	spin_unlock_irqrestore(&rec_lock, flags);
}

static __printf(1, 2) void rec_printf(const char *fmt, ...)
{
	char line[240];
	va_list ap;

	va_start(ap, fmt);
	vscnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	rec_line(line);
}

/* the exception block as ps3fb's struct gpu_driver_info has it (ps3fb.c) */
struct uw_exc {
	u32 channel_id, cause, res1[8], dma_put, dma_get, call, jump, res2;
	u32 fifo_put, fifo_get, fifo_ref, fifo_cache[512], graph_fifo[512];
};
struct uw_dinfo {
	u32 version_driver, version_gpu, memory_size, hardware_channel;
	u32 nvcore_frequency, memory_frequency, reserved[6], labelArea, reportDataArea;
	u32 reserved2[1056];
	struct { u64 ts; u32 status, offset, res1, res2, field, reserved1; u64 res3; u32 raster;
		 u64 vblank_count; u32 fieldVsync, reserved2; } head[8];
	struct { u32 irq_outlet, status, mask, video_cause, graph_cause, user_cause, res[8];
		 struct uw_exc exc; } irq;
};

static void rec_begin(struct ps3rsx_device *rsx)
{
	struct uw_dinfo __iomem *d = rsx->dinfo;
	struct uw_exc __iomem *e = &d->irq.exc;
	union ps3_firmware_version fw = {};
	unsigned long flags;
	struct tm tm;
	u32 get, put, i;

	spin_lock_irqsave(&rec_lock, flags);
	rec_cur = rec_count++ % REC_N;
	rec_len[rec_cur] = 0;
	rec_open = true;
	spin_unlock_irqrestore(&rec_lock, flags);
	ps3_get_firmware_version(&fw);
	time64_to_tm(ktime_get_real_seconds(), 0, &tm);
	rec_printf("=== ps3rsx graph exception record #%u: %04ld-%02d-%02d %02d:%02d:%02d UTC", rec_count,
		   tm.tm_year + 1900L, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	rec_printf("kernel %s, ps3rsx %s, lv1 firmware %u.%u.%u, uptime %llu s",
		   utsname()->release, THIS_MODULE->srcversion ?: "?", fw.major, fw.minor, fw.rev,
		   ktime_get_boottime_ns() / NSEC_PER_SEC);
	rec_printf("faulting submit: channel %d, pid %d (%s); context %#llx, recovery %d, used %d",
		   rsx->fault_chan, rsx->fault_pid, rsx->fault_comm, rsx->ctx.handle, recovery,
		   recovery_used);
	if (!d)
		return;
	rec_printf("exception: channel %u cause %#x dma put %#x get %#x call %#x jump %#x res2 %#x",
		   in_be32(&e->channel_id), in_be32(&e->cause), in_be32(&e->dma_put),
		   in_be32(&e->dma_get), in_be32(&e->call), in_be32(&e->jump), in_be32(&e->res2));
	rec_printf("  res1 %08x %08x %08x %08x %08x %08x %08x %08x", in_be32(&e->res1[0]),
		   in_be32(&e->res1[1]), in_be32(&e->res1[2]), in_be32(&e->res1[3]),
		   in_be32(&e->res1[4]), in_be32(&e->res1[5]), in_be32(&e->res1[6]),
		   in_be32(&e->res1[7]));
	get = in_be32(&e->fifo_get) & 0x1ff;
	put = in_be32(&e->fifo_put) & 0x1ff;
	rec_printf("  fifo get %#x put %#x ref %#x; the 32 cache entries ending at GET (method:data, G/P marked):",
		   in_be32(&e->fifo_get), in_be32(&e->fifo_put), in_be32(&e->fifo_ref));
	for (i = 0; i < 32; i += 4) {
		char l[200];
		int n = 0, k;

		for (k = 0; k < 4; k++) {
			u32 idx = (get - 31 + i + k) & 0x1ff;

			n += scnprintf(l + n, sizeof(l) - n, " %c%08x:%08x",
				       idx == get && idx == put ? 'B' : idx == get ? 'G' :
				       idx == put ? 'P' : ' ', in_be32(&e->fifo_cache[idx]),
				       in_be32(&e->graph_fifo[idx]));
		}
		rec_printf("  [%03x]%s", (get - 31 + i) & 0x1ff, l);
	}
}

static void rec_end(struct ps3rsx_device *rsx, const char *outcome)
{
	unsigned long flags;

	rec_printf("=== outcome: %s", outcome);
	spin_lock_irqsave(&rec_lock, flags);
	rec_open = false;
	spin_unlock_irqrestore(&rec_lock, flags);
	pr_err("ps3rsx: graph exception record #%u: channel %d, pid %d (%s): %s; full record in /sys/devices/platform/ps3rsx/crash_report\n",
	       rec_count, rsx->fault_chan, rsx->fault_pid, rsx->fault_comm, outcome);
}

/* the record, oldest first */
static ssize_t crash_report_read(struct file *f, struct kobject *kobj, struct bin_attribute *a,
				 char *buf, loff_t off, size_t count)
{
	unsigned int n = min(rec_count, (unsigned int)REC_N), k, first = rec_count - n;
	unsigned long flags;
	loff_t pos = 0;
	size_t done = 0;

	spin_lock_irqsave(&rec_lock, flags);
	for (k = 0; k < n && done < count; k++) {
		unsigned int r = (first + k) % REC_N;
		size_t len = rec_len[r];

		if (off < pos + len) {
			size_t skip = off > pos ? off - pos : 0, c = min(len - skip, count - done);

			memcpy(buf + done, recs + r * REC_SIZE + skip, c);
			done += c;
			off += c;
		}
		pos += len;
	}
	spin_unlock_irqrestore(&rec_lock, flags);
	return done;
}
static struct bin_attribute crash_report_attr = {
	.attr = { .name = "crash_report", .mode = 0400 },
	.read = crash_report_read,
};

/* ===================================== in place ===================================== */

void ps3fb_gpu_handles(u64 *memory_handle, u64 *context_handle, u64 *ddr_lpar,
		       void **xdr, unsigned long *xdr_size);

static unsigned int recoveries, recovery_failed;
static bool display_only;	/* recovery failed: no 3D, KMS only (dumb buffers, lv1 flips) */

bool ps3rsx_uw_display_only(void)
{
	return READ_ONCE(display_only);
}

static ssize_t display_only_show(struct device *d, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(display_only));
}
static DEVICE_ATTR_RO(display_only);

/*
 * The escape hatch (Ctrl+Super+Alt+Shift+F12 held 5 s, read by ps3-system's root daemon
 * ps3-gpu-escape): "1" puts ps3rsx into display-only mode until reboot, wedged or not,
 * with a crash record and the RSX_DISPLAY_ONLY_FORCED uevent (the crash report service then
 * restarts the desktop without glamor). Root only (0200). No register access, no lv1 call.
 */
static void uevent_env(struct ps3rsx_device *rsx, char *what);
static ssize_t force_display_only_store(struct device *d, struct device_attribute *a,
					const char *buf, size_t count)
{
	struct ps3rsx_device *rsx = dev_get_drvdata(d);
	bool v;

	if (kstrtobool(buf, &v) || !v)
		return -EINVAL;
	if (READ_ONCE(display_only))
		return count;
	WRITE_ONCE(display_only, true);
	rsx->fault_chan = -1;
	rsx->fault_serial = 0;
	rsx->fault_pid = task_pid_nr(current);
	get_task_comm(rsx->fault_comm, current);
	rec_begin(rsx);
	UW("display-only mode forced (the escape hatch, by pid %d %s): no 3D until reboot",
	   rsx->fault_pid, rsx->fault_comm);
	rec_end(rsx, "display-only forced by the escape hatch");
	uevent_env(rsx, "RSX_DISPLAY_ONLY_FORCED=1");
	return count;
}
static DEVICE_ATTR_WO(force_display_only);

static ssize_t recoveries_show(struct device *d, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", READ_ONCE(recoveries));
}
static DEVICE_ATTR_RO(recoveries);

static ssize_t recovery_failed_show(struct device *d, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", READ_ONCE(recovery_failed));
}
static DEVICE_ATTR_RO(recovery_failed);

static void uevent_env(struct ps3rsx_device *rsx, char *what)
{
	char n[32];
	char *env[] = { what, n, NULL };

	snprintf(n, sizeof(n), "RECOVERIES=%u", READ_ONCE(recoveries));
	if (rsx->drm.primary && rsx->drm.primary->kdev)
		kobject_uevent_env(&rsx->drm.primary->kdev->kobj, KOBJ_CHANGE, env);
}

static void uevent(struct ps3rsx_device *rsx, bool ok)
{
	uevent_env(rsx, ok ? "RSX_RECOVERED=1" : "RSX_RECOVERY_FAILED=1");
}

/* hard IRQ (exception_hook): record the faulting channel, queue the work */
void ps3rsx_uw_exception(struct ps3rsx_device *rsx)
{
	/* every exception gets a crash record; the recovery unless recovery=0. The faulting
	 * commit is found in the work (find_fault), from the reference the hook read */
	if (recovery && !READ_ONCE(display_only))
		WRITE_ONCE(rsx->recovering, true);
	queue_work(system_long_wq, &rsx->recover_work);
}

/* the NV40 cache entry at GET (nouveau nv04_fifo_intr_cache_error reads it so): method, data */
static int cache_entry(u32 get, u32 *m, u32 *dv)
{
	u64 phys = RSX_MMIO + 0x90000, lpar = 0;
	u32 ptr = (get & 0x7ff) >> 2;
	void __iomem *p;
	s64 st;

	st = lv1_undocumented_function_114(phys, 12, 0x1000, &lpar);
	UW("lv1_map_physical_address_region(%#llx, 12, 0x1000) [hcall 114, NV40 CACHE1 method/data]: %lld",
	   phys, st);
	if (st)
		return -EIO;
	p = ioremap(lpar, 0x1000);
	if (p) {
		*m = in_be32(p + ptr * 8);
		*dv = in_be32(p + ptr * 8 + 4);
		UW("read32 NV40_PFIFO_CACHE1_METHOD/DATA(%u): method %#x data %#x", ptr, *m, *dv);
		iounmap(p);
	}
	lv1_undocumented_function_115(lpar);
	return p ? 0 : -ENOMEM;
}

enum run_result { RUN_OK, RUN_NOTIFIER, RUN_FAIL };

/* ps3fb's reset program + SET_REFERENCE on the new context; the watchdog restarts the FIFO */
static enum run_result run_reset(struct ps3rsx_device *rsx, void __iomem *ctrl, u32 chid, u64 handle)
{
	u64 mem, fbctx, ddr;
	unsigned long xdr_size;
	void *fb_xdr;
	u32 *p, cookie, m, dv;
	s64 st;
	int i;

	ps3fb_gpu_handles(&mem, &fbctx, &ddr, &fb_xdr, &xdr_size);
	memcpy(ps3rsx_gart_ptr(rsx, 0), fb_xdr, 0x9b8);
	p = ps3rsx_gart_ptr(rsx, RING_START);
	cookie = ++rsx->seqno;
	p[0] = 0x00000002;			/* CALL 0 */
	p[1] = 0x00040050;			/* SET_REFERENCE */
	p[2] = cookie;
	wmb();
	if (ps3rsx_uw_pre_setup())
		return RUN_FAIL;
	st = lv1_gpu_context_attribute(handle, L1GPU_CONTEXT_ATTRIBUTE_FIFO_SETUP, RING_START,
				       RING_START, 0, 0);
	UW("lv1_gpu_context_attribute(%#llx, FIFO_SETUP, %#x, %#x): %lld", handle, RING_START,
	   RING_START, st);
	if (st)
		return RUN_FAIL;
	ps3rsx_uw_post_setup(chid);
	out_be32(ctrl + 0x40, RING_START + 12);
	for (i = 0; i < 200; i++) {		/* 2 s */
		u32 pull0, get;

		if (in_be32(ctrl + 0x48) == cookie)
			return RUN_OK;
		msleep(10);
		pull0 = in_be32(reg(0x3250));
		get = in_be32(reg(0x3270));
		/* the first fresh context after a trap: the notifier lookup fails (2 of 2 so far) */
		if ((pull0 & 0x10) && !(pull0 & 1) && get == 4 && !cache_entry(get, &m, &dv) &&
		    (m & 0x1ffc) == 0x180 && dv == 0x66604200)
			return RUN_NOTIFIER;
		ps3rsx_uw_nudge();
	}
	return in_be32(ctrl + 0x48) == cookie ? RUN_OK : RUN_FAIL;
}

/* CACHE1 off channel chid onto the null channel; for the free (op_refree's guards) */
static int park(u32 chid)
{
	u32 push1 = rd(0x3204, "NV03_PFIFO_CACHE1_PUSH1");

	if (!chid || push1 != (0x00010000 | chid) || rd(0x3270, "NV03_PFIFO_CACHE1_GET") != 4 ||
	    rd(0x3244, "NV04_PFIFO_CACHE1_DMA_GET") != rd(0x3240, "NV04_PFIFO_CACHE1_DMA_PUT") ||
	    rd(0x2100, "NV03_PFIFO_INTR_0") || (rd(0x3250, "NV04_PFIFO_CACHE1_PULL0") & 1) ||
	    (rd(0x3220, "NV04_PFIFO_CACHE1_DMA_PUSH") & 1)) {
		UW("park: state outside the plan: no free");
		return -ESTALE;
	}
	wr(0x2500, "NV03_PFIFO_CACHES", 0);
	wr(0x3200, "NV03_PFIFO_CACHE1_PUSH0", 0);
	wr(0x3250, "NV04_PFIFO_CACHE1_PULL0", 0x10);
	wr(0x3270, "NV03_PFIFO_CACHE1_GET", 0);
	wr(0x3210, "NV03_PFIFO_CACHE1_PUT", 0);
	wr(0x3204, "NV03_PFIFO_CACHE1_PUSH1", 0x1f);
	wr(0x3200, "NV03_PFIFO_CACHE1_PUSH0", 1);
	wr(0x3250, "NV04_PFIFO_CACHE1_PULL0", 1);
	wr(0x2500, "NV03_PFIFO_CACHES", 1);
	return rd(0x3204, "NV03_PFIFO_CACHE1_PUSH1") == 0x1f ? 0 : -ESTALE;
}

/*
 * The new context's reports area starts as lv1 left it, not where the clients' fence labels and
 * query reports stand. Seed it with the old one (the leaked context's, still allocated, and no
 * GPU writes it any more), so every label reads the last value the GPU wrote: a fence that
 * completed stays complete (no label goes back to 0, or jumps to garbage that would complete
 * fences early), and the ones the wedge stopped complete with the next fence their client
 * writes (Mesa 0053 kicks its context's current fence when a wait finds its own cancelled).
 * exec_lock held: nothing runs on the new context yet.
 */
static void copy_reports(struct ps3rsx_device *rsx, const struct ps3fb_gpu_ctx *c)
{
	u64 size = min(rsx->ctx.reports_size, c->reports_size);
	void __iomem *from, *to;
	u64 i;

	if (!rsx->ctx.reports_lpar || !c->reports_lpar || !size)
		return;
	from = ioremap(rsx->ctx.reports_lpar, size);
	to = ioremap(c->reports_lpar, size);
	if (from && to) {
		for (i = 0; i < size; i += 4)
			out_be32(to + i, in_be32(from + i));
		UW("reports area seeded from the old context (%llu KiB): fence labels continue", size >> 10);
	} else {
		UW("reports area: no mapping to seed it; fence labels restart from lv1's values");
	}
	if (from)
		iounmap(from);
	if (to)
		iounmap(to);
}

/* the new context's pages and GART; mm_lock taken here */
static int take_ctx(struct ps3rsx_device *rsx, struct ps3fb_gpu_ctx *c, void __iomem **ctrl,
		    void __iomem **dinfo)
{
	int ret;

	mutex_lock(&rsx->mm_lock);
	ret = ps3rsx_gart_remap_all(rsx);
	mutex_unlock(&rsx->mm_lock);
	if (ret)
		return ret;
	*ctrl = ioremap(c->ctrl_lpar, PAGE_SIZE);
	*dinfo = ioremap(c->dinfo_lpar, 128 * 1024);
	if (!*ctrl || !*dinfo) {
		if (*ctrl)
			iounmap(*ctrl);
		if (*dinfo)
			iounmap(*dinfo);
		return -ENOMEM;
	}
	return 0;
}

/* the channels for the record (validator state, last submits); with mark, the faulting client is
 * refused and every other one is told once (lost) */
static void mark_clients(struct ps3rsx_device *rsx, bool mark)
{
	struct drm_file *file;

	mutex_lock(&rsx->drm.filelist_mutex);
	list_for_each_entry(file, &rsx->drm.filelist, lhead) {
		struct ps3rsx_file *pf = file->driver_priv;
		struct ps3rsx_chan *ch;
		bool fault = false;

		if (!pf)
			continue;
		mutex_lock(&pf->lock);
		/* by serial: a channel id the faulting client freed may already be another's */
		list_for_each_entry(ch, &pf->chans, head)
			if (rsx->fault_serial && ch->serial == rsx->fault_serial)
				fault = true;
		list_for_each_entry(ch, &pf->chans, head) {
			unsigned int k;

			rec_printf("channel %u%s: bound %08x %08x %08x %08x %08x %08x %08x %08x prim_open %d cancelled %u broken %d",
				   ch->id, fault ? " (the faulting client)" : "",
				   ch->vstate.bound[0], ch->vstate.bound[1], ch->vstate.bound[2],
				   ch->vstate.bound[3], ch->vstate.bound[4], ch->vstate.bound[5],
				   ch->vstate.bound[6], ch->vstate.bound[7], ch->vstate.prim_open,
				   ch->cancelled, ch->broken);
			for (k = 0; k < min_t(u32, ch->hist_n, PS3RSX_HIST); k++) {
				struct ps3rsx_subhist *h =
					&ch->hist[(ch->hist_n - 1 - k) % PS3RSX_HIST];

				rec_printf("  submit seq %#x pid %d %u bytes, first %08x last %08x",
					   h->seq, h->pid, h->len, h->first, h->last);
			}
			if (!mark)
				continue;
			if (fault)
				ch->fatal = true;
			else
				ch->lost = true;
		}
		if (fault && mark) {
			WRITE_ONCE(pf->refused, true);
			UW("the faulting client (channel %d) is refused from now on (-ENODEV)",
			   rsx->fault_chan);
		}
		mutex_unlock(&pf->lock);
	}
	mutex_unlock(&rsx->drm.filelist_mutex);
}

/* the commit the GPU trapped in (blame.h), never simply the last submitter; exec_lock taken */
static void find_fault(struct ps3rsx_device *rsx)
{
	const struct ps3rsx_blame_ent *e;

	mutex_lock(&rsx->exec_lock);
	e = ps3rsx_blame_find(&rsx->blame, READ_ONCE(rsx->fault_ref));
	rsx->fault_known = e;
	rsx->fault_seq = e ? e->seq : 0;
	rsx->fault_chan = e ? e->chan : -1;
	rsx->fault_serial = e ? e->serial : 0;
	rsx->fault_pid = e ? e->pid : 0;
	strscpy(rsx->fault_comm, e ? e->comm : "?", sizeof(rsx->fault_comm));
	mutex_unlock(&rsx->exec_lock);
}

/* after rec_begin: the blame into the record and the log */
static void log_fault(struct ps3rsx_device *rsx)
{
	if (rsx->fault_known)
		UW("the GPU trapped in commit %#x (reference %#x): %s, channel %d, %s (pid %d)",
		   rsx->fault_seq, rsx->fault_ref,
		   rsx->fault_chan < 0 ? "the kernel's own" : "a client's", rsx->fault_chan,
		   rsx->fault_comm, rsx->fault_pid);
	else
		UW("the trapping commit is unknown (reference %#x): no client is refused",
		   rsx->fault_ref);
}

static void recover_fn(struct work_struct *w)
{
	struct ps3rsx_device *rsx = container_of(w, struct ps3rsx_device, recover_work);
	struct ps3fb_gpu_ctx c;
	void __iomem *ctrl = NULL, *dinfo = NULL;
	enum run_result r;
	u32 chid, push1;
	int ret = -EIO;

	WRITE_ONCE(ps3rsx_test_allow_rt12, false);
	find_fault(rsx);
	rec_begin(rsx);
	log_fault(rsx);
	if (!READ_ONCE(rsx->uw_ready)) {
		/* an exception during exec_init's own reset program: record it, go display-only,
		 * touch nothing (exec_init fails and unwinds by itself) */
		mark_clients(rsx, false);
		WRITE_ONCE(display_only, true);
		WRITE_ONCE(rsx->recovering, false);
		wake_up_all(&rsx->recover_wq);
		rec_end(rsx, "exception during the driver's start: display-only, no recovery");
		uevent(rsx, false);
		return;
	}
	if (!recovery || READ_ONCE(display_only)) {
		mark_clients(rsx, false);
		rec_end(rsx, READ_ONCE(display_only) ? "display-only already: nothing more to do" :
			"no in-place recovery (recovery=0): wedged until reboot");
		uevent(rsx, false);
		return;
	}
	if (recovery_used || ps3fb_gpu_recovered()) {
		/* ps3fb's flag outlives a module reload: one in-place recovery per boot */
		mark_clients(rsx, false);
		UW("a recovery already ran in this boot: no in-place recovery left, nothing written");
		goto failed_nolock;
	}
	recovery_used = true;
	UW("in-place recovery after a graph exception, faulting channel %d", rsx->fault_chan);
	mark_clients(rsx, true);	/* pf->lock before exec_lock, as channel_free takes them */
	mutex_lock(&rsx->exec_lock);
	/* every kernel copy queued before this point fails (kcopy_wait), whatever the order of
	 * the publish below */
	WRITE_ONCE(rsx->recover_gen, rsx->recover_gen + 1);
	ps3rsx_exec_cancel_poll(rsx);
	ps3rsx_exec_fail_fences(rsx);
	if (!mapped && map_pages()) {
		unmap_pages();
		goto failed;
	}
	push1 = rd(0x3204, "NV03_PFIFO_CACHE1_PUSH1");
	if ((push1 & 0x1f) == 0) {
		UW("CACHE1 holds channel 0 (ps3fb's): nothing written");
		goto failed;
	}
	ret = ps3fb_gpu_recover_begin(&rsx->hook);
	if (ret) {
		UW("ps3fb_gpu_recover_begin: %d: nothing written", ret);
		goto failed;
	}
	exc_at = ps3fb_gpu_exception_seen();
	if (rd(0x400720, "NV04_PGRAPH_FIFO") == 0)
		wr(0x400720, "NV04_PGRAPH_FIFO", 1);
	if (rd(0x2140, "NV03_PFIFO_INTR_EN_0") == 0 && rd(0x2100, "NV03_PFIFO_INTR_0") == 0)
		wr(0x2140, "NV03_PFIFO_INTR_EN_0", 0x017d1111);
	if (rd(0x40013c, "NV40_PGRAPH_INTR_EN") == 0 && rd(0x400100, "NV03_PGRAPH_INTR") == 0)
		wr(0x40013c, "NV40_PGRAPH_INTR_EN", 0x03111fd1);
	ret = ps3fb_gpu_ctx_leak(&rsx->hook);
	UW("ps3fb_gpu_ctx_leak: %d", ret);
	if (ret)		/* -EALREADY too: the current context could be one that ran work */
		goto failed;
	ret = ps3fb_gpu_ctx_get(0x820, &c);
	UW("new driver context: %d, %#llx", ret, c.handle);
	if (ret)
		goto failed;
	WRITE_ONCE(active, true);
	ret = take_ctx(rsx, &c, &ctrl, &dinfo);
	if (ret)
		goto failed;
	chid = in_be32(dinfo + 12);
	/* rsx->ctx stays on the old (leaked, alive) context until the publish: the vsync IRQ, flips
	 * and cursor never see the fresh or a freed handle */
	r = run_reset(rsx, ctrl, chid, c.handle);
	if (r == RUN_NOTIFIER) {
		UW("the notifier lookup failed on the first fresh context: park, free, allocate again");
		if (park(chid))
			goto failed;
		/* the free comes after a pause and a second look: the test builds that proved it
		 * waited a second or more here (their logging), and a free of a context still in
		 * CACHE1 is the call that shut the console down; a recovery is rare, so 300 ms cost
		 * nothing */
		msleep(300);
		if (rd(0x3204, "NV03_PFIFO_CACHE1_PUSH1") != 0x1f) {
			UW("park didn't hold (CACHE1 back on a channel): no free");
			goto failed;
		}
		iounmap(ctrl);
		iounmap(dinfo);
		ctrl = dinfo = NULL;
		ret = ps3fb_gpu_ctx_realloc(&rsx->hook, c.handle, &c);
		UW("ps3fb_gpu_ctx_realloc: %d, %#llx", ret, c.handle);
		if (ret)
			goto failed;
		ret = take_ctx(rsx, &c, &ctrl, &dinfo);
		if (ret)
			goto failed;
		chid = in_be32(dinfo + 12);
		r = run_reset(rsx, ctrl, chid, c.handle);
	}
	if (r != RUN_OK)
		goto failed;
	copy_reports(rsx, &c);
	/* publish: the vsync plug moves, the old pages stay mapped (the leaked context lives) */
	ps3rsx_kms_vsync_irq_fini(rsx);
	rsx->ctx = c;
	WRITE_ONCE(rsx->ctrl, ctrl);
	WRITE_ONCE(rsx->dinfo, dinfo);
	rsx->ring_put = RING_START + 12;
	rsx->ring_lapping = false;
	ps3rsx_hw_shadow_sync(rsx);
	ps3rsx_shadow_clear(rsx->hw_shadow);
	rsx->hw_behind = NULL;
	rsx->last_chan = -1;
	rsx->prim_owner = NULL;
	wake_up_all(&rsx->prim_wq);
	WRITE_ONCE(rsx->wedged, false);
	ps3rsx_exec_restart_poll(rsx);
	mutex_unlock(&rsx->exec_lock);
	ps3rsx_kms_vsync_irq_init(rsx);
	/* clients' fence and query mappings fault in the new page */
	ps3rsx_reports_moved(rsx, c.reports_lpar, c.reports_size);
	ps3rsx_uw_start();
	WRITE_ONCE(recoveries, recoveries + 1);
	UW("in-place recovery done: channel %u, context %#llx (%u so far)", chid, c.handle, recoveries);
	WRITE_ONCE(rsx->recovering, false);
	wake_up_all(&rsx->recover_wq);
	rec_end(rsx, "recovered in place");
	uevent(rsx, true);
	return;
failed:
	if (ctrl)
		iounmap(ctrl);
	if (dinfo)
		iounmap(dinfo);
	mutex_unlock(&rsx->exec_lock);
failed_nolock:
	WRITE_ONCE(recovery_failed, recovery_failed + 1);
	/* display-only: KMS keeps dumb buffers and lv1 flips (proven on a wedged RSX, 2026-11-01);
	 * every GPU submit and channel is refused, so Mesa falls back to software */
	WRITE_ONCE(display_only, true);
	UW("in-place recovery FAILED: display-only mode (no 3D) until reboot");
	WRITE_ONCE(rsx->recovering, false);
	wake_up_all(&rsx->recover_wq);
	rec_end(rsx, "recovery failed: display-only until reboot");
	uevent(rsx, false);
}

void ps3rsx_uw_dev_init(struct ps3rsx_device *rsx)
{
	INIT_WORK(&rsx->recover_work, recover_fn);
	init_waitqueue_head(&rsx->recover_wq);
	rsx->fault_chan = -1;
	if (!recs)
		recs = vzalloc(REC_N * REC_SIZE);	/* kept until module exit */
}

/* at the end of a successful exec_init */
void ps3rsx_uw_sysfs_init(struct ps3rsx_device *rsx)
{
	if (device_create_file(rsx->drm.dev, &dev_attr_recoveries) ||
	    device_create_file(rsx->drm.dev, &dev_attr_recovery_failed) ||
	    device_create_file(rsx->drm.dev, &dev_attr_display_only) ||
	    device_create_file(rsx->drm.dev, &dev_attr_force_display_only) ||
	    device_create_bin_file(rsx->drm.dev, &crash_report_attr))
		drm_warn(&rsx->drm, "unwedge: no sysfs counters\n");
}

/* no recovery work runs or starts from here on (remove, exec_init's error path) */
void ps3rsx_uw_quiesce(struct ps3rsx_device *rsx)
{
	ps3fb_gpu_set_exception_hook(NULL);
	cancel_work_sync(&rsx->recover_work);
}

void ps3rsx_uw_dev_fini(struct ps3rsx_device *rsx)
{
	cancel_work_sync(&rsx->recover_work);
	device_remove_bin_file(rsx->drm.dev, &crash_report_attr);
	device_remove_file(rsx->drm.dev, &dev_attr_force_display_only);
	device_remove_file(rsx->drm.dev, &dev_attr_display_only);
	device_remove_file(rsx->drm.dev, &dev_attr_recovery_failed);
	device_remove_file(rsx->drm.dev, &dev_attr_recoveries);
}
