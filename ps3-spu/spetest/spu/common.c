#include "common.h"

struct spt_ctl ctl __attribute__((aligned(128)));
struct spt_res res __attribute__((aligned(128)));
uint64_t ctl_ea;
static uint32_t t0, passes;

extern char _end[];

uint32_t t_free_start(void)
{
	return ((uint32_t)(uintptr_t)_end + 127u) & ~127u;
}

uint32_t t_free_end(void)
{
	return SPT_LS_SIZE - SPT_STACK_RESERVE;
}

void t_begin(unsigned long long slot, unsigned long long ea, unsigned test)
{
	uint32_t go;

	(void)slot;
	ctl_ea = ea;
	mfc_get(&ctl, ea, sizeof(ctl), 31, 0, 0);
	mfc_write_tag_mask(1u << 31);
	mfc_read_tag_status_all();
	res.magic = SPT_MAGIC;
	res.test = test;
	/* the decrementer runs down from the top; t_elapsed() is the difference */
	spu_write_decrementer(0xFFFFFFFFu);
	t0 = spu_read_decrementer();
	go = spu_read_in_mbox();
	if (ctl.magic != SPT_MAGIC || go != (SPT_GO | test)) {
		res.nfail++;
		res.sub[SPT_NSUB - 1]++;
		res.f[0].what = 0xBAD;
		res.f[0].got = ctl.magic;
		res.f[0].want = go;
	}
	t0 = spu_read_decrementer();
}

void t_fail(unsigned sub, uint32_t addr, uint32_t got, uint32_t want)
{
	if (res.nfail < SPT_NFAIL) {
		res.f[res.nfail].what = sub;
		res.f[res.nfail].addr = addr;
		res.f[res.nfail].got = got;
		res.f[res.nfail].want = want;
	}
	res.nfail++;
	if (sub < SPT_NSUB)
		res.sub[sub]++;
}

uint32_t t_elapsed(void)
{
	return t0 - spu_read_decrementer();
}

/* after the decrementer was used for something else: `spent` ticks have gone by in all */
void t_rebase(uint32_t spent)
{
	spu_write_decrementer(0xFFFFFFFFu);
	t0 = spu_read_decrementer() + spent;
}

int t_more(void)
{
	passes++;
	res.passes = passes;
	if (!(ctl.flags & SPT_F_STRESS))
		return passes < ctl.iters;
	return t_elapsed() < ctl.run_ticks;
}

int t_end(void)
{
	res.state = 1;
	mfc_put(&res, ctl_ea + SPT_OFF_RES, sizeof(res), 30, 0, 0);
	mfc_write_tag_mask(1u << 30);
	mfc_read_tag_status_all();
	return res.nfail ? 1 : 0;
}
