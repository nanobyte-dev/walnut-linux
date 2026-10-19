#ifndef SPT_OPLIST_H
#define SPT_OPLIST_H
/* The execution test's operations, one X-macro line each:
 *   OP(name, input class, compare, SPU expression of a, b, c (all vec_uint4, result vec_uint4))
 * The PPE ignores the expression and has a reference per name (ref.c). Compare modes:
 *   EXACT  every bit
 *   SPF    single-precision words, +0 and -0 alike
 *   SPF02  words 0 and 2 only (frds leaves the others unspecified)
 *   DPF    double-precision lanes, +0 and -0 alike
 *   NONE   no reference: only the digest, compared between SPEs and between passes
 * The SPU expressions use the view macros of spu/t_exec.c (F4, D2, ...). */
#define OPLIST(OP) \
 OP(add,	IN_INT, EXACT, spu_add(a, b)) \
 OP(sub,	IN_INT, EXACT, spu_sub(a, b)) \
 OP(and,	IN_INT, EXACT, spu_and(a, b)) \
 OP(or,		IN_INT, EXACT, spu_or(a, b)) \
 OP(xor,	IN_INT, EXACT, spu_xor(a, b)) \
 OP(nor,	IN_INT, EXACT, spu_nor(a, b)) \
 OP(nand,	IN_INT, EXACT, spu_nand(a, b)) \
 OP(andc,	IN_INT, EXACT, spu_andc(a, b)) \
 OP(orc,	IN_INT, EXACT, spu_orc(a, b)) \
 OP(eqv,	IN_INT, EXACT, spu_eqv(a, b)) \
 OP(selb,	IN_INT, EXACT, spu_sel(a, b, c)) \
 OP(ceq,	IN_INT, EXACT, U4(spu_cmpeq(a, b))) \
 OP(cgt,	IN_INT, EXACT, U4(spu_cmpgt(S4(a), S4(b)))) \
 OP(clgt,	IN_INT, EXACT, U4(spu_cmpgt(a, b))) \
 OP(ceqh,	IN_INT, EXACT, U4(spu_cmpeq(H8(a), H8(b)))) \
 OP(cgth,	IN_INT, EXACT, U4(spu_cmpgt(SH8(a), SH8(b)))) \
 OP(clgth,	IN_INT, EXACT, U4(spu_cmpgt(H8(a), H8(b)))) \
 OP(ceqb,	IN_INT, EXACT, U4(spu_cmpeq(B16(a), B16(b)))) \
 OP(cgtb,	IN_INT, EXACT, U4(spu_cmpgt(SB16(a), SB16(b)))) \
 OP(clgtb,	IN_INT, EXACT, U4(spu_cmpgt(B16(a), B16(b)))) \
 OP(shl,	IN_INT, EXACT, spu_sl(a, b)) \
 OP(rot,	IN_INT, EXACT, spu_rl(a, S4(b))) \
 OP(rotm,	IN_INT, EXACT, spu_rlmask(a, S4(b))) \
 OP(rotma,	IN_INT, EXACT, U4(spu_rlmaska(S4(a), S4(b)))) \
 OP(shli,	IN_INT, EXACT, spu_sl(a, 5)) \
 OP(roti,	IN_INT, EXACT, spu_rl(a, 11)) \
 OP(rotmi,	IN_INT, EXACT, spu_rlmask(a, -9)) \
 OP(rotmai,	IN_INT, EXACT, U4(spu_rlmaska(S4(a), -9))) \
 OP(ah,		IN_INT, EXACT, U4(spu_add(H8(a), H8(b)))) \
 OP(sfh,	IN_INT, EXACT, U4(spu_sub(H8(a), H8(b)))) \
 OP(shlh,	IN_INT, EXACT, U4(spu_sl(H8(a), H8(b)))) \
 OP(roth,	IN_INT, EXACT, U4(spu_rl(H8(a), SH8(b)))) \
 OP(mpye,	IN_INT, EXACT, U4(spu_mule(SH8(a), SH8(b)))) \
 OP(mpyo,	IN_INT, EXACT, U4(spu_mulo(SH8(a), SH8(b)))) \
 OP(mpyu,	IN_INT, EXACT, U4(spu_mulo(H8(a), H8(b)))) \
 OP(mpyh,	IN_INT, EXACT, U4(spu_mulh(SH8(a), SH8(b)))) \
 OP(mpya,	IN_INT, EXACT, U4(spu_madd(SH8(a), SH8(b), S4(c)))) \
 OP(mul32,	IN_INT, EXACT, mul32(a, b)) \
 OP(clz,	IN_INT, EXACT, spu_cntlz(a)) \
 OP(cntb,	IN_INT, EXACT, U4(spu_cntb(B16(a)))) \
 OP(avgb,	IN_INT, EXACT, U4(spu_avg(B16(a), B16(b)))) \
 OP(absdb,	IN_INT, EXACT, U4(spu_absd(B16(a), B16(b)))) \
 OP(sumb,	IN_INT, NONE,  U4(spu_sumb(B16(a), B16(b)))) \
 OP(gb,		IN_INT, EXACT, spu_gather(a)) \
 OP(gbh,	IN_INT, EXACT, spu_gather(H8(a))) \
 OP(gbb,	IN_INT, EXACT, spu_gather(B16(a))) \
 OP(orx,	IN_INT, EXACT, spu_orx(a)) \
 OP(xsbh,	IN_INT, EXACT, U4(spu_extend(SB16(a)))) \
 OP(xshw,	IN_INT, EXACT, U4(spu_extend(SH8(a)))) \
 OP(xswd,	IN_INT, EXACT, U4(spu_extend(S4(a)))) \
 OP(rotqby,	IN_INT, EXACT, U4(spu_rlqwbyte(B16(a), spu_extract(S4(b), 0)))) \
 OP(shlqby,	IN_INT, EXACT, U4(spu_slqwbyte(B16(a), spu_extract(S4(b), 0)))) \
 OP(rotqmby,	IN_INT, EXACT, U4(spu_rlmaskqwbyte(B16(a), spu_extract(S4(b), 0)))) \
 OP(rotqbi,	IN_INT, EXACT, U4(spu_rlqw(B16(a), spu_extract(S4(b), 0)))) \
 OP(shlqbi,	IN_INT, EXACT, U4(spu_slqw(B16(a), spu_extract(S4(b), 0)))) \
 OP(rotqmbi,	IN_INT, EXACT, U4(spu_rlmaskqw(B16(a), spu_extract(S4(b), 0)))) \
 OP(shufb,	IN_INT, EXACT, U4(spu_shuffle(B16(a), B16(b), B16(c)))) \
 OP(fa,		IN_SP,  SPF,   U4(spu_add(F4(a), F4(b)))) \
 OP(fs,		IN_SP,  SPF,   U4(spu_sub(F4(a), F4(b)))) \
 OP(fm,		IN_SP,  SPF,   U4(spu_mul(F4(a), F4(b)))) \
 OP(fma,	IN_SP,  SPF,   U4(spu_madd(F4(a), F4(b), F4(c)))) \
 OP(fms,	IN_SP,  SPF,   U4(spu_msub(F4(a), F4(b), F4(c)))) \
 OP(fnms,	IN_SP,  SPF,   U4(spu_nmsub(F4(a), F4(b), F4(c)))) \
 OP(fceq,	IN_SP,  EXACT, U4(spu_cmpeq(F4(a), F4(b)))) \
 OP(fcgt,	IN_SP,  EXACT, U4(spu_cmpgt(F4(a), F4(b)))) \
 OP(fcmeq,	IN_SP,  EXACT, U4(spu_cmpabseq(F4(a), F4(b)))) \
 OP(fcmgt,	IN_SP,  EXACT, U4(spu_cmpabsgt(F4(a), F4(b)))) \
 OP(cflts0,	IN_SP,  EXACT, U4(spu_convts(F4(a), 0))) \
 OP(cflts8,	IN_SP,  EXACT, U4(spu_convts(F4(a), 8))) \
 OP(csflt,	IN_INT, NONE,  U4(spu_convtf(S4(a), 3))) \
 OP(frest,	IN_SP,  NONE,  U4(spu_re(F4(a)))) \
 OP(frsqest,	IN_SP,  NONE,  U4(spu_rsqrte(F4(a)))) \
 OP(fdiv,	IN_SP,  NONE,  U4(fdiv(F4(a), F4(b)))) \
 OP(fa_e,	IN_SPE, SPF,   U4(spu_add(F4(a), F4(b)))) \
 OP(fs_e,	IN_SPE, SPF,   U4(spu_sub(F4(a), F4(b)))) \
 OP(fm_e,	IN_SPE, SPF,   U4(spu_mul(F4(a), F4(b)))) \
 OP(dfa,	IN_DP,  DPF,   U4(spu_add(D2(a), D2(b)))) \
 OP(dfs,	IN_DP,  DPF,   U4(spu_sub(D2(a), D2(b)))) \
 OP(dfm,	IN_DP,  DPF,   U4(spu_mul(D2(a), D2(b)))) \
 OP(dfma,	IN_DP,  DPF,   U4(spu_madd(D2(a), D2(b), D2(c)))) \
 OP(dfms,	IN_DP,  DPF,   U4(spu_msub(D2(a), D2(b), D2(c)))) \
 OP(dfnms,	IN_DP,  DPF,   U4(spu_nmsub(D2(a), D2(b), D2(c)))) \
 OP(dfnma,	IN_DP,  DPF,   U4(spu_nmadd(D2(a), D2(b), D2(c)))) \
 OP(dfceq,	IN_DP,  EXACT, U4(spu_cmpeq(D2(a), D2(b)))) \
 OP(dfcgt,	IN_DP,  EXACT, U4(spu_cmpgt(D2(a), D2(b)))) \
 OP(dfcmeq,	IN_DP,  EXACT, U4(spu_cmpabseq(D2(a), D2(b)))) \
 OP(dfcmgt,	IN_DP,  EXACT, U4(spu_cmpabsgt(D2(a), D2(b)))) \
 OP(fesd,	IN_SP,  DPF,   U4(spu_extend(F4(a)))) \
 OP(frds,	IN_DP,  SPF02, U4(spu_roundtf(D2(a)))) \
 OP(dfa_d,	IN_DPD, DPF,   U4(spu_add(D2(a), D2(b)))) \
 OP(dfs_d,	IN_DPD, DPF,   U4(spu_sub(D2(a), D2(b)))) \
 OP(dfm_d,	IN_DPD, DPF,   U4(spu_mul(D2(a), D2(b)))) \
 OP(dfma_d,	IN_DPD, DPF,   U4(spu_madd(D2(a), D2(b), D2(c))))

enum {
#define X(n, i, c, e) OPX_##n,
	OPLIST(X)
#undef X
	OPX_N
};

#define EX_QW		256	/* quadwords per op and pass */
#define EX_OP_BYTES	(EX_QW * 16)

#endif
