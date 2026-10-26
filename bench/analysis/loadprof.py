#!/usr/bin/env python3
"""loadprof.py RUN_DIR...: where a game's start-up goes, from a PERF_LOAD=1 run (docs/PROFILING.md
"Start-up profiling"); also report.py --load. The start-up span is what the measured window leaves
out: the game's exec until its load ends (bench.txt load_end: "load"), then until the window starts
(the warm-up, WARMUP s of play: "warmup"). Per phase and for the whole span:
  wall        the phase's seconds
  main thread from stalls.rec (STALLS=1, stalls.py's causes over all of the phase): disk, io, swap,
              reclaim, gpu, cpu, wait ... seconds; without a record, the main thread's CPU from perf
  CPU         perf-load-stacks.tsv (perf-post.sh: perfprof.py collapse --load): seconds of CPU of the
              game's processes (all threads, the main thread apart), Xorg and kswapd0, by group and by
              the leaf's DSO class, with the top functions of each group
  I/O         load-io/ (client.sh): MB and requests read from the disks, the disk's busy seconds,
              swap-ins (zram, VRAM swap), the game's major faults, read syscalls and read_bytes,
              direct reclaim and kswapd scans (/proc/vmstat)
  ld.so       ld/stats.<pid> (LD_DEBUG=statistics): the dynamic loader's start-up time, relocation
              and object loading (timebase ticks on ppc64: load-io/timebase), relocation counts;
              libraries dlopened later (libGL, libgallium) show only in perf (group link)
Groups (a sample's group: the innermost frame of its stack that matches, first the umbrella rules
over every user frame, then for a kernel leaf the kernel's paths, then the leaf rules over the user
frames from the leaf outwards, else the leaf's DSO class; GROUPS below has each one's lever):
  link, init, shader.cache, shader.backend, shader.glsl, dxt, mipmap, tex, buffers, zlib, png,
  jpeg, audio, zstd, image, files, lua, fonts, k.fault, k.read, k.open, k.mmap, k.exec, k.reclaim,
  kgpu, k.other, game, mesa, drm, libc, cxx, other, unk
Then the levers: for each group the main thread's seconds (the critical path: what the load's wall
time can lose at most if the work went away; half of it if it ran twice as fast) and every thread's,
for the load and the warm-up; the disk: the main thread's wait against the same bytes read
sequentially at LOADPROF_DISK_MBS (default 30 MB/s, a USB 2.0 disk). summary(d) gives report.py its
lp_* columns."""
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import perfprof  # noqa: E402
import report  # noqa: E402

PHASES = ("load", "warmup")
USER_HZ = 100
DISK_MBS = float(os.environ.get("LOADPROF_DISK_MBS") or 30)

# group: what it is, the lever (what would make it cheaper)
GROUPS = {
    "link": ("dynamic linking: ld.so mapping, relocation, symbol lookup, lazy binding, dlopen",
             "fewer and smaller DSOs, DT_GNU_HASH, -z now vs lazy binding, prelinking; LD_DEBUG gives the start-up part"),
    "init": ("library and static constructors (_dl_init, _GLOBAL__sub_I_*)", "less work at load time in constructors"),
    "shader.cache": ("Mesa's disk shader cache itself: hashing, (de)serialising, compression, its files",
                     "smaller entries; a cache hit costs this instead of the compile"),
    "shader.backend": ("the nv30 back end: st variants, nir_to_tgsi, nvfx translate",
                       "runs at every start, warm cache or not: an nv30-side cache of the machine code"),
    "shader.glsl": ("the GL front end: GLSL compile and link, NIR at link, ARB and fixed-function programs",
                    "Mesa's disk shader cache skips it on a hit (SHADERCACHE=cold vs warm)"),
    "dxt": ("DXT/S3TC compression: libspeq (the PPE's share), Mesa's s3tc, the game's own",
            "precompressed textures; the SPUs' work shows as the main thread's waits, not here"),
    "mipmap": ("mipmap generation: Mesa's, the game's own", "precomputed mips, or mips by the GPU (blits)"),
    "tex": ("Mesa texture upload: texstore, format conversion, pack/unpack, miptree transfers",
            "formats the RSX takes as they are (no conversion), fewer copies"),
    "buffers": ("Mesa buffer uploads (vertex, index)", "fewer, larger uploads"),
    "zlib": ("inflate: zlib and the games' own (pk3/zip members, PNG streams)",
             "zlib-ng (about 2x on inflate), stored (uncompressed) archive members for the big files"),
    "png": ("PNG decode apart from inflate (filters, rows; libpng and the games' own)",
            "a faster decoder, or textures in a format that needs none (DDS, KTX)"),
    "jpeg": ("JPEG decode (libjpeg-turbo, the games' own)",
             "libjpeg-turbo's AltiVec code (JSIMD_FORCENONE=1 shows what it saves); DXT instead"),
    "audio": ("audio decode and sound font loading (Vorbis, Ogg, FLAC, Opus, MP3, SF2)",
              "decode on first use or in a thread, or keep PCM"),
    "zstd": ("other decompressors (zstd, lz4, xz, bzip2)", "a cheaper level or format"),
    "image": ("other image decoders (stb_image, SDL_image, TGA/BMP/PCX, the games' loaders)", "a faster decoder"),
    "files": ("the game's file system code (pk3 directory scans, path hashing, archive headers)",
              "fewer archives and files, an index"),
    "lua": ("Lua (LuaJIT: mods, scripts)", "less script work at start"),
    "fonts": ("fonts (FreeType, fontconfig, HarfBuzz)", "fewer faces and sizes rendered at start"),
    "k.fault": ("kernel: page faults (first touch, file pages mapped)",
                "fewer pages touched, larger mappings; the I/O of file faults is under disk wait"),
    "k.read": ("kernel: read/pread, page cache copies, readahead", "bigger reads, mmap"),
    "k.open": ("kernel: open, stat, getdents, close (path lookup)", "fewer files: pack them"),
    "k.mmap": ("kernel: mmap, munmap, brk, mprotect", "fewer mappings"),
    "k.exec": ("kernel: exec", "-"),
    "k.reclaim": ("kernel: reclaim and swap compression in the game's own context",
                  "less memory at load (see also kswapd0)"),
    "kgpu": ("kernel GPU driver (ps3rsx, DRM): buffer allocation, uploads", "fewer, larger buffers"),
    "k.other": ("kernel: the rest", "-"),
    "game": ("the game's own code (outside the groups above)", "the game's own load path"),
    "mesa": ("Mesa, the rest (state, draws, context creation)", "-"),
    "drm": ("libdrm (nouveau pushbuf, buffer objects)", "-"),
    "libc": ("libc and libm outside the groups (memcpy, malloc, strings)", "-"),
    "cxx": ("libstdc++", "-"),
    "other": ("other libraries", "-"),
    "unk": ("samples perf could not place", "-"),
}
DECOMP = ("zlib", "png", "jpeg", "audio", "zstd", "image")
SHADER = ("shader.glsl", "shader.backend", "shader.cache")


