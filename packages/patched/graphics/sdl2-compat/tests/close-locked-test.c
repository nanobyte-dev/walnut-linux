/* Close the audio device while holding its lock, through SDL 1.2's or SDL2's API.
   Libraries are dlopen()ed and the few prototypes declared here (the ABI is stable),
   so no SDL headers are needed.  Exit 0 when the close returns; a hang is the deadlock.
   usage: close-locked-test sdl2 /path/libSDL2-2.0.so.0 | sdl12 libSDL-1.2.so.0 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {	/* SDL_AudioSpec, the same layout in 1.2 and 2.0 */
	int freq; unsigned short format; unsigned char channels, silence;
	unsigned short samples, padding; unsigned int size;
	void (*callback)(void *, unsigned char *, int); void *userdata;
} Spec;

static volatile int calls;
static void cb(void *u, unsigned char *s, int n) { (void)u; memset(s, 0, n); calls++; }

static void *sym(void *h, const char *n) {
	void *p = dlsym(h, n);
	if (!p) { fprintf(stderr, "missing symbol %s\n", n); _exit(2); }
	return p;
}

int main(int argc, char **argv) {
	if (argc != 3) { fprintf(stderr, "usage: %s sdl2|sdl12 LIB\n", argv[0]); return 2; }
	void *h = dlopen(argv[2], RTLD_NOW | RTLD_GLOBAL);
	if (!h) { fprintf(stderr, "dlopen %s: %s\n", argv[2], dlerror()); return 2; }
	int (*init)(unsigned) = sym(h, "SDL_Init");
	const char *(*err)(void) = sym(h, "SDL_GetError");
	Spec want = { 44100, 0x8010 /* AUDIO_S16SYS */, 2, 0, 1024, 0, 0, cb, 0 }, have;
	if (init(0x10 /* SDL_INIT_AUDIO */) < 0) { fprintf(stderr, "SDL_Init: %s\n", err()); return 2; }

	if (!strcmp(argv[1], "sdl2")) {
		unsigned (*open)(const char *, int, const Spec *, Spec *, int) = sym(h, "SDL_OpenAudioDevice");
		void (*pause)(unsigned, int) = sym(h, "SDL_PauseAudioDevice");
		void (*lock)(unsigned) = sym(h, "SDL_LockAudioDevice");
		void (*close)(unsigned) = sym(h, "SDL_CloseAudioDevice");
		unsigned dev = open(NULL, 0, &want, &have, 0);
		if (!dev) { fprintf(stderr, "SDL_OpenAudioDevice: %s\n", err()); return 2; }
		pause(dev, 0);
		usleep(200000);	/* the audio thread is running and calling back */
		lock(dev);
		usleep(200000);	/* ...and now waits for the stream lock we hold */
		close(dev);
	} else {
		int (*open)(Spec *, Spec *) = sym(h, "SDL_OpenAudio");
		void (*pause)(int) = sym(h, "SDL_PauseAudio");
		void (*lock)(void) = sym(h, "SDL_LockAudio");
		void (*close)(void) = sym(h, "SDL_CloseAudio");
		if (open(&want, &have) < 0) { fprintf(stderr, "SDL_OpenAudio: %s\n", err()); return 2; }
		pause(0);
		usleep(200000);
		lock();
		usleep(200000);
		close();
	}
	printf("closed while locked (%d callbacks)\n", calls);
	return 0;
}
