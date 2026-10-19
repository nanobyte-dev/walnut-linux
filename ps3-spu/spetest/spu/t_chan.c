/* Channels: the inbound mailbox, the outbound and interrupt mailboxes (round trips with the
 * PPE), both signal notification registers (one in overwrite and one in OR mode, set by the
 * runner), the decrementer (counts down, wraps, raises its event, rate against the PPE's clock).
 * The PPE answers each MSG_* word (runner.c, chan_conduct). */
#include "common.h"

#define DEC_MASK MFC_DECREMENTER_EVENT

static void mailbox_rounds(unsigned pass)
{
	unsigned r, i;

	for (r = 0; r < 4; r++) {
		unsigned n = (r + pass) % 3 + 1;

		out_msg(MSG_PING | n);
		for (i = 0; i < n; i++) {
			uint32_t x = spu_read_in_mbox();

			spu_write_out_mbox(x * 0x9E3779B1u + 1u);
			spu_write_out_intr_mbox(~x ^ 0x5A5Au);
		}
	}
}

static void signal_rounds(unsigned pass)
{
	unsigned r;

	for (r = 0; r < 6; r++) {
		uint32_t s1a, s2a, s1b, s2b, v1, v2;

		out_msg(MSG_SIG | (pass * 8 + r));
		(void)spu_read_in_mbox();	/* the PPE has written the registers */
		s1a = spu_stat_signal1();
		s2a = spu_stat_signal2();
		v1 = spu_read_signal1();
		v2 = spu_read_signal2();
		s1b = spu_stat_signal1();
		s2b = spu_stat_signal2();
		spu_write_out_mbox(v1);
		spu_write_out_mbox(v2);
		spu_write_out_mbox(s1a | (s2a << 4) | (s1b << 8) | (s2b << 12));
	}
}

static void decrementer(void)
{
	uint32_t d0, d1, v, i, prev;
	int32_t late;
	uint32_t ev;

	/* counts down, monotonic over many reads */
	spu_write_decrementer(0x7FFFFFFFu);
	d0 = spu_read_decrementer();
	prev = d0;
	for (i = 0; i < 2000; i++) {
		v = spu_read_decrementer();
		if (v > prev) {
			t_fail(CH_DEC, i, v, prev);
			break;
		}
		prev = v;
	}
	d1 = spu_read_decrementer();
	if (!(d1 < d0) || d0 - d1 > 0x1000000u)
		t_fail(CH_DEC, 1, d1, d0);
	/* wraps through zero and keeps counting */
	spu_write_decrementer(300);
	for (i = 0; i < 1000000; i++)
		if (spu_read_decrementer() > 0x80000000u)
			break;
	v = spu_read_decrementer();
	if (i == 1000000 || v < 0xFFF00000u)
		t_fail(CH_DEC, 2, v, 0xFFFFFFFFu);
	/* the event */
	spu_write_event_mask(0);
	spu_write_event_ack(DEC_MASK);
	spu_write_decrementer(4000);	/* 50 us */
	spu_write_event_mask(DEC_MASK);
	ev = spu_read_event_status();
	late = (int32_t)spu_read_decrementer();
	if (!(ev & DEC_MASK))
		t_fail(CH_DECEV, 0, ev, DEC_MASK);
	else if (late > 0 || late < -3200000)	/* fired at about zero: within 40 ms (hardware: microseconds) */
		t_fail(CH_DECEV, 1, (uint32_t)late, 0);
	spu_write_event_ack(DEC_MASK);
	spu_write_event_mask(0);
}

/* the rate: START, a wait of 1/2 s by our own decrementer, STOP and the ticks that passed */
static void rate(void)
{
	uint32_t want = ctl.tb_hz / 2u, a, t;

	spu_write_decrementer(0xFFFFFFFFu);
	out_msg(MSG_RATE_START);
	a = spu_read_decrementer();
	do
		t = a - spu_read_decrementer();
	while (t < want);
	out_msg(MSG_RATE_STOP);
	out_msg(t);
}

int main(unsigned long long slot, unsigned long long ea, unsigned long long seed)
{
	unsigned pass = 0;
	uint32_t el;

	(void)seed;
	t_begin(slot, ea, T_CHAN);
	if (ctl.tb_hz < 1000000u)
		ctl.tb_hz = 79800000u;
	do {
		mailbox_rounds(pass);
		signal_rounds(pass);
		el = t_elapsed();
		decrementer();
		rate();
		t_rebase(el + ctl.tb_hz / 2u + 40000u);	/* the two above rewrote the decrementer */
		pass++;
	} while (t_more());
	return t_end();
}