def rx(p):
    """a symbol pattern: matches at the start of the name or of a C++ qualifier's last parts"""
    return re.compile(r"(?:^|::)(?:%s)" % p)


def drx(p):
    return re.compile(r"^(?:%s)" % p)


MESA = perfprof.MESA
LDSO = drx(r"ld64\.so|ld-linux|ld\.so")
# the umbrella rules: whatever runs under such a frame belongs to it (the innermost one wins);
# (group, DSO basename pattern or None, symbol pattern or None); a frame matches when both do
UMBRELLA = [
    ("init", None, rx(r"_dl_init$|call_init$|_init$|_GLOBAL__sub_I_|__static_initialization_and_destruction")),
    ("link", LDSO, None),
    ("link", None, rx(r"__?dlopen|dlopen_implementation|dlmopen|_dl_open|dl_open_worker")),
    ("shader.cache", MESA, rx(r"disk_cache_|cache_put_job|cache_get_job|destroy_put_job|st_load_nir_from_disk_cache|"
                              r"st_store_nir_in_disk_cache|st_deserialise_nir_program|shader_cache_(read|write)_program_metadata|"
                              r"st_serialize_(base_)?nir|nir_serialize|nir_deserialize|write_nir_to_cache|blob_|"
                              r"util_compress|util_decompress|mesa_db_|mesa_cache_db|foz_|_mesa_sha1|_mesa_blake3|"
                              r"mesa_blake3|blake3_|mesa_sha1")),
    ("shader.backend", MESA, rx(r"nir_to_tgsi|ntt_|tgsi_(?!exec)|_nvfx_fragprog_translate|_nvfx_vertprog_translate|nvfx_|"
                                r"nv30_fp_state_create|nv30_vp_state_create|st_create_fp_variant|st_create_common_variant|"
                                r"st_create_vp_variant|st_get_fp_variant|st_get_common_variant|st_get_vp_variant|"
                                r"st_finalize_nir|st_precompile_shader_variant|st_create_nir_shader|st_prepare_vertex_program")),
    ("dxt", drx(r"libspeq"), None),
    ("dxt", None, rx(r"speq_|util_format_dxt\w*_pack_|util_format_rgtc\w*_pack_|util_format_dxtn|tx_compress_dxtn|"
                     r"compress_dxt|util_format_s3tc|_mesa_texstore_rgba?_dxt|texstore_rgba?_dxt|stb_compress_dxt|"
                     r"stb__Compress|squish|rygCompress")),
    ("mipmap", MESA, rx(r"_mesa_generate_mipmap|st_generate_mipmap|util_gen_mipmap|_mesa_GenerateMipmap|"
                        r"_mesa_GenerateTextureMipmap|generate_mipmap|_mesa_prepare_mipmap_level|do_row|make_[123]d_mipmap|"
                        r"make_[12]d_stack_mipmap")),
    ("mipmap", None, rx(r"R_MipMap|R_BlendOverTexture|scaletexture|ScaleTexture|stbir_|stbir__|gluBuild2DMipmaps|gluScaleImage")),
    ("shader.glsl", MESA, rx(r"_mesa_glsl_|_mesa_compile_shader|link_shaders|_mesa_CompileShader|_mesa_LinkProgram|"
                             r"link_program|_mesa_link_program|st_link_shader|st_link_glsl_to_nir|st_glsl_to_nir|glsl_to_nir|"
                             r"gl_nir_|_mesa_ast_|ast_|glcpp|do_common_optimization|st_nir_preprocess|st_nir_lower|"
                             r"st_finalize_program|_mesa_ShaderSource|build_program_resource_list|_mesa_parse_arb|"
                             r"_mesa_get_fixed_func_(vertex|fragment)_program|create_new_program|st_translate_prog_to_nir|"
                             r"prog_to_nir|ir_|glsl_|lower_|opt_|link_|exec_list|do_(dead_code|copy_propagation|"
                             r"function_inlining|if_simplification|tree_grafting|vec_index_to|algebraic|constant|lower_jumps|"
                             r"mat_op_to_vec|minmax_prune|rebalance_tree|structure_splitting|swizzle_swizzle|noop_swizzle|"
                             r"vectorize|set_program_inouts)")),
]
# the leaf rules: the first user frame from the leaf outwards that matches one
LEAF = [
    ("zlib", drx(r"libz\.so|libz-ng"), None),
    ("zlib", None, rx(r"inflate|adler32|crc32|uncompress|tinfl_|mz_inflate|mz_uncompress|stbi__parse_zlib|stbi__zhuffman|"
                      r"stbi__decode_zlib|stbi_zlib_|stbi__zexpand|stbi__compute_huffman_codes|stbi__parse_huffman_block|"
                      r"stbi__parse_uncompressed_block|Inflate_|ZLib_|puff$|zlib_")),
    ("png", drx(r"libpng|libspng"), None),
    ("png", None, rx(r"png_|Png_|lodepng|stbi__\w*png|stbi__create_png_image|stbi__compute_transparency|stbi__de_iphone|"
                     r"R_LoadPNG|LoadPNG|DecodePNG|spng_|CImageLoaderPng|DecompressIDATs")),
    ("jpeg", drx(r"libjpeg|libturbojpeg"), None),
    ("jpeg", None, rx(r"jpeg_|jsimd_|decode_mcu|ycc_rgb_convert|tj[A-Z]|stbi__jpeg|stbi__idct|stbi__YCbCr|"
                      r"stbi__decode_jpeg|R_LoadJPG|LoadJPG|CImageLoaderJPG")),
    ("audio", drx(r"lib(vorbis|vorbisfile|ogg|FLAC|opus|opusfile|mpg123|sndfile|modplug|xmp|mikmod|gme|SDL2?_mixer|"
                  r"SDL3_mixer|fluidsynth|vorbisidec|speex|mp3lame)"), None),
    ("audio", None, rx(r"ov_|vorbis_|ogg_|stb_vorbis|FLAC_|FLAC__|opus_|op_|mpg123_|drmp3|drflac|drwav|S_LoadSound|"
                       r"S_CodecLoad|S_OGG_|S_WAV_|ResampleSfx|_vds_|mdct_|drft_|decode_packed_entry_number|floor[01]_|"
                       r"res[012]_|mapping0_|fluid_sffile|fluid_defsfont|Mix_LoadWAV|Mix_LoadMUS|alutLoad|sf_read")),
    ("zstd", drx(r"lib(zstd|lz4|lzma|bz2|brotli)"), None),
    ("zstd", None, rx(r"ZSTD_|LZ4_|lzma_|BZ2_|HUF_|FSE_")),
    ("image", drx(r"lib(SDL[23]?_image|webp|tiff|IL|freeimage|jxl|avif|gif|openjp2)"), None),
    ("image", None, rx(r"stbi_|stbi__|IMG_|WebP|SDL_LoadBMP|LoadTGA|R_LoadTGA|R_LoadBMP|R_LoadPCX|LoadPCX|"
                       r"CImageLoader|Image_Decode|Bitmap_Decode")),
    ("files", drx(r"libphysfs|libzip|libminizip"), None),
    ("files", None, rx(r"FS_|unz|Sys_ListFiles|Sys_ListFilteredFiles|HashFileName|CFileSystem|CZipReader|CArchiveLoader|"
                       r"PHYSFS_|__PHYSFS|ZipArchive|zip_")),
    ("tex", MESA, rx(r"nv30_miptree|nv30_transfer|_mesa_texstore|texstore_|st_TexSubImage|st_TexImage|st_CompressedTex|"
                     r"_mesa_format_convert|_mesa_swizzle_and_convert|_mesa_unpack_|_mesa_pack_|u_default_texture_subdata|"
                     r"st_texture_image_map|_mesa_TexSubImage|_mesa_TexImage|_mesa_TextureSubImage|_mesa_CompressedTex|"
                     r"util_format_\w*pack|util_format_\w*fetch|util_format_translate|st_finalize_texture|_mesa_make_temp|"
                     r"util_copy_rect|util_copy_box|st_AllocTextureImageBuffer|st_texture_create|_mesa_image_|_mesa_store_|"
                     r"_mesa_upscale_teximage2d|st_choose_format|st_choose_matching_format|_mesa_texture_sub_image|"
                     r"teximage|texsubimage|texture_sub_image|_mesa_tex|nv30_resource_copy_region|nv30_transfer_rect|"
                     r"nv30_blit|_mesa_compressed_")),
    ("buffers", MESA, rx(r"u_upload_|u_vbuf_upload_buffers|nv30_vbo_upload|nouveau_user_buffer|nouveau_buffer_|nv30_buffer|"
                         r"st_upload_|_mesa_BufferSubData|_mesa_buffer_sub_data|bufferobj_data|st_bufferobj|_mesa_BufferData|"
                         r"u_default_buffer_subdata|nv30_index_upload|vbo_get_minmax|_mesa_NamedBufferData|buffer_data")),
    ("lua", drx(r"libluajit|liblua"), None),
    ("lua", None, rx(r"lj_|lua_|luaL_|luaV_|luaD_|luaH_|luaS_|luaC_")),
    ("fonts", drx(r"lib(freetype|fontconfig|harfbuzz|SDL[23]?_ttf|expat)"), None),
    ("fonts", None, rx(r"FT_|Fc[A-Z]|hb_|TTF_|stbtt_")),
]
# kernel paths: the first kernel frame from the leaf outwards that matches one
KERNEL = [
    ("k.reclaim", r"try_to_free_pages|do_try_to_free_pages|shrink_|__alloc_pages_slowpath|__alloc_pages_direct_|"
                  r"compact_zone|compaction_alloc|zram_|zs_|lzo1x_|lz4_|zswap_|swap_writepage|ps3vswap"),
    ("kgpu", perfprof.KGPU_SYM.pattern.lstrip("^")),
    ("k.exec", r"do_execveat_common|sys_execve|bprm_execve|load_elf_binary|begin_new_exec|exec_binprm|setup_new_exec"),
    ("k.fault", r"do_page_fault|__do_page_fault|___do_page_fault|handle_mm_fault|__handle_mm_fault|do_hash_fault|"
                r"__do_hash_fault|hash_page|filemap_fault|do_anonymous_page|do_fault|do_read_fault|do_cow_fault|do_wp_page|"
                r"wp_page_copy|do_swap_page|exc_page_fault|handle_page_fault|data_access_common|instruction_access_common|"
                r"filemap_map_pages"),
    ("k.read", r"ksys_read|ksys_pread64|vfs_read|vfs_readv|sys_read|sys_pread64|sys_readv|sys_preadv|filemap_read|"
               r"generic_file_read_iter|ext4_file_read_iter|filemap_get_pages|page_cache_ra_|page_cache_sync_ra|"
               r"page_cache_async_ra|ondemand_readahead|read_pages|copy_page_to_iter|_copy_to_iter|ext4_mpage_readpages|"
               r"mpage_readahead"),
    ("k.open", r"do_sys_openat2|do_sys_open|path_openat|do_filp_open|vfs_statx|vfs_fstatat|sys_newfstatat|sys_statx|"
               r"sys_stat|sys_lstat|sys_fstat|link_path_walk|path_lookupat|filename_lookup|iterate_dir|sys_getdents64|"
               r"__fput|sys_close|close_fd|sys_access|sys_faccessat|do_faccessat|sys_readlink|sys_openat"),
    ("k.mmap", r"do_mmap|vm_mmap_pgoff|ksys_mmap_pgoff|sys_mmap|__vm_munmap|sys_munmap|do_vmi_munmap|do_brk_flags|sys_brk|"
               r"mprotect_fixup|do_mprotect_pkey|sys_mprotect|sys_madvise|do_madvise|unmap_region|mmap_region|sys_mremap"),
]
KERNEL = [(g, re.compile(r"^(?:__se_|__do_|__powerpc_|__x64_|__arm64_)*(?:%s)" % p)) for g, p in KERNEL]


