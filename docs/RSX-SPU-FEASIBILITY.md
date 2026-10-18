# RSX and SPU feasibility: CPU writes to VRAM, SPU DMA, kernel M2MF, NV40 features

Research notes (2026-10-15), answering the open questions that docs/HARDWARE-LEVERAGE.md and
docs/SPU-OFFLOAD-SURVEY.md left. Nothing here was run on the PS3; each answer says what is read
from source or documentation and what still needs a probe. (Status 2026-10-17: section 3 is built,
ps3rsx/src/kcopy.c, with swap-ins ~10x faster, PLAN.md item 1. E8b, E16, E17, the memcpy check,
egl-deriv, instancing and VTF have not run, and the validator still lets 0x900-0x97c through with
unchecked data. Ranked in IDEAS.md. Unchanged on 2026-10-18: none of these probes has run.) Confidence: **high** = read in code
or a primary document, **medium** = inferred from several sources, **low** = plausible, unchecked.

Sources used throughout:
- the Cell BE Programming Handbook v1.11 (May 2008), cited as "Handbook §x, p.y"
  ([PDF](https://www.cresco.enea.it/LA1/cresco_sp14_ylichron/CBE-docs/CellBE_PXCell_Handbook_v1.11_12May08_pub.pdf));
- our kernel tree (6.12), paths relative to `build/kernel/src`;
- RPCS3 master, paths relative to `rpcs3/Emu/RSX/`;
- PSL1GHT master (`ppu/include/rsx`, `ppu/librsx`).

## 1. Why the PPE writes VRAM at 124 MB/s and not 4 GB/s

**Answer.** The PPE can't get anywhere near GB/s through any cache-inhibited mapping, because
every cache-inhibited store leaves the chip as its own EIB/IOIF transaction of at most 16 bytes.
The 4 GB/s figure needs 128-byte transactions, which only SPE DMA (or cacheable-line castouts)
produce. The 19 MB/s memcpy figure is a separate bug: glibc's Cell memcpy executes `dcbz`, which
traps on every cache line of cache-inhibited memory. Confidence: high for the mechanism, medium
for the ceiling (needs E8b).

How the pages are mapped:
- `ioremap()` → `pgprot_noncached` → `_PAGE_NON_IDEMPOTENT` → HPTE **I=1, G=1**. Source:
  `arch/powerpc/mm/ioremap.c:12-21`, `arch/powerpc/include/asm/book3s/64/pgtable.h:769-774`,
  `arch/powerpc/mm/book3s64/hash_utils.c:338-341`.
- `ioremap_wc()` → `pgprot_noncached_wc` → `_PAGE_TOLERANT` → HPTE **I=1, G=0**. Source:
  `ioremap.c:23-31`, `pgtable.h:776-781`, `hash_utils.c:338-339`.
- `ps3_hpte_insert` passes those `rflags` unchanged to `lv1_insert_htab_entry`, and `BUG()`s if
  lv1 refuses the entry (`arch/powerpc/platforms/ps3/htab.c:52-70`).
- Whether lv1 keeps or rewrites WIMG for GPU memory is undocumented. We know lv1 does inspect
  `hpte_r`: it insists the SPE shadow registers be mapped read-only, and accepts I=1,G=0 there
  (`arch/powerpc/platforms/ps3/spu.c:186-196`).
- Our kernel uses 4 KiB pages (`CONFIG_PPC_4K_PAGES=y`), so `mmu_ci_restrictions` plays no part.

What the PPE can do with cache-inhibited stores (Handbook):
- **§8.4.1.1, p.209:** "When software executes a store that is caching-inhibited and not
  guarded, the PPE can combine multiple stores to the same quadword into one store operation on
  the EIB. The length of this store operation can be 1, 2, 4, 8, or 16 bytes."
- So I=1,G=0 gathers at most two `std` into one 16-byte write. There is no 32- or 128-byte
  gathering for cache-inhibited stores. The 8 × 64 B L2 store gathering (§6.1.5.7) is for
  cacheable stores only.
- **§7.7.2, p.189:** guarded cache-inhibited stores are performed in program order and never
  combined. `sync` stops combining for any cache-inhibited storage; `eieio` stops it only for
  guarded storage.
- **§6.1.6.4, p.148 and §9.5.8, p.254:** `dcbz` on an I=1 page raises an alignment interrupt.
  So do misaligned cache-inhibited accesses and `lmw`/`stmw`/`lswi`/`stswx`.
- **WIMG (§4.2.6.7, Table 4-4):** only 0010, 0100 and 0101 are implemented, plus G.
- `stvx` is not in the alignment list, so an aligned 16-byte VMX store to cache-inhibited memory
  should be legal (medium: no sentence says so explicitly).

Reading the measurements:
- 124 MB/s with 8-byte stores (userspace, `ps3-gpu-tools/src/rsx-probe.c:333-353`, through ps3fb's I|G
  `/dev/ps3gpu_vram`) is one 8-byte transaction every ~64 ns, i.e. ~200 PPE cycles per store.
  The cost is per transaction, not per byte. The best case for 16-byte transactions is then
  ~250 MB/s.
- E8 in the kernel (`ps3rsx/probes/rsx_ctx_probe2.c:369-395`) measured 69 vs 70 MB/s, guarded vs WC.
  That loop times the first touch of each page, so ~1000 hash faults and `lv1_insert_htab_entry`
  calls fall inside the timed region.
- WC equal to guarded means one of three things: combining didn't happen (the loop isn't the
  limit), lv1 forces G=1, or the IOIF cost per transaction is the same at 8 and 16 bytes. E8b
  separates them.
- **glibc memcpy at 19 MB/s:** our sysroot's libc selects `__memcpy_cell` through the
  `PPC_FEATURE_CELL_BE` hwcap. The ifunc resolver is at `libc.so.6` 0xc3650 (`andis. r10,r9,1`),
  and its code at 0xce500 contains `dcbz r11,r6` at 0xce608 (objdump of
  `build/mesa/sysroot/usr/lib/powerpc64-linux-gnu/libc.so.6`).
- Every 128-byte destination line therefore takes an alignment interrupt, which the kernel
  emulates (`arch/powerpc/kernel/align.c:351-353`, `emulate_dcbz`). Confidence: high.
- Any userspace `memcpy` into a VRAM mapping pays this: the X shadow path, Mesa's
  `nv30_transfer_rect_cpu` on VRAM textures, a game writing a mapped VRAM buffer.

What PS3 homebrew does:
- libgcm maps RSX local memory at EA 0xC0000000. RPCS3's `sys_rsx_memory_allocate` returns
  "the local memory base address, usually 0xC0000000" (`Emu/Cell/lv2/sys_rsx.cpp:295`).
- PSL1GHT's `rsxInit` is a thin wrapper over the closed `gcmInitBodyEx` stub, so the page
  attributes lv2 uses aren't visible.
- psdevwiki's RSX page (Wayback copy) repeats Sony's numbers: Cell→GDDR3 write 4 GB/s, read
  16 MB/s, IOIF0 15.8 GB/s practical at 128-byte packets.
- Sony's guidance, as reflected in libgcm practice, is to fill local memory with SPU DMA or RSX
  DMA from main memory, not PPE stores.
- ps3vram (Linux) never maps VRAM for the CPU at all: it ioremaps only CTRL and REPORTS
  (`drivers/block/ps3vram.c:695,702`) and moves data with the RSX.

**Recommendations.**
1. **Userspace (cheap, do first):** never `memcpy` into a VRAM mapping on the PS3.
   - Add a `ps3_vram_write()` helper: aligned 8-byte (or 16-byte `stvx`, if E8b shows a gain)
     store loop, no `dcbz`. Use it in Mesa's PS3 CPU paths that write VRAM.
   - Check with `perf stat -e alignment-faults,emulation-faults` on X and a game. Any count in
     the thousands per second is this bug.
2. **`ps3rsx/src/mem.c:97` (VRAM BO mmap):** switch `pgprot_noncached` to `pgprot_noncached_wc`,
   **only after E8b** shows that lv1 keeps G=0 and that it helps.
   - VRAM is plain memory, so G=0 is correct.
   - Writers must then end with `sync` (any ioctl that kicks the GPU already does: `out_be32` has
     a leading `sync`).
   - Expected gain is at most ~2× (16-byte transactions); keep the GART mmap cacheable as now.
3. **Kernel (vswap `to_vram`, `vswap.c:86-94`):** use `ioremap_wc` for the chunk mapping
   (`vswap.c:185`) under the same E8b condition, with 16-byte pairs of `__raw_writeq` to
   consecutive addresses (already the case) and one `mb()` per page.
4. **Bulk uploads** stay on M2MF from GART (2.0 GB/s measured) or move to SPU DMA (section 2).
   PPE stores are for small, latency-bound writes only.
5. **A cacheable mapping of VRAM** (`dcbz` + stores + `dcbf` to make 128-byte castouts) is the
   only PPE route to large transactions. It isn't worth the risk:
   - lv1 may refuse the HPTE, and `ps3_hpte_insert` then `BUG()`s;
   - any partial line costs a 16 MB/s read-for-ownership;
   - the GPU's writes are not snooped.

   Park it unless a probe inserts the HPTE itself with error handling (stretch E8c, ask first).

## 2. Can SPU DMA reach VRAM under Linux?

**Answer: very probably yes, with no kernel change.**
- spufs resolves MFC translation misses by inserting the HPTE from the process's Linux PTE.
- Our VRAM BO mmaps have fully populated PTEs from `io_remap_pfn_range`.
- The logical SPEs share the LPAR's virtual address space with the PPE.

Confidence: medium-high for the mechanism. Low for the rate, since nobody has published an
SPU→VRAM figure for Linux.

The path in the kernel:
- `bo_mmap` (`ps3rsx/src/mem.c:65-99`): VRAM BOs get `VM_IO|VM_PFNMAP`, `pgprot_noncached`, and
  `io_remap_pfn_range(vma, start, (ddr_lpar + offset) >> PAGE_SHIFT, …)`. `remap_pfn_range`
  fills every PTE at mmap time.
- An MFC translation miss raises a class-1 interrupt. `spufs_handle_class1`
  (`arch/powerpc/platforms/cell/spufs/fault.c:105-150`) first calls
  `hash_page(ea, access, 0x300, dsisr)` (line 123).
- For a present PTE, `hash_page` inserts the HPTE (I=1,G=1, lpar address unchanged by
  `ps3_mm_phys_to_lpar`, since GPU lpar addresses lie above `map.total`). Then it restarts the
  DMA. This is the same HPTE the PPE would get.
- Only if `hash_page` fails does it fall back to `copro_handle_mm_fault` (line 128).
  - `handle_mm_fault` on a PFNMAP VMA with no `.fault` returns SIGBUS, giving `-EFAULT` and an
    `SPE_EVENT_SPE_DATA_STORAGE` to the program (`arch/powerpc/mm/copro_fault.c:59-70`).
  - So a populated PFNMAP mapping works, and an unpopulated one fails cleanly rather than
    hanging, but only for a context created with `SPU_CREATE_EVENTS_ENABLED`: then `spu_run`
    returns the status word with the event in its third argument. Without the flag the whole
    process gets SIGSEGV/SIGBUS (`spufs/fault.c` `spufs_handle_event`). Either way the context
    can't be restarted (the MFC stays suspended on the faulting command): close it and create
    another.
- PS3 SPEs are built with `lv1_construct_logical_spe(…, get_vas_id(), …)`, the PPE's own VAS
  (`arch/powerpc/platforms/ps3/spu.c:140-160`). The MFC translates through the same lv1-managed
  hash table.
- The handbook allows MFC DMA to caching-inhibited storage; atomic (`getllar`/`putllc`)
  commands to it are not allowed. The MFC splits transfers into ≤128-byte EIB transactions,
  which is what makes GB/s possible on IOIF0.
- Whether G=1 changes MFC behaviour (ordering, splitting) is not documented: test both I|G and
  I-only mappings.

Restrictions and prior art:
- Under GameOS, SPU jobs DMA into RSX local memory through its 0xC0000000-based EA. It's the
  standard SPURS/EDGE pattern, but no public primary source found; medium.
- On Linux there is no prior art for VRAM:
  - ps3vram used the RSX's FIFO and M2MF;
  - spu-medialib/MPlayer `vo_ps3` wrote to `/dev/fb0`, which is ps3fb's **XDR** shadow
    buffer, not VRAM.
- No lv1 rule against SPE access to IOIF0 memory is known. lv1 checks HPTEs at insertion, and
  the PPE's identical insertion already succeeds.
- **Reads stay slow:** SPU `mfc_get` from VRAM goes through the same 16 MB/s read path. Use it
  only to verify, never as a data path.
- **The kernel still can't use SPEs** (no in-kernel spufs client), so this is for userspace
  producers: libspeq, DXT encode, decompression into VRAM BOs. It is not a vswap path.

**Probe E16 (userspace, `ps3-gpu-tools/src/spu-vram.c` + an SPU kernel):**
1. Open `/dev/dri/renderD128`. Do the PS3_ABI getparam and `GEM_NEW` of a 1 MiB VRAM BO
   (mappable, below 224 MB), then `GEM_INFO` and `mmap` it `MAP_SHARED`. Pre-fill it with
   0x5a through M2MF or PPE stores.
2. Create an SPU context with `spu_create("/spu/vram", SPU_CREATE_EVENTS_ENABLED, 0755)`. Load
   the SPU program (spu-gcc, `docs/SPU-GCC.md`) and pass the EA and size in its registers before
   the first run (the context's `regs` file: crt0 hands r3-r5 to `main`; `spu_run` takes no
   argument), or through the inbound mailbox. libspeq does both of these (ps3-spu/lib).
3. SPU side:
   - fill a 16 KiB LS buffer with a pattern (word = EA offset);
   - issue 64 `mfc_put` of 16 KiB with 4 tags rotating, then `mfc_write_tag_mask`/`mfc_read_tag_status_all`;
   - time it with the decrementer and stop with the elapsed ticks;
   - second pass: one `mfc_get` of 16 KiB back into LS, compare, and report its time too.
4. PPE side:
   - check `spu_run`'s return status: a stop code means OK; a DATA_STORAGE event is the failure
     mode;
   - verify 4 KiB samples through the mmap (slow reads, so keep it small) or with an M2MF
     VRAM→GART copy;
   - print MB/s.
5. Repeat after switching the BO mmap to `pgprot_noncached_wc` (module parameter in a test
   build), and with a GART BO as the control (expect several GB/s).

Expectations:
- success: ≥1 GB/s puts, correct data;
- the get is slow (~16 MB/s);
- failure modes: a DATA_STORAGE event (PTE not present, i.e. our assumption is wrong), or an
  MFC class-0 error on the IOIF access.

Risk: only the BO the test owns is written, and the GPU is not involved. Worst case an SPE
error kills the test context. Confirm no other process is using the RSX heavily during the run.

## 3. Kernel M2MF copies for the VRAM swap (vswap.c)

Today `do_read` copies VRAM to the page with `from_vram` (`vswap.c:96-103, 262-264`), at 10 MB/s,
i.e. 0.4 ms per 4 KiB. `chunk_evict` reads up to 256 pages the same way **under `vs->lock`**
(`vswap.c:397-405`), so a write-back blocks all swap I/O for ~100 ms per chunk. Writes
(`to_vram`) stay on the PPE (124 MB/s, 33 µs per page; GPU copies wouldn't help them much).

Design: swap-in and write-back use M2MF VRAM→GART into a bounce reserved at init, issued on the
shared ring between user submits. They fall back to the PPE path whenever the GPU can't be used
right now. Confidence: high for the coexistence rules (read from our code), medium for the
latency (extrapolated from E-series rates).

### Constraints read from the driver

- **Ring access:** `exec_lock` covers `ps3rsx_ring_begin`/`ps3rsx_ring_commit`
  (`exec.c:208-267`).
  - `ring_commit` does `kzalloc(GFP_KERNEL)` for a dma_fence while `exec_lock` is held
    (`exec.c:234`). A submitter can thus enter direct reclaim holding `exec_lock` and write to
    ps3vswap.
  - So the swap path must only **trylock** `exec_lock`, and must not allocate a fence.
- **Console ownership:** when ps3fb owns the GPU (`gpu_owned == false`, `console.c`), its blits
  share PGRAPH state with us (E5). Nothing may be submitted then.
  - Retaking it (`retake()` → `ps3fb_gpu_takeover(true)` → `console_lock`) is too heavy for
    the I/O path.
  - The swap path uses the GPU only if it is already owned: trylock `own_lock`, check
    `gpu_owned && !console_pending`, then `submitting++`. Pair it with `ps3rsx_console_unhold`.
- **Open primitive:** no method may be written while `rsx->prim_owner` is set (RSX.md: a state
  switch inside BEGIN/END raised an exception; `submit.c:377-401`). So fall back while it is set.
- **Software context switch:**
  - The shadow keys copy-object state per object handle (`shadow.c:27-36`, `reg2d[obj]`), and
    the restore emits differences through the channel's own subchannel (`shadow.c:229-292`).
  - A restore only happens when `last_chan != ch->id` (`submit.c:404`).
  - Mesa sets M2MF DMA in/out once, before loops that may flush (`nv30_transfer.c:537-560`), so
    a kernel copy that leaves the M2MF state changed and `last_chan` unchanged would corrupt
    Mesa's next copy.
  - The kernel copy must therefore:
    - (a) use **the same M2MF object Mesa uses, 0x31337303**, so a hardware state that is
      per-engine or per-object is tracked either way;
    - (b) write every word it emits into `hw_shadow` with `ps3rsx_shadow_write` (after
      `hw_shadow_sync`);
    - (c) set `last_chan = -1`, `hw_behind = NULL`. The next user submit then re-emits its M2MF
      binds and state (a few words).
  - With `ctxsw=0` or `dry_run=1`, kernel copies are off.
- **Objects:**
  - DMA_NOTIFY 0x180 = 0x66604200: a transfer without it raised cause 0x105 (RSX.md).
  - DMA_BUFFER_IN 0x184 = 0xfeed0000 (VRAM).
  - DMA_BUFFER_OUT 0x188 = 0xfeed0001 (GART IO).
  - The notifier write lands at reports+0, which no fence uses (E14).
- **Fences:**
  - No `dma_fence`: commit a bare SET_REFERENCE seqno and poll `ref` (`seq_passed`).
  - `fence_poll`'s wedge detection only runs while user fences are pending
    (`exec.c:123-161`), so the waiter checks `rsx->wedged` (set from the exception hook) and its
    own timeout.
- **Block context:** the tag set has `BLK_MQ_F_BLOCKING` (`vswap.c:616`), so `queue_rq` may
  sleep. It already takes mutexes and runs in a `memalloc_noio` scope. Polling with
  `usleep_range` is fine; allocating is not.
- **Bounce:** GART is mapped in 1 MiB IO pages (`gart.c`). A 4 KiB swap-cache page can't be
  IO-mapped on its own, so data goes VRAM → bounce (M2MF) → page (PPE `memcpy` from cacheable,
  IOMMU-coherent RAM, ~2 µs per 4 KiB).
  - Reserve **one fixed 1 MiB GART chunk** at `ps3rsx_vswap_init`: a `drm_mm` node in
    `rsx->gart`, `ps3rsx_gart_get`, then `fixed = true` so `reclaim_work` never frees it.
  - That costs 1 MiB of RAM. It replaces the 256 `alloc_page` bounces that
    `ps3rsx_vswap_shrink` takes under memory pressure today (`vswap.c:467-472`).

### Wedges and timeouts: no data loss

Both GPU paths only *read* VRAM, and the slot stays allocated until the copy is confirmed. On
any failure the same slot is read by the PPE:
- trylock failure, console owned, primitive open, `wedged`;
- timeout (no ref within 50 ms, e.g. behind a long frame).

After a timeout the bounce is marked busy until `ref` passes the copy's seqno (`bounce_seq`), so
a late GPU write can't land in a later request's data.

Open point: is VRAM still readable by the CPU after a graph exception? It is very likely (the
memory controller isn't PGRAPH), but untested. The current design comment (`vswap.c:13-15`)
already relies on it.

### Pseudo-code

```c
/* exec.c: commit without a dma_fence (no allocation); exec_lock held */
u32 ps3rsx_ring_commit_seq(struct ps3rsx_device *rsx, u32 len)
{
	u32 *p = ring_cpu(rsx, rsx->ring_put + len);
	p[0] = SET_REFERENCE; p[1] = ++rsx->seqno;
	rsx->ring_put += len + 8;
	wmb();
	out_be32(rsx->ctrl + CTRL_PUT, rsx->ring_put);
	return rsx->seqno;
}

/* exec.c: wait for seq from process context; spin briefly, then sleep */
int ps3rsx_seq_wait(struct ps3rsx_device *rsx, u32 seq, unsigned int timeout_us)
{
	ktime_t end = ktime_add_us(ktime_get(), timeout_us);
	int spins = 50;
	while (!ps3rsx_seq_done(rsx, seq)) {
		if (READ_ONCE(rsx->wedged)) return -EIO;
		if (ktime_after(ktime_get(), end)) return -ETIMEDOUT;
		if (spins-- > 0) udelay(2); else usleep_range(20, 50);
	}
	return 0;
}

/* kcopy.c (new): get the GPU for a kernel copy without waiting on anything */
int ps3rsx_kcopy_begin(struct ps3rsx_device *rsx)
{
	if (!ctxsw || dry_run || READ_ONCE(rsx->wedged)) return -EAGAIN;
	if (!mutex_trylock(&rsx->own_lock)) return -EAGAIN;
	if (!rsx->gpu_owned || rsx->console_pending) { mutex_unlock(&rsx->own_lock); return -EAGAIN; }
	rsx->submitting++;                          /* console can't take it back meanwhile */
	mutex_unlock(&rsx->own_lock);
	if (!mutex_trylock(&rsx->exec_lock)) goto unhold;
	if (rsx->prim_owner) { mutex_unlock(&rsx->exec_lock); goto unhold; }
	return 0;
unhold:
	ps3rsx_console_unhold(rsx);
	return -EAGAIN;
}

#define SC_K 1          /* subchannel Mesa doesn't use (Mesa: 3D 7, M2MF 2, 2D 3-5) */
static void kput(struct ps3rsx_device *rsx, u32 **p, u32 m, const u32 *v, u32 n)
{
	u32 i;
	*(*p)++ = (n << 18) | (SC_K << 13) | m;
	for (i = 0; i < n; i++) {
		*(*p)++ = v[i];
		ps3rsx_shadow_write(rsx->hw_shadow, SC_K, m ? m + 4 * i : 0, v[i]);
	}
}

/* Copy n runs VRAM -> GART; returns the seqno to wait for. exec_lock held. */
int ps3rsx_kcopy_to_gart(struct ps3rsx_device *rsx, const struct kcopy_run *r, int n, u32 *seq)
{
	static const u32 obj = 0x31337303, dma[3] = { 0x66604200, 0xfeed0000, 0xfeed0001 };
	u32 len = 4 * (2 + 4 + 9 * n), *p, *start;
	int err, i;

	start = p = ps3rsx_ring_begin(rsx, len, &err);
	if (!p) return err;
	if (rsx->hw_behind) {                       /* = hw_shadow_sync() */
		ps3rsx_shadow_take(rsx->hw_behind, rsx->hw_shadow);
		rsx->hw_behind = NULL;
	}
	kput(rsx, &p, 0x000, &obj, 1);              /* bind M2MF on SC_K */
	kput(rsx, &p, 0x180, dma, 3);               /* notify, in = VRAM, out = GART */
	for (i = 0; i < n; i++) {                   /* one op per run of consecutive slots */
		u32 v[8] = { r[i].vram_off, r[i].gart_io, PAGE_SIZE, PAGE_SIZE,
			     PAGE_SIZE, r[i].npages, 0x101, 0 };
		kput(rsx, &p, 0x30c, v, 8);         /* 0x328 (last word) launches; not shadowed */
	}
	*seq = ps3rsx_ring_commit_seq(rsx, (p - start) * 4);
	rsx->last_chan = -1;                        /* next user submit restores its state */
	return 0;
}

void ps3rsx_kcopy_end(struct ps3rsx_device *rsx)   /* after the commit, before waiting */
{
	mutex_unlock(&rsx->exec_lock);
}

/* vswap.c: one request's VRAM reads, after the loop has collected them (vs->lock held) */
static void reads_finish(struct ps3rsx_vswap *vs, struct vread *rd, int n)
{
	struct ps3rsx_device *rsx = vs->rsx;
	u32 seq;
	int i;

	if (n >= gpu_min_pages && bounce_free(vs) &&
	    !ps3rsx_kcopy_begin(rsx)) {
		int err = ps3rsx_kcopy_to_gart(rsx, runs_of(rd, n, vs->bounce_io), nruns, &seq);
		ps3rsx_kcopy_end(rsx);
		if (!err)
			err = ps3rsx_seq_wait(rsx, seq, 50000);
		ps3rsx_console_unhold(rsx);
		if (!err) {
			for (i = 0; i < n; i++)          /* bounce (cacheable) -> swap-cache page */
				memcpy_to_page(rd[i].page, rd[i].off,
					       vs->bounce + i * PAGE_SIZE + rd[i].in_page, rd[i].len);
			vs->gpu_reads += n;
			return;
		}
		vs->bounce_seq = seq;               /* late GPU write possible: bounce busy */
	}
	for (i = 0; i < n; i++)                     /* PPE fallback: unchanged behaviour */
		from_vram_page(rd[i].page, rd[i].off, slot_io(vs, rd[i].slot) + rd[i].in_page,
			       rd[i].len);
}
```

`chunk_evict` gets the same treatment:
- Under `vs->lock`, one M2MF op copies the whole chunk (offset = chunk start, 256 lines of
  4 KiB, pitch 4 KiB) into the bounce, then waits on the seqno.
- The bios are built straight on the bounce pages (`nth_page(chunk->page, s)`): no PPE copy at
  all.
- `vs->lock` is dropped while the disk writes, as today, and `vs->bounce_owner = EVICT` makes
  reads use the PPE path until the bios complete.
- On fallback it does today's PPE copy into `alloc_page` bounces.

### Functions to add or change

| Where | Change |
|---|---|
| `exec.c` | add `ps3rsx_ring_commit_seq()` (no allocation) and `ps3rsx_seq_wait()`. Refactor `ps3rsx_ring_commit()` to allocate its fence before the caller takes `exec_lock` (removes reclaim under `exec_lock` for everyone). |
| new `kcopy.c` | `ps3rsx_kcopy_begin/_to_gart/_end`. `ps3rsx_kcopy_from_gart` later, if writes ever move to the GPU. |
| `submit.c` | export `hw_shadow_sync()` (or inline it as above). No other change: `last_chan = -1` already makes the next submit restore. |
| `gart.c` | `ps3rsx_gart_pin(rsx, size, &io)`: allocate + back + mark `fixed`, for the bounce. |
| `vswap.c` | bounce reserve in `ps3rsx_vswap_init`/`_fini`; `do_read` collects VRAM pieces, then `reads_finish()` after the segment loop; `chunk_evict` GPU path + bounce ownership; `bounce_seq`; stats `gpu_reads`, `gpu_fallbacks`; module params `vswap_gpu` (default 1) and `vswap_gpu_min_pages`. |
| `ps3rsx.h` | prototypes; `struct kcopy_run { u32 vram_off, gart_io, npages; }`. |

### Expected latency

| Case | Today (PPE) | M2MF, GPU idle | M2MF, frame queued |
|---|---|---|---|
| 1 page | 0.4 ms | ~15–30 µs *est.* | + the queued work, up to one frame (16–33 ms) |
| 32-page request | 12.8 ms | ~0.25 ms *est.* (≈7 µs/page at the measured 600 MB/s + ~20 µs fixed) | same + queue |
| chunk write-back (256 pages) | ~100 ms under `vs->lock` | ~2 ms *est.* | same + queue |

The fixed part is a ring write and PUT store (~2 µs), FIFO wake-up and M2MF setup (a few µs),
`ref` polling granularity (2 µs spins) and the bounce memcpy (~2 µs per page).

Since the ring is FIFO, a copy queues behind whatever the game has submitted. Rule to start
with, then tune from measurements:
- GPU when it is idle (`ps3rsx_exec_idle()`) or the request has ≥ 8 pages;
- PPE otherwise.

## 4. Instancing, vertex texture fetch and derivatives on NV40/RSX

### Instancing: the RSX has a per-attribute frequency divider

Confidence: high for methods and encodings, medium for semantics (from RPCS3's emulation).

- **0x1fc0 `NV4097_SET_FREQUENCY_DIVIDER_OPERATION`**: one word, a 16-bit mask with one bit per
  vertex attribute. Bit set = MODULO, clear = DIVIDE.
  - RPCS3 `gcm_enums.h:737` and `:86-87` (`CELL_GCM_FREQUENCY_MODULO=1, _DIVIDE=0`), and
    `rsx_decode.h:538-575`.
  - PSL1GHT `nv40.h:5077` (`NV40TCL_DIVIDER_FREQUENCY_OP`) and `commands_impl.h:1056-1062`
    (`rsxSetFrequencyDividerOperation`).
  - The libgcm name is `cellGcmSetFrequencyDividerOperation`.
- **The frequency is bits 16-31 of VTXFMT (0x1740 + 4i)**, above type (0-3), size (4-7) and
  stride (8-15).
  - RPCS3 `rsx_decode.h:4395-4460`.
  - PSL1GHT `rsxBindVertexArrayAttrib` writes `(frequency<<16)|(stride<<8)|(elems<<4)|type`
    (`commands_impl.h:1261-1268`).
  - Neither envytools rnndb (`nv30-40_3d.xml:454-458`) nor our `nv30-40_3d.xml.h:1464-1479`
    knows the field. Mesa writes 0 there, which RPCS3 treats as "normal per-vertex".
- **Fetch index** (RPCS3 `Program/GLSLSnippets/RSXProg/RSXVertexFetch.glsl:186-203`,
  `RSXThread.cpp:441-545`):
  - frequency 0 → element 0;
  - modulo → `(vertex + index_offset) % f`;
  - divide → `(vertex - base_index) / f`.

  "vertex" is the index value for indexed draws and the running vertex counter for array draws.
  There is no instance counter and no instance-ID register, which is why
  `vs_instanceid` must stay false.
- **Mesa today:**
  - `nv30_screen.c:155-156` (`vs_instanceid = false`,
    `vertex_element_instance_divisor = false; /* xxx = yes? */`) and `start_instance = false`
    (line 179);
  - `nv30_draw_arrays`/`_elements` take `instance_count` but draw once (`nv30_vbo.c:363, 488`);
  - st/mesa exposes `ARB_instanced_arrays` from `vertex_element_instance_divisor` alone, and
    that extension brings `glDraw*InstancedARB`. So turning the cap on obliges the driver to
    honour `instance_count`.
- **How to expose it**, in two steps:
  1. **Correctness first, no divider:**
     - with the cap on, `nv30_draw_vbo` loops over instances;
     - per instance, it re-emits VTXBUF(i) (0x1680+4i) for each attribute with
       `instance_divisor`, at offset `base + (inst / divisor) * stride`, then the draw;
     - works for every primitive type (strips stay separate) and costs ~2 words per
       per-instance attribute per instance;
     - the per-draw GL overhead in Mesa, which is what costs PPE time, is paid once;
     - the same loop could load a VP constant per instance to emulate `gl_InstanceID` (4 words),
       which would allow `vs_instanceid` and GL 3.1-style `ARB_draw_instanced`.
  2. **Hardware divider for list primitives:** one draw of `count × instances` vertices with
     per-vertex attributes MODULO `count` and per-instance attributes DIVIDE `count × divisor`.
     - Needs `count ≤ 65535` (16-bit field). Indexed draws need the index list replicated with
       `+ inst × (max_index + 1)` offsets, or BASE_INDEX (0x173c) per instance, whose
       interaction with the divider is unverified.
     - Add 0x1fc0 to the validator allowlist (`ps3rsx/src/validate.c:67`, today
       `{0x1fc4, 0x1fd8}`). It's plain state, so `shadow.c` restores it as is.
     - Mesa must write 0x1fc0 = 0 and frequency 0 whenever instancing is off: state is shared
       between channels.
- **Value for our games is small.** ioquake3 and ClassiCube don't use instancing. It matters
  for new or ported content and for glamor-like batching only.

### Vertex texture fetch

Confidence: high for methods and the Mesa state, medium for format limits.

- **Methods:** 0x900 + 0x20·unit for **4 units**:
  - OFFSET 0x900, FORMAT 0x904, ADDRESS (wrap) 0x908, CONTROL0 (enable) 0x90c, CONTROL3 0x910,
    FILTER 0x914, IMAGE_RECT 0x918, BORDER_COLOR 0x91c;
  - RPCS3 `gcm_enums.h:627-634`, `rsx_methods.h:119` (4 units); PSL1GHT `nv40.h:4676-4683`;
  - Mesa has hand-written macros in `nv30_winsys.h:10-18`.
- **Formats and filtering:**
  - NVIDIA's vp40 profile notes: NV4x fetches 1-, 3- and 4-component fp32 textures, no cube or
    3D textures, no filtering. ANGLE's VTF notes for SM3 GeForce 6/7: R32F and RGBA32F, 2D,
    point sampling.
  - PSL1GHT's vertex texture filter takes only a bias and its wrap only S/T, which fits
    "nearest, 2D".
  - The RSX format codes are presumably `CELL_GCM_TEXTURE_X32_FLOAT` (0x9c) and
    `W32_Z32_Y32_X32_FLOAT` (0x9b) with the LN/NR flags (low confidence on the exact values;
    take them from RPCS3 `gcm_enums.h` when implementing).
- **Mesa today:**
  - `nv40_verttex_validate` (`nv40_verttex.c:31-52`) is a stub: the `if (ss && sv)` branch is
    empty, and it reads `fragprog.textures/samplers` instead of `vertprog.*`;
  - the vertex `shader_caps` never set `max_texture_samplers`, so GL sees 0;
  - `nvfx_vertprog.c` has no TEX/TXL translation (`NV40_VP_INST_VEC_OP_TXL` 0x19 is defined in
    `nvfx_shader.h:72` and unused). The VP instruction's texture-unit field still has to be
    taken from RPCS3's `VertexProgramDecompiler.cpp` (around line 637).
- **Kernel risk, now:** the validator allows 0x900-0x97c with unchecked data
  (`validate.c:47`), so a bad vertex texture format or offset could wedge the GPU today. Before
  Mesa uses VTF, the validator should:
  - check FORMAT location bits like other DMA fields;
  - allow only the two fp32 formats (2D, no mipmap) when CONTROL0 enables a unit;
  - bounds-check OFFSET + size against the owning BO, like fragment textures.
- **Turning it on:**
  1. fill `nv40_verttex_validate` (OFFSET, FORMAT, WRAP, ENABLE, FILTER, SIZE, plus
     `BUFCTX_VERTTEX` references and a 0x1fd8 texture cache invalidate);
  2. add TXL/TEX to `nvfx_vertprog.c`;
  3. set the vertex `max_texture_samplers = max_sampler_views = 4`.

  Gallium can't restrict formats per stage. A non-fp32 view bound to a vertex sampler must be
  converted (blit into an RGBA32F shadow texture) or left disabled. It must never be sent as
  is, since an unsupported format could raise an exception.

### Fragment derivatives

Confidence: high.

- The ISA has DDX 0x15 / DDY 0x16 ("can only write XY", `nvfx_shader.h:233-234`), and
  `nvfx_fragprog.c:566-586` already translates TGSI DDX/DDY. When Z/W are written, it computes
  ZW through a temp with a swizzle.
- GLSL `dFdx`/`dFdy` lower to TGSI DDX/DDY through `nir_to_tgsi`.
- **Desktop GLSL 1.10+ does not check the cap**: Mesa's `derivatives()` builtin predicate is
  true for any fragment shader at desktop version ≥ 110. So dFdx/dFdy work in GL 2.1 today.
- `caps->fragment_shader_derivatives = false` (`nv30_screen.c:150`) only withholds GLES2's
  `OES_standard_derivatives`. `fs_fine_derivative` (line 200) gates `ARB_derivative_control`,
  which should stay off.
- **Turning it on safely:**
  1. write `egl-deriv` (GLES2 and GL 2.1 variants): a fragment shader outputting
     `dFdx(uv)*k`, `dFdy(uv)*k` and `fwidth`, compared against softpipe within 1 LSB;
  2. include a case writing all four components (the temp path) and one under fp32 export
     (0009);
  3. set `fragment_shader_derivatives = true` on `is_ps3`.

  Expected quirk: coarse per-2×2 derivatives (one value per quad). softpipe is also per-quad
  coarse, so they should match. Sign convention is unverified; RPCS3 maps DDX/DDY to GLSL
  `dFdx`/`dFdy` with no fix-up (`FragmentProgramDecompiler.cpp:1231-1232`).

## Recommended experiments

In order of value per effort. All are userspace or test-module runs; none touches flash, boot
flags or syscon.

1. **E8b: cache-inhibited store rate, done right** (test module, `rsx_ctx_probe2.c wc2`).
   - ioremap 4 MiB of `VRAM_SCRATCH` twice (`ioremap`, `ioremap_wc`) and **pre-touch every page
     before timing** (one `__raw_readq` per 4 KiB).
   - Then time: guarded 8-byte stores, WC 8-byte stores, WC 16-byte `stvx`
     (`enable_kernel_altivec()`, 16-byte aligned), and WC `std` pairs with a `sync` every 4 KiB.
     Use `mftb` for timing.
   - Find the HPTE of one page of each mapping (`hpt_vpn`, `hpt_hash`, read both PTEGs with
     `lv1_read_htab_entries`). Print `hpte_r & (HPTE_R_I|HPTE_R_G)` to see whether lv1 kept G=0.
   - Decides recommendation 1.2/1.3.
2. **memcpy trap check** (userspace):
   - run `perf stat -e alignment-faults,emulation-faults rsx-probe cpu` (if the kernel lacks
     these events, `CONFIG_PPC_EMULATED_STATS` gives `/sys/kernel/debug/powerpc/emulated_instructions/dcbz`);
   - expect ~65k faults per 8 MiB memcpy;
   - then the same test with a `std` loop and with a 16-byte `vec_st` loop.
3. **E16: SPU DMA into a VRAM BO** (section 2 probe): put rate, data check, I|G vs I-only,
   GART control.
4. **E17: M2MF small-copy latency**: decides the vswap thresholds.
   - From `rsxdrm-smoke`: VRAM→GART copies of 1, 8, 32 and 256 pages (one op per page, and one
     op for contiguous runs), timing submit→ref with the GPU idle;
   - then the same while `glxgears` (or OpenArena timedemo) runs.
5. **vswap GPU path** (after 4):
   - swap-in heavy test (`stress-ng --vm` sized over RAM, or the OpenArena game-mode run);
   - compare `gpu_reads`/`gpu_fallbacks`, swap-in latency (`perf trace` on page faults or
     `/proc/vmstat` pswpin rate) and the write-back time per chunk against today;
   - also run with X on a text console (`gpu_owned` false: must fall back cleanly).
6. **egl-deriv**, then flip `fragment_shader_derivatives` (section 4).
7. **Instancing step 1** (instance loop) behind an env var, an `egl-instanced` test against
   softpipe; the divider (0x1fc0) only after a raw-command probe (`rsx-tri` variant):
   - per-instance colour with DIVIDE and frequency = 3;
   - one draw of 3 × 4 vertices;
   - check four distinct triangles with the webcam/readback.
8. **VTF**: first harden the validator (0x900-0x91c checks); then an `rsx-tri` variant sampling
   an RGBA32F 4×4 texture in the vertex program; then the Mesa work.

Not recommended: a cacheable PPE mapping of VRAM (E8c). lv1 refusing the HPTE `BUG()`s the
kernel, and the gain only applies to whole-line writes. Ask before trying it.
