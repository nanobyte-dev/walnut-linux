/* spetest-sim: the runner on the SPU simulator (make test) */
#include <stdio.h>
#include "../runner.h"

extern const struct spt_be spt_be_sim;

int main(int argc, char **argv)
{
	return spt_main(argc, argv, &spt_be_sim, stdout);
}
