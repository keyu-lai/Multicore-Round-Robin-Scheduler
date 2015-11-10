#include "sched.h"
#include <linux/limits.h>

#define WEIGHT_FG	10
#define WEIGHT_BG 	1
#define BASE_TICKS 	1

#define FLAG_YIELD	999999
#define FLAG_TICK	777777

#ifdef CONFIG_SMP
static int
select_task_rq_wrr(struct task_struct *p, int sd_flag, int flags)
{
	int i, lowest_weight = INT_MAX, lightest_load_cpu = -1;
	struct rq *this_cpu_rq;
	struct wrr_rq *this_cpu_wrr;

	rcu_read_lock();
	for_each_possible_cpu(i) {
		this_cpu_rq = cpu_rq(i);
		this_cpu_wrr = &this_cpu_rq->wrr;
		if (this_cpu_wrr->total_weight < lowest_weight) {
			lightest_load_cpu = i;
			lowest_weight = this_cpu_wrr->total_weight; 
		}
	}
	rcu_read_unlock();

	return lightest_load_cpu;
}
#endif /* CONFIG_SMP */

static void check_preempt_curr_wrr(struct rq *rq, struct task_struct *p, int flags)
{
}

static struct task_struct *wrr_task_of(struct sched_wrr_entity *wrr_se)
{
	return container_of(wrr_se, struct task_struct, wrr);
}

/* pick_next_task()'s callers all hold the rq lock */
static struct task_struct *pick_next_task_wrr(struct rq *rq)
{
	struct list_head *queue = &rq->wrr.queue;
	struct sched_wrr_entity *sched_ent;

	if (list_empty(queue)) 
		return NULL;
	sched_ent = list_first_entry(queue, struct sched_wrr_entity, run_list);
	return wrr_task_of(sched_ent);
}

/* enqueue's many callers all hold the rq lock */
static void
enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{	
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *rq_wrr = &rq->wrr;

	wrr_se->time_slice = wrr_se->weight * BASE_TICKS;
	//trace_printk("enqueuing - flag %d - previous total weight: %d\ttasks: %d\n", flags, rq_wrr->total_weight, rq_wrr->nr_running);
	rq_wrr->total_weight += wrr_se->weight;
	rq_wrr->nr_running++;
	//trace_printk("enqueuing - flag %d -           task weight: %d\n", flags, wrr_se->weight);
	list_add_tail(&wrr_se->run_list, &rq_wrr->queue);
	//trace_printk("enqueuing - flag %d -      new total weight: %d\ttasks: %d\n", flags, rq_wrr->total_weight, rq_wrr->nr_running);
}

/* dequeue's many callers all hold the rq lock */
static void
dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *rq_wrr = &rq->wrr;

	//trace_printk("dequeuing - flag %d - previous total weight: %d\ttasks: %d\n", flags, rq_wrr->total_weight, rq_wrr->nr_running);
	rq_wrr->total_weight -= wrr_se->weight;
	rq_wrr->nr_running--;
	//trace_printk("dequeuing - flag %d -           task weight: %d\n", flags, wrr_se->weight);
	wrr_se->time_slice = 0;
	list_del(&wrr_se->run_list);
	//trace_printk("dequeuing - flag %d -      new total weight: %d\ttasks: %d\n", flags, rq_wrr->total_weight, rq_wrr->nr_running);
}

/* sys_sched_yield(), which calls this, holds the rq lock */
static void yield_task_wrr(struct rq *rq)
{
	dequeue_task_wrr(rq, current, FLAG_YIELD);
	enqueue_task_wrr(rq, current, FLAG_YIELD);
	/* schedule() is about to be called by sys_sched_yield() */
}

static void put_prev_task_wrr(struct rq *rq, struct task_struct *prev)
{
}

/* scheduler_tick() holds the rq lock */
static void task_tick_wrr(struct rq *rq, struct task_struct *curr, int queued)
{
	struct sched_wrr_entity *wrr_se = &curr->wrr;

	wrr_se->time_slice--; 
	if (wrr_se->time_slice <= 0) {
		dequeue_task_wrr(rq, curr, FLAG_TICK);
		enqueue_task_wrr(rq, curr, FLAG_TICK);
		set_tsk_need_resched(curr);
	}
}

static void set_curr_task_wrr(struct rq *rq)
{
}

/* this function copied from debug.c */
static char *task_group_path(struct task_group *tg, char *buf, size_t buflen)
{
	if (autogroup_path(tg, buf, buflen))
		return buf;

	if (!tg->css.cgroup) {
		buf[0] = '\0';
		return buf;
	}
	cgroup_path(tg->css.cgroup, buf, buflen);
	return buf;
}

static char group_path[PATH_MAX];
static bool is_foreground(struct task_struct *p)
{
	struct task_group *tg = task_group(p);

	task_group_path(tg, group_path, PATH_MAX);
	if (strncmp(group_path, "/bg_non_interactive", 19) == 0)
		return false;
	return true;
}

static unsigned int choose_weight(struct task_struct *p)
{
	return (is_foreground(p) ? WEIGHT_FG : WEIGHT_BG);
}

static void switched_to_wrr(struct rq *rq, struct task_struct *p)
{
	p->wrr.rq = &rq->wrr;
	p->wrr.weight = choose_weight(p);
	p->wrr.time_slice = p->wrr.weight;

}

static void switched_from_wrr(struct rq *rq, struct task_struct *p)
{
}

static void
prio_changed_wrr(struct rq *rq, struct task_struct *p, int oldprio)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	unsigned int new_weight = choose_weight(p);

	if (new_weight != wrr_se->weight /*&& p->on_rq*/) {
		//trace_printk("!!! previous task weight: %d\n", wrr_se->weight);
		wrr_se->weight = new_weight;
		//trace_printk("! now in foreground? %d   on rq? %d\n", is_foreground(p), p->on_rq);
		//trace_printk("!!! new task weight: %d\n", wrr_se->weight);
	}
}

static unsigned int get_rr_interval_wrr(struct rq *rq, struct task_struct *task)
{
	return 0;
}


const struct sched_class wrr_sched_class = {
	.next 				= &fair_sched_class,
	.enqueue_task		= enqueue_task_wrr,
	.dequeue_task		= dequeue_task_wrr,
	.yield_task 		= yield_task_wrr,
	.check_preempt_curr	= check_preempt_curr_wrr,
	.pick_next_task		= pick_next_task_wrr,
	.put_prev_task		= put_prev_task_wrr,
#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_wrr,
	.switched_from		= switched_from_wrr,
#endif
	.set_curr_task      = set_curr_task_wrr,
	.task_tick			= task_tick_wrr,
	.get_rr_interval	= get_rr_interval_wrr,
	.prio_changed		= prio_changed_wrr,
	.switched_to		= switched_to_wrr,
};