def norm(sym):
    """a symbol for matching: no ppc64 ELFv1 dot, no (anonymous namespace), no arguments"""
    s = (sym or "").lstrip(".").replace("(anonymous namespace)::", "")
    i = s.find("(")
    return s[:i] if i > 0 else s


def base(dso):
    return (dso or "").rsplit("/", 1)[-1]


def kern(dso):
    return perfprof.is_kernel(dso) or (dso or "").startswith("[ps3rsx") or (dso or "").startswith("[drm")


def rule_hit(rules, sym, dso):
    b = base(dso)
    n = None
    for g, dr, sr in rules:
        if dr is not None and not dr.search(b):
            continue
        if sr is not None:
            if n is None:
                n = norm(sym)
            if not sr.search(n):
                continue
        return g
    return None


class Ctx:
    """what tells the game's own code apart (perfprof.context, with the start-up record's files)"""

    def __init__(self, d):
        d = Path(d)
        tg, exe, dirs, gpkgs, pkgs = perfprof.context(d)
        le = d / "perf-load.exe"
        if le.exists() and le.read_text().strip():
            exe = le.read_text().strip()
            dirs = [exe.rsplit("/", 1)[0]] if exe.count("/") > 2 else []
            b = exe.rsplit("/", 1)[-1]
            dirs += ["/usr/lib/" + b, "/usr/lib/games/" + b, "/usr/share/games/" + b]
            if pkgs.get(exe):
                gpkgs = tuple(sorted(set(gpkgs) | {pkgs[exe]}))
        self.exe, self.dirs, self.gpkgs, self.pkgs = exe, dirs, gpkgs, pkgs

    def dso_class(self, sym, dso):
        c = perfprof.classify(dso, sym, self.exe, self.dirs, self.gpkgs, self.pkgs)
        if c == "libc" and LDSO.search(base(dso)):
            return "ld.so"
        if c == "other" and re.search(r"^libstdc\+\+", base(dso)):
            return "cxx"
        return c


