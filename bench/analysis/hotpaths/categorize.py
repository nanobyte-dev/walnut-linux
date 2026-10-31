#!/usr/bin/env python3
"""Per-game CPU class shares and graphics-stack hot paths from analyze.py's pickles
(docs/HOTPATHS.md). usage: categorize.py RUN.pkl... (one per game; writes summary.json here)
A sample's hot path is the innermost Mesa frame (leaf outwards) that matches a RULES pattern, so
memcpy, the kernel and ps3rsx under a Mesa function count for that function's category."""
import pickle, collections, re, sys, json

MESA = ('libgallium', 'libGLX_mesa', 'libGL.so', 'libGLdispatch', 'libGLX.so', 'libEGL', 'dri_gbm')

# innermost matching frame (scanning from the leaf outwards) decides the category
RULES = [
    ('hud', r'^hud_|number_to_human_readable'),
    ('vtx-translate', r'^generic_run|^translate_|^emit_R|^u_vbuf_translate|_unpack_rgba_float$'),
    ('query-wait', r'^nv30_query_result|^nv30_query_wait'),
    ('fence-wait', r'nouveau_fence_wait|^_nouveau_fence_wait|^nouveau_bo_wait'),
    ('submit-ioctl', r'^pushbuf_flush|^pushbuf_submit|^nouveau_pushbuf_kick'),
    ('bo-alloc/free', r'^nouveau_bo_new|^nouveau_bo_ref$|^nouveau_bo_del|^nouveau_mm_|^bo_free|^nouveau_fence_new|'
                      r'nouveau_fence_unref_bo|^nouveau_fence_del'),
    ('bo-map', r'^nouveau_bo_map'),
    ('tex-upload', r'^nv30_miptree_transfer|^nv30_transfer|^_mesa_texstore|^texstore_|^st_TexSubImage|^st_TexImage|'
                   r'^_mesa_format_convert|^u_default_texture_subdata|^st_texture_image_map|^_mesa_TexSubImage|'
                   r'^_mesa_TexImage|^util_format_.*pack|^st_finalize_texture|^st_generate_mipmap|^speq_|^_mesa_make_temp|'
                   r'^nv30_blit|^nv30_resource_copy|^util_copy_rect|^util_format_translate'),
    ('vtx-upload', r'^u_upload_data|^u_upload_alloc|^u_vbuf_upload_buffers|^nv30_vbo_upload|^nouveau_user_buffer|'
                   r'^nouveau_buffer_|^nv30_buffer|^st_upload_|^_mesa_BufferSubData|^_mesa_buffer_sub_data|'
                   r'^bufferobj_data|^st_bufferobj|^u_default_buffer_subdata|^nv30_index_upload|^vbo_get_minmax'),
    ('fp-const', r'^nv30_fragprog_validate|^nv30_fragprog_upload|^nvfx_fp_'),
    ('nv30-validate', r'^nv30_state_validate|^nv30_.*_validate|^nv30_validate|^nv30_vertprog|^nv30_fragtex|'
                      r'^nv30_set_|^nv30_bind_|^nv30_.*_state_|^nv40_verttex|^nv30_fragprog_'),
    ('winsys-bookkeeping', r'^pushbuf_|^nouveau_pushbuf|^nouveau_bufctx|^nouveau_fence_ref|^nouveau_fence_|'
                           r'^nouveau_bo_|^nouveau_'),
    ('nv30-draw-emit', r'^nv30_draw_vbo|^nv30_emit_|^nv30_render|^nv30_push_|^emit_vertices|^nv30_clear'),
    ('st-validate', r'^st_|^_mesa_update_|^update_program|^update_|^cso_|^_mesa_get_fixed_func|^_mesa_load_state|'
                    r'^fetch_state|^_mesa_reference_|^_mesa_lookup|^u_vbuf_|^util_set_vertex|^_mesa_search_program|'
                    r'^_mesa_lock_context_textures|^validate_|^_mesa_validated|^vbo_|^bind_|^_mesa_bind'),
    ('swap/present', r'^loader_dri3|^dri3_|^dri_flush|^dri2_|^__glXSwapBuffers|^glXSwapBuffers|^xcb_|^st_context_flush|'
                     r'^nv30_context_flush|^nouveau_fence_kick'),
    ('gl-api', r'^_mesa_|^_dispatch_stub|^gl[A-Z]|^vbo_exec|^_hw_select|^_save_|^__glX'),
]
RULES = [(n, re.compile(p)) for n, p in RULES]
KGPU_LEAF = re.compile(r'^(ps3rsx|drm_|dma_|ttm_)')


def ismesa(f):
    return f[1].startswith(MESA)


def category(st):
    """st: frames outer..leaf. Returns (category, leafclass) or None when no graphics frame."""
    idx = [i for i, f in enumerate(st) if ismesa(f)]
    leaf = st[-1] if st else ('unk', '', '')
    if not idx:
        if leaf[0] == 'kgpu':
            return ('kernel-gpu-other', 'kgpu')
        return None
    for f in reversed(st[:idx[-1] + 1]):
        if not ismesa(f):
            continue
        for name, rx in RULES:
            if rx.search(f[2]):
                return (name, leaf[0] if leaf is not f else 'self')
    return ('mesa-other', leaf[0] if leaf is not st[idx[-1]] else 'self')


