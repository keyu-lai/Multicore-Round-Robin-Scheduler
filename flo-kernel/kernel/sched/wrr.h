#ifndef __LINUX_WRR_H
#define __LINUX_WRR_H

#define MAX_CPUS 4

#define BASE_TICKS 1

#define FLAG_YIELD 999999
#define FLAG_TICK 777777

extern int WEIGHT_FG;
extern int WEIGHT_BG;

struct wrr_info {

	int num_cpus;
	int nr_running[MAX_CPUS];
	int total_weight[MAX_CPUS];
};

#endif