def group_of(frames, ctx):
    """frames: [(sym, dso)], the leaf first -> the sample's group"""
    for sym, dso in frames:
        if not kern(dso) and (g := rule_hit(UMBRELLA, sym, dso)):
            return g
    lsym, ldso = frames[0] if frames else ("[unknown]", "[unknown]")
    if kern(ldso):
        for sym, dso in frames:
            if not kern(dso):
                break
            n = norm(sym)
            for g, r in KERNEL:
                if r.search(n):
                    return g
        return "kgpu" if ctx.dso_class(lsym, ldso) == "kgpu" else "k.other"
    for sym, dso in frames:
        if not kern(dso) and (g := rule_hit(LEAF, sym, dso)):
            return g
    c = ctx.dso_class(lsym, ldso)
    return {"ld.so": "link", "kernel": "k.other"}.get(c, c)


# --- reading a run ---------------------------------------------------------------------------
def read_stacks(path):
    """perf-load-stacks.tsv -> (header, [(phase, pid, tid, comm, samples, period, frames leaf first)])"""
    head, rows = {}, []
    if not path.exists():
        return head, rows
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith("# boundary="):
            head = perfprof.kv_line_text(line[2:])
            continue
        if line.startswith("#") or not line.strip():
            continue
        f = line.split("\t")
        if len(f) < 6:
            continue
        frames = []
        for x in f[6:]:
            s, _, d = x.partition("\x1f")
            frames.append((s, d))
        rows.append((f[0], int(f[1]), int(f[2]), f[3], int(f[4]), int(f[5]), frames[::-1]))
    return head, rows


