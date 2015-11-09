#ifndef __LINUX_WRR_H
#define __LINUX_WRR_H

#define MAX_CPUS 4

struct wrr_info {

	int num_cpus;
	int nr_running[MAX_CPUS];
	int total_weight[MAX_CPUS];
};

#endif

