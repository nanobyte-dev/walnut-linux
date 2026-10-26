/* vtx-snorm8-stubs: the Mesa utilities nvfx_vertprog.c and the TGSI parser call, for
 * vtx-snorm8-vpsim.c on the host (no Mesa headers here: their real ones need much more) */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
const char *debug_get_option_cached(const char *name, const char *dfault) { const char *v = getenv(name); return v ? v : dfault; }
bool debug_parse_bool_option(const char *str, bool dfault) { if (!str) return dfault; return !strcmp(str, "1") || !strcmp(str, "true") || !strcmp(str, "y"); }
void _debug_vprintf(const char *format, va_list ap) { vfprintf(stderr, format, ap); }
double _mesa_strtod(const char *s, char **end) { return strtod(s, end); }
float _mesa_strtof(const char *s, char **end) { return strtof(s, end); }
const void *util_format_description(int f) { (void)f; return NULL; }
int u_printf_length(const char *f, va_list ap) { return vsnprintf(NULL, 0, f, ap); }
bool tgsi_sanity_check(const void *t) { (void)t; return true; }