def read_kv_file(path):
    """/proc/<pid>/io or status, vmstat: name -> int"""
    out = {}
    if not path.exists():
        return out
    for line in path.read_text(errors="replace").splitlines():
        k, _, v = line.replace(":", " ", 1).partition(" ")
        v = v.split()
        if v and re.fullmatch(r"-?\d+", v[0]):
            out[k.strip()] = int(v[0])
    return out


def read_stat(path):
    """/proc/<pid>/stat: minflt, majflt, utime, stime (s), threads"""
    if not path.exists():
        return None
    t = path.read_text(errors="replace")
    r = t[t.rfind(")") + 2:].split()
    if len(r) < 18:
        return None
    return {"minflt": int(r[7]), "majflt": int(r[9]), "utime": int(r[11]) / USER_HZ, "stime": int(r[12]) / USER_HZ,
            "threads": int(r[17])}


WHOLE = re.compile(r"^(sd[a-z]+|ps3d[a-z]+|nvme\d+n\d+|mmcblk\d+|vd[a-z]+|hd[a-z]+)$")


def read_disks(path):
    """/proc/diskstats -> {hdd|zram|vswap: [reads, sectors read, ms reading, io ms]} (whole disks only)"""
    out = {"hdd": [0, 0, 0, 0], "zram": [0, 0, 0, 0], "vswap": [0, 0, 0, 0]}
    if not path.exists():
        return None
    for line in path.read_text(errors="replace").splitlines():
        f = line.split()
        if len(f) < 14:
            continue
        name = f[2]
        kind = "zram" if name.startswith("zram") else "vswap" if name.startswith("ps3vswap") else (
            "hdd" if WHOLE.match(name) else None)
        if kind is None:
            continue
        v = [int(x) for x in f[3:]]
        o = out[kind]
        for i, j in enumerate((0, 2, 3, 9)):
            o[i] += v[j]
    return out


def io_phases(d):
    """{phase: {...}} from load-io/: the disks and the VM between the snapshots, the game's counters"""
    io = Path(d) / "load-io"
    if not io.is_dir():
        return {}
    snaps = {}
    for lab in ("start", "load", "warm"):
        if (io / (lab + ".info")).exists():
            snaps[lab] = {"disks": read_disks(io / (lab + ".diskstats")), "vm": read_kv_file(io / (lab + ".vmstat")),
                          "stat": read_stat(io / (lab + ".stat")), "main": read_stat(io / (lab + ".mainstat")),
                          "io": read_kv_file(io / (lab + ".io")), "status": read_kv_file(io / (lab + ".status"))}
    out = {}
    for ph, (a, b) in (("load", ("start", "load")), ("warmup", ("load", "warm"))):
        if a not in snaps or b not in snaps:
            continue
        A, B = snaps[a], snaps[b]
        r = {}
        if A["disks"] and B["disks"]:
            for k in ("hdd", "zram", "vswap"):
                x = [q - p for p, q in zip(A["disks"][k], B["disks"][k])]
                r[k + "_reads"], r[k + "_mb"], r[k + "_read_s"], r[k + "_busy_s"] = (
                    x[0], x[1] * 512 / 1e6, x[2] / 1000, x[3] / 1000)
        vm = {k: B["vm"].get(k, 0) - A["vm"].get(k, 0) for k in B["vm"]}

        def vsum(*pre):
            return sum(v for k, v in vm.items() if any(k.startswith(p) for p in pre))
        r.update(pgmajfault=vm.get("pgmajfault", 0), pswpin=vm.get("pswpin", 0), pswpout=vm.get("pswpout", 0),
                 direct_scan=vsum("pgscan_direct"), kswapd_scan=vsum("pgscan_kswapd"), allocstall=vsum("allocstall"),
                 refault=vsum("workingset_refault"), compact_stall=vm.get("compact_stall", 0))
        # the game's own: since its exec at "load" (the gate's counters are not the game's), then deltas
        g0 = None if ph == "load" else A
        for k, src in (("stat", "stat"), ("main", "main")):
            s1 = B[src]
            s0 = g0[src] if g0 else None
            if s1:
                for f in ("minflt", "majflt", "utime", "stime"):
                    r["%s_%s" % (k, f)] = s1[f] - (s0[f] if s0 else 0)
        for f in ("rchar", "read_bytes", "syscr"):
            if f in B["io"]:
                r["io_" + f] = B["io"][f] - (g0["io"].get(f, 0) if g0 else 0)
        for f in ("voluntary_ctxt_switches", "nonvoluntary_ctxt_switches"):
            if f in B["status"]:
                r[f] = B["status"][f] - (g0["status"].get(f, 0) if g0 else 0)
        out[ph] = r
    return out


