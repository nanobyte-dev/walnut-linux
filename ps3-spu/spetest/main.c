#include <stdio.h>
#include "runner.h"

int main(int argc, char **argv)
{
	return spt_main(argc, argv, &spt_be_spufs, stdout);
}
