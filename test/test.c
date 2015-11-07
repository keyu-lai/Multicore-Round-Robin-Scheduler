#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <errno.h>
#include <string.h>

#define SCHED_WRR 	6

int main (void)
{
	pid_t pid;
	int rc, i;
	struct sched_param param;

	/*fork();
	fork();*/
	pid = getpid();

	param.sched_priority = 0;
	rc = sched_setscheduler(0, SCHED_WRR, &param);
	printf("rc = %d %s\n", rc, strerror(errno));
	fflush(stdout);
	while (1) {
		printf("i'm running! %d\n", pid);
		fflush(stdout);
		for (i = 0; i < 10000000; i++);
		/*printf("i'm going to sleep! %d\n", pid);
		fflush(stdout);
		sleep(1);*/
	}
	return 0;
}