LDNUM = r"([\d,.']+)"


def ld_stats(d, game_pid):
    """ld.so's statistics (LD_DEBUG=statistics) of the game's pid: its last start-up block (the
    commands before it exec'd in the same pid print their own first) and its exit totals"""
    ld = Path(d) / "ld"
    if not ld.is_dir():
        return None
    files = sorted(ld.glob("stats.*"))
    if not files:
        return None

    def num(s):
        return int(re.sub(r"[^\d]", "", s))

    out = {"files": len(files)}
    pick = [f for f in files if game_pid and f.name == "stats.%s" % game_pid]
    if not pick:   # without game_pid: the file with the most relocations
        pick = files
    best = None
    for f in pick:
        blocks, cur = [], None
        for line in f.read_text(errors="replace").splitlines():
            line = line.split(":", 1)[1] if re.match(r"^\s*\d+:", line) else line
            if "runtime linker statistics" in line:
                cur = {}
                blocks.append(cur)
                continue
            if cur is None:
                continue
            for key, pat in (("startup", r"total startup time in dynamic loader:\s*" + LDNUM),
                             ("reloc_time", r"time needed for relocation:\s*" + LDNUM),
                             ("load_time", r"time needed to load objects:\s*" + LDNUM),
                             ("final_relocs", r"final number of relocations:\s*" + LDNUM),
                             ("final_cached", r"final number of relocations from cache:\s*" + LDNUM),
                             ("relocs", r"^\s*number of relocations:\s*" + LDNUM),
                             ("cached", r"^\s*number of relocations from cache:\s*" + LDNUM),
                             ("relative", r"number of relative relocations:\s*" + LDNUM)):
                m = re.search(pat, line)
                if m and key not in cur:
                    cur[key] = num(m[1])
                    break
        start = [b for b in blocks if "startup" in b]
        fin = [b for b in blocks if "final_relocs" in b]
        if not start:
            continue
        r = dict(start[-1])
        r["execs"] = len(start)
        if fin:
            r.update(fin[-1])
        r["file"] = f.name
        if best is None or r.get("relocs", 0) > best.get("relocs", 0) or f.name == "stats.%s" % game_pid:
            best = r
            if f.name == "stats.%s" % game_pid:
                break
    if not best:
        return out
    out.update(best)
    tb = Path(d) / "load-io" / "timebase"
    hz = None
    if tb.exists():
        try:
            hz = float(tb.read_text().split()[0])
        except (ValueError, IndexError):
            hz = None
    out["hz"] = hz
    if hz:
        for k in ("startup", "reloc_time", "load_time"):
            if k in out:
                out[k + "_s"] = out[k] / hz
    return out


def phase_bounds(b):
    le = report.num(b.get("load_end"))
    se = report.startup_end(b)
    return le, se


def analyze(d):
    """the start-up analysis of run directory d, or None when it has nothing of it"""
    d = Path(d)
    b = report.kv(d / "bench.txt")
    head, rows = read_stacks(d / "perf-load-stacks.tsv")
    io = io_phases(d)
    if not rows and not io:
        return None
    ctx = Ctx(d)
    tg = perfprof.kv_line(d / "perf-load.targets")
    others = {int(v): k for k, v in tg.items() if k in ("xorg", "kswapd0") and v.isdigit()}
    gp = report.num(b.get("game_pid")) or report.num(tg.get("game"))
    main_tid = int(gp) if gp else None
    le, se = phase_bounds(b)
    res = {"dir": d, "b": b, "le": le, "se": se, "main": main_tid, "io": io,
           "ld": ld_stats(d, int(gp) if gp else None), "ph": {}}
    for ph in PHASES:
        res["ph"][ph] = {"grp": Counter(), "grp_main": Counter(), "cls": Counter(), "cls_main": Counter(),
                         "top": defaultdict(Counter), "threads": Counter(), "others": Counter(), "n": 0,
                         "cpu": 0.0, "cpu_main": 0.0, "altivec": 0.0}
    cache = {}
    for phase, pid, tid, comm, n, per, frames in rows:
        P = res["ph"].get(phase)
        if P is None:
            continue
        s = per / 1e9
        if pid in others:
            P["others"][others[pid]] += s
            continue
        key = tuple(frames)
        if key not in cache:
            cache[key] = group_of(frames, ctx)
        g = cache[key]
        lsym, ldso = frames[0] if frames else ("[unknown]", "[unknown]")
        c = ctx.dso_class(lsym, ldso)
        P["n"] += n
        P["cpu"] += s
        P["grp"][g] += s
        P["cls"][c] += s
        P["top"][g][(base(ldso), norm(lsym) or "[unknown]")] += s
        P["threads"][(tid, comm)] += s
        if g == "jpeg" and "altivec" in (lsym or "").lower():
            P["altivec"] += s
        if tid == main_tid:
            P["cpu_main"] += s
            P["grp_main"][g] += s
            P["cls_main"][c] += s
    res["n"] = sum(res["ph"][p]["n"] for p in PHASES)
    # the main thread's states over each phase (stalls.py, STALLS=1)
    res["stalls"] = {}
    if (d / "stalls.rec").exists():
        try:
            import stalls
            st = stalls.analyze(d)
        except (OSError, ValueError, KeyError, IndexError):
            st = None
        if st and st.get("load"):
            res["stalls"]["load"] = st["load"][1]
        if st and st.get("warmup"):
            res["stalls"]["warmup"] = st["warmup"][2]
    return res