def load(paths=None):
    out = {}
    for p in paths or sys.argv[1:]:
        r = pickle.load(open(p, 'rb'))
        b = dict(l.split('=', 1) for l in open(r['run'] + '/bench.txt').read().splitlines() if '=' in l)
        out[b.get('game') or p] = r
    return out


def self_and_inclusive(runs):
    """self time of driver functions (memcpy under a Mesa caller as 'memcpy< caller') and inclusive
    time of every Mesa/ps3rsx function, % of each game's samples"""
    selfc = collections.defaultdict(dict)
    incl = collections.defaultdict(dict)
    for lab, r in runs.items():
        T = sum(r['game'].values())
        s = collections.Counter()
        inc = collections.Counter()
        for (thr, st), c in r['game'].items():
            if not st:
                continue
            lf = st[-1]
            if lf[0] in ('mesa', 'kgpu', 'drm'):
                s[lf[2]] += c
            elif lf[0] == 'libcmem':
                caller = next((f[2] for f in reversed(st) if ismesa(f)), None)
                if caller:
                    s['memcpy< %s (%s)' % (caller, lf[2])] += c
            seen = set()
            for f in st:
                if (ismesa(f) or f[0] == 'kgpu') and f[2] not in seen:
                    seen.add(f[2])
                    inc[f[2]] += c
        for k, v in s.items():
            selfc[k][lab] = 100 * v / T
        for k, v in inc.items():
            incl[k][lab] = 100 * v / T
    return selfc, incl


def show(d, labs, n, title):
    print(title)
    print('%6s %3s  %-50s ' % ('mean', 'n', 'function') + ' '.join('%6s' % l[:6] for l in labs))
    for k, v in sorted(d.items(), key=lambda x: -sum(x[1].values()))[:n]:
        print('%6.2f %3d  %-50s ' % (sum(v.values()) / len(labs), sum(1 for x in v.values() if x >= 0.5), k[:50]) +
              ' '.join('%6.1f' % v.get(l, 0) for l in labs))


def main():
    runs = load()
    classes = ['game', 'mesa', 'drm', 'kgpu', 'kernel', 'libcmem', 'libc', 'other', 'unk']
    tab = {}
    cats = collections.defaultdict(dict)
    catleaf = collections.defaultdict(lambda: collections.defaultdict(float))
    gfx = {}
    xorg = {}
    for lab, r in runs.items():
        T = sum(r['game'].values())
        cls = collections.Counter()
        cc = collections.Counter()
        g = 0
        for (thr, st), c in r['game'].items():
            cls[st[-1][0] if st else 'unk'] += c
            k = category(st)
            if k:
                cc[k[0]] += c
                catleaf[k[0]][k[1]] += 100 * c / T / len(runs)
                g += c
        tab[lab] = {k: 100 * cls[k] / T for k in classes}
        tab[lab]['n'] = T
        for k, v in cc.items():
            cats[k][lab] = 100 * v / T
        gfx[lab] = 100 * g / T
        xorg[lab] = 100 * sum(r['xorg'].values()) / T
    json.dump({'classes': tab, 'cats': cats, 'gfx': gfx, 'xorg': xorg,
               'catleaf': {k: dict(v) for k, v in catleaf.items()}}, open('summary.json', 'w'), indent=1)
    print('classes: % of the game process\'s samples by the sampled instruction; gfx%: samples with a Mesa')
    print('frame or a ps3rsx/drm leaf; xorg%: Xorg\'s samples as % of the game\'s')
    labs = list(runs)
    print('%-16s %7s ' % ('game', 'samples') + ' '.join('%7s' % c for c in classes) + '   gfx%  xorg%')
    for lab in labs:
        t = tab[lab]
        print('%-16s %7d ' % (lab, t['n']) + ' '.join('%7.1f' % t[c] for c in classes) +
              '  %5.1f  %5.1f' % (gfx[lab], xorg[lab]))
    print()
    print('%-20s %6s %3s ' % ('category', 'mean', 'n>1') + ' '.join('%6s' % l[:6] for l in labs) + '  leaf mix')
    for k, v in sorted(cats.items(), key=lambda x: -sum(x[1].values())):
        mean = sum(v.values()) / len(labs)
        n1 = sum(1 for x in v.values() if x >= 1)
        lm = sorted(catleaf[k].items(), key=lambda x: -x[1])[:4]
        print('%-20s %6.2f %3d ' % (k, mean, n1) + ' '.join('%6.1f' % v.get(l, 0) for l in labs) +
              '  ' + ' '.join('%s:%.2f' % (a, b) for a, b in lm))
    selfc, incl = self_and_inclusive(runs)
    print('\nmean = average over the games of % of each game\'s CPU; n = games at 0.5% or more')
    show(selfc, labs, 40, '\nself time in the driver (memcpy< X: memcpy/memset called from X)')
    show(incl, labs, 40, '\ninclusive time of Mesa and ps3rsx functions')


if __name__ == '__main__':
    main()
