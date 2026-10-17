#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "names.h"
static uint64_t frames[1<<20]; static int nframes;
#define NN (sizeof(glc_names)/sizeof(glc_names[0]))
extern void *glc_real[];
extern uint64_t glc_idx;
extern uint64_t glc_log[];

static uint32_t (*real_ticks)(void);
static int fake; static uint32_t fakebase; static int fakeframe0;
static uint64_t marks[256]; static int markframe[256]; static char markname[256][64]; static int nmarks;
static FILE *(*real_fopen)(const char *, const char *);
FILE *fopen64(const char *p, const char *m)
{
    if(!real_fopen) real_fopen = dlsym(RTLD_NEXT, "fopen64");
    const char *g = strstr(p, "glc_mark_");
    if(g && nmarks < 256) { strncpy(markname[nmarks], g+9, 63); marks[nmarks] = glc_idx; markframe[nmarks++] = nframes;
        if(!fake && getenv("GLC_FAKECLOCK")) { fake = 1; fakebase = real_ticks(); fakeframe0 = nframes; } }
    return real_fopen(p, m);
}
uint32_t SDL_GetTicks(void) { if(!real_ticks) real_ticks = dlsym(RTLD_NEXT, "SDL_GetTicks"); return fake ? fakebase + (uint32_t)(nframes - fakeframe0) * 33 : real_ticks(); }
static void *(*real_gpa)(const char *);
static void (*real_swap)(void *);
__attribute__((constructor)) static void init(void)
{
    for(size_t i = 0; i < NN; i++) glc_real[i] = dlsym(RTLD_NEXT, glc_names[i]);
    real_gpa = dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress");
    real_ticks = dlsym(RTLD_NEXT, "SDL_GetTicks"); real_swap = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
}
void *SDL_GL_GetProcAddress(const char *name)
{
    size_t lo = 0, hi = NN;
    while(lo < hi) { size_t m = (lo+hi)/2; int c = strcmp(glc_names[m], name); if(!c) { if(!glc_real[m]) glc_real[m] = real_gpa(name); return glc_stubs[m]; } if(c < 0) lo = m+1; else hi = m; }
    return real_gpa(name);
}
void SDL_GL_SwapWindow(void *w)
{
    if(nframes < (1<<20)) frames[nframes++] = glc_idx;
    real_swap(w);
}
__attribute__((destructor)) static void fini(void)
{
    const char *o = getenv("GLC_OUT"); if(!o) return;
    char p[1024];
    snprintf(p, sizeof p, "%s.log", o); FILE *f = fopen(p, "wb"); fwrite(glc_log, 8, glc_idx, f); fclose(f);
    snprintf(p, sizeof p, "%s.frames", o); f = fopen(p, "w"); for(int i = 0; i < nframes; i++) fprintf(f, "%llu\n", (unsigned long long)frames[i]); fclose(f);
    snprintf(p, sizeof p, "%s.marks", o); f = fopen(p, "w"); for(int i = 0; i < nmarks; i++) fprintf(f, "%s %llu %d\n", markname[i], (unsigned long long)marks[i], markframe[i]); fclose(f);
    snprintf(p, sizeof p, "%s.names", o); f = fopen(p, "w"); for(size_t i = 0; i < NN; i++) fprintf(f, "%s\n", glc_names[i]); fclose(f);
    snprintf(p, sizeof p, "%s.maps", o); f = fopen(p, "w"); FILE *m = fopen("/proc/self/maps", "r"); char l[512]; while(fgets(l, sizeof l, m)) fputs(l, f); fclose(m); fclose(f);
}