def wall(res, ph):
    le, se = res["le"], res["se"]
    if ph == "load":
        return le
    return se - le if le is not None and se is not None else None


def disk_wait(res, ph):
    """the main thread's seconds waiting for the disk (stalls.rec: disk, io, swap-ins), or None"""
    st = res["stalls"].get(ph)
    if not st:
        return None
    return sum(st.get(c, 0) for c in ("disk", "io", "swap_zram", "swap_vswap"))


def levers(res):
    """[(lever, load main s, warm-up main s, all threads s (both phases), note)] largest first"""
    out = []
    L, W = res["ph"]["load"], res["ph"]["warmup"]
    for g in GROUPS:
        m0, m1 = L["grp_main"][g], W["grp_main"][g]
        a = L["grp"][g] + W["grp"][g]
        if a < 0.005:
            continue
        out.append((g, m0, m1, a, GROUPS[g][1]))
    out.sort(key=lambda x: -(x[1] + x[2] + 0.001 * x[3]))
    return out


def fmt(x, f="%.2f"):
    return "-" if x is None else f % x


def show(d, top=4):
    res = analyze(d)
    print("== %s" % d)
    if res is None:
        print("   no start-up data (PERF_LOAD=1 makes it; perf-post.sh writes perf-load-stacks.tsv)")
        return
    b = res["b"]
    sc = ""
    if b.get("shadercache"):
        k = (b.get("shadercache_kb") or "").split()
        sc = ", shader cache %s (%s -> %s KiB)" % (b["shadercache"], k[0] if k else "?", k[1] if len(k) > 1 else "?")
    print("   %s %s: load %s s, warm-up to %s s%s; the game's perf samples %d, perf's own CPU %s s, ack %s" % (
        b.get("game", "?"), b.get("variant", "?"), fmt(res["le"]), fmt(res["se"]), sc, res["n"],
        b.get("perf_load_cpu", "-"), b.get("perf_load_ack", "-")))
    if not b.get("mono_t0") and res["n"]:
        print("   (no mono_t0: the phases split at the first sample + load_end)")
    # the phases side by side
    print("   %-8s %7s %8s %8s %6s %7s %9s %8s %7s %7s %8s" % (
        "phase", "wall", "mainCPU", "allCPU", "Xorg", "kswapd", "diskwait", "readMB", "busy_s", "majflt", "swapin"))
    for ph in PHASES:
        P, io = res["ph"][ph], res["io"].get(ph, {})
        print("   %-8s %7s %8s %8s %6s %7s %9s %8s %7s %7s %8s" % (
            ph, fmt(wall(res, ph), "%.1f"), fmt(P["cpu_main"]), fmt(P["cpu"]), fmt(P["others"].get("xorg", 0)),
            fmt(P["others"].get("kswapd0", 0)), fmt(disk_wait(res, ph)), fmt(io.get("hdd_mb"), "%.1f"),
            fmt(io.get("hdd_busy_s"), "%.1f"), fmt(io.get("stat_majflt") if "stat_majflt" in io else io.get("pgmajfault"), "%d"),
            fmt((io.get("pswpin") if io else None), "%d")))
    ld = res["ld"]
    if ld and "startup" in ld:
        u = "s" if ld.get("hz") else "ticks"
        sv = (lambda k: fmt(ld.get(k + "_s"), "%.3f")) if ld.get("hz") else (lambda k: str(ld.get(k, "-")))
        print("   ld.so (LD_DEBUG, %s, the game's own exec of %d): start-up %s %s (relocation %s, loading objects %s), "
              "%s relocations (%s relative, %s from cache), %s at exit with dlopen's" % (
                  ld.get("file"), ld.get("execs", 1), sv("startup"), u, sv("reloc_time"), sv("load_time"),
                  ld.get("relocs", "-"), ld.get("relative", "-"), ld.get("cached", "-"), ld.get("final_relocs", "-")))
    for ph in PHASES:
        P, io = res["ph"][ph], res["io"].get(ph, {})
        w = wall(res, ph)
        print("   -- %s (%s s)" % (ph, fmt(w, "%.1f")))
        st = res["stalls"].get(ph)
        if st:
            tot = sum(st.values()) or 1
            print("      main thread (stalls.rec): " + "  ".join(
                "%s %.2f" % (c, v) for c, v in sorted(st.items(), key=lambda kv: -kv[1]) if v >= 0.01) +
                "  (%.0f%% on the CPU)" % (100 * (st.get("cpu", 0) + st.get("runq", 0)) / tot))
        elif w and P["cpu_main"]:
            print("      main thread: %.2f s of CPU, %.2f s off it (no stalls.rec: STALLS=1 splits that)" % (
                P["cpu_main"], max(0.0, w - P["cpu_main"])))
        if io:
            print("      I/O: disk %s reads %s MB (busy %s s), zram %s reads, VRAM swap %s reads; the game: majflt %s, "
                  "minflt %s, read() %s MB (%s calls), from storage %s MB; direct scan %s, kswapd scan %s, allocstall %s" % (
                      fmt(io.get("hdd_reads"), "%d"), fmt(io.get("hdd_mb"), "%.1f"), fmt(io.get("hdd_busy_s"), "%.1f"),
                      fmt(io.get("zram_reads"), "%d"), fmt(io.get("vswap_reads"), "%d"), fmt(io.get("stat_majflt"), "%d"),
                      fmt(io.get("stat_minflt"), "%d"), fmt(io.get("io_rchar", 0) / 1e6 if "io_rchar" in io else None, "%.1f"),
                      fmt(io.get("io_syscr"), "%d"),
                      fmt(io.get("io_read_bytes", 0) / 1e6 if "io_read_bytes" in io else None, "%.1f"),
                      fmt(io.get("direct_scan"), "%d"), fmt(io.get("kswapd_scan"), "%d"), fmt(io.get("allocstall"), "%d")))
        if not P["n"]:
            continue
        print("      CPU by group (s: all threads / main thread; top functions, all threads):")
        for g, s in P["grp"].most_common():
            if s < 0.005:
                continue
            tops = ", ".join("%s %.2f" % (f if len(f) < 40 else f[:37] + "...", v) for (db, f), v in
                             P["top"][g].most_common(top))
            print("        %-14s %6.2f %6.2f  %s" % (g, s, P["grp_main"][g], tops))
        print("      by the leaf's DSO class (s, all threads): " + "  ".join(
            "%s %.2f" % kv for kv in P["cls"].most_common() if kv[1] >= 0.005))
        th = P["threads"].most_common(5)
        print("      threads (s): " + ", ".join("%s/%d %.2f" % (c, t, v) for (t, c), v in th))
        if P["grp"]["jpeg"] >= 0.05:
            print("      jpeg: %.2f s, %.2f s in functions named *altivec* (libjpeg-turbo's SIMD; 0 without its "
                  "symbols: install libjpeg62-turbo-dbgsym, or compare with JSIMD_FORCENONE=1)" % (P["grp"]["jpeg"], P["altivec"]))
    lv = levers(res)
    if lv:
        print("   levers over the start-up span (main thread: the critical path, s; at most that much wall time,"
              " half of it at 2x the speed):")
        print("      %-14s %8s %8s %8s %8s  %s" % ("group", "loadMain", "warmMain", "allCPU", "2x:load", "lever"))
        for g, m0, m1, a, note in lv[:16]:
            print("      %-14s %8.2f %8.2f %8.2f %8.2f  %s" % (g, m0, m1, a, m0 / 2, note))
    for ph in PHASES:
        dw, io = disk_wait(res, ph), res["io"].get(ph, {})
        if dw is None or not io.get("hdd_mb"):
            continue
        seq = io["hdd_mb"] / DISK_MBS
        print("   disk, %s: the main thread waited %.2f s for %.1f MB; read sequentially at %g MB/s that is %.2f s: "
              "%.2f s to win by reading ahead in big blocks, %.2f s with the files already in RAM (GAMEPREFETCH)" % (
                  ph, dw, io["hdd_mb"], DISK_MBS, seq, max(0.0, dw - seq), dw))


