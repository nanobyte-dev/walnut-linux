#ifndef SPT_RUNNER_H
#define SPT_RUNNER_H

#include <stdio.h>
#include "spt.h"

/* The whole program: options from argv, the backend to drive, where the report goes.
 * Returns the exit code: 0 all passed, 1 a failure, 2 usage or setup, 3 a SPE/test never ran,
 * 4 stopped by the temperature limit. */
int spt_main(int argc, char **argv, const struct spt_be *be, FILE *out);

#endif
