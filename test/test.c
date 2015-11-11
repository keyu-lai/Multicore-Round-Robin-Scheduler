#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>
#include "test.h"

int main(int argc, char *argv[])
{
	int fg_weight;
	int bg_weight;
	int i;
	struct wrr_info buf;

	if (argc == 3) {
		fg_weight = atoi(argv[1]);
		bg_weight = atoi(argv[2]);
		if (syscall(379, fg_weight, bg_weight)) {
			printf("error: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	while (1) {
		if (syscall(378, &buf)) {
			printf("error: %s\n", strerror(errno));
			exit(EXIT_FAILURE);		
		}
		for (i = 0; i < buf.num_cpus; i++) {
			printf("cpu%d: %d, %d\n", i, buf.nr_running[i],
				buf.total_weight[i]);
		}
		usleep(300000);
	}
	return 0;
}