def summary(d):
    """report.py's lp_* columns ({} without start-up data)"""
    try:
        res = analyze(d)
    except (OSError, ValueError, KeyError, IndexError):
        return {}
    if not res:
        return {}
    L, W = res["ph"]["load"], res["ph"]["warmup"]
    out = {}
    if res["n"]:
        both = L["grp"] + W["grp"]
        out.update(lp_cpu=round(L["cpu"], 2), lp_main=round(L["cpu_main"], 2), lp_wcpu=round(W["cpu"], 2),
                   lp_link=round(both["link"], 2), lp_glsl=round(both["shader.glsl"], 2),
                   lp_shbe=round(both["shader.backend"], 2), lp_shcache=round(both["shader.cache"], 2),
                   lp_wshader=round(sum(W["grp"][g] for g in SHADER), 2),
                   lp_tex=round(both["tex"], 2), lp_mip=round(both["mipmap"], 2), lp_dxt=round(both["dxt"], 2),
                   lp_decomp=round(sum(both[g] for g in DECOMP), 2))
    for ph, k in (("load", "lp_mb"), ("warmup", "lp_wmb")):
        io = res["io"].get(ph, {})
        if "hdd_mb" in io:
            out[k] = round(io["hdd_mb"], 1)
    io = res["io"].get("load", {})
    if "stat_majflt" in io:
        out["lp_majflt"] = io["stat_majflt"]
    if res["ld"] and res["ld"].get("startup_s") is not None:
        out["lp_ld"] = round(res["ld"]["startup_s"], 3)
    dw = disk_wait(res, "load")
    if dw is not None:
        out["lp_disk"] = round(dw, 2)
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for a in sys.argv[1:]:
        show(a)
