#include "sched.h"
#include <linux/limits.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include "wrr.h"

int WEIGHT_FG = 10;
int WEIGHT_BG = 1;

#ifdef CONFIG_SMP
static int
select_task_rq_wrr(struct task_struct *p, int sd_flag, int flags)
{
	int i, lowest_weight = INT_MAX, lightest_load_cpu = -1;
	struct rq *this_cpu_rq;
	struct wrr_rq *this_cpu_wrr;

	for_each_possible_cpu(i) {
		this_cpu_rq = cpu_rq(i);
		raw_spin_lock(&this_cpu_rq->lock);
		this_cpu_wrr = &this_cpu_rq->wrr;
		if (this_cpu_wrr->total_weight < lowest_weight) {
			lightest_load_cpu = i;
			lowest_weight = this_cpu_wrr->total_weight;
		}
		raw_spin_unlock(&this_cpu_rq->lock);
	}

	return lightest_load_cpu;
}
#endif /* CONFIG_SMP */

static void
check_preempt_curr_wrr(struct rq *rq, struct task_struct *p, int flags)
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
	rq_wrr->total_weight += wrr_se->weight;
	rq_wrr->nr_running++;
	list_add_tail(&wrr_se->run_list, &rq_wrr->queue);

}

/* dequeue's many callers all hold the rq lock */
static void
dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *rq_wrr = &rq->wrr;

	rq_wrr->total_weight -= wrr_se->weight;
	rq_wrr->nr_running--;
	wrr_se->time_slice = 0;
	list_del(&wrr_se->run_list);
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
	return is_foreground(p) ? WEIGHT_FG : WEIGHT_BG;
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

	if (new_weight != wrr_se->weight)
		wrr_se->weight = new_weight;
}

static unsigned int get_rr_interval_wrr(struct rq *rq, struct task_struct *task)
{
	return 0;
}

/* ------------------------ Periodic load balancing ------------------------ */
static void move_task(struct task_struct *p, struct rq *from_rq,
			  struct rq *to_rq) {
	deactivate_task(from_rq, p, 0);
	set_task_cpu(p, to_rq->cpu);
	activate_task(to_rq, p, 0);
}

static int can_migrate_task_wrr(struct task_struct *p, struct rq *from_rq,
				struct rq *to_rq) {
	if (task_rq(p) == to_rq)
		return 0;
	if (!cpumask_test_cpu(to_rq->cpu, tsk_cpus_allowed(p)))
		return 0;
	if (task_running(from_rq, p))
		return 0;
	if (!cpu_online(to_rq->cpu))
		return 0;
	return 1;
}

void trigger_load_balance_wrr(struct rq *rq, int cpu)
{
	if (time_after_eq(jiffies, rq->next_balance_wrr)) {
		rq->next_balance_wrr = jiffies + HZ/2;
		if (cpu == 0)
			raise_softirq(SCHED_SOFTIRQ_WRR);
	}
}

static void load_balance_wrr(struct softirq_action *h)
{
	int tmp_cpu, max_cpu = 0, min_cpu = 0;
	int cpu_cnt = 0;
	unsigned int tmp_weight, max_weight = 0, min_weight = INT_MAX;
	unsigned int submax_weight = 0;
	unsigned int submin_weight = INT_MAX;
	struct rq *from_rq;
	struct rq *to_rq;
	struct sched_wrr_entity *se_wrr;
	struct task_struct *p;
	unsigned long flags;
	struct rq *tmp_rq;

	for_each_possible_cpu(tmp_cpu) {
		tmp_rq = cpu_rq(tmp_cpu);
		raw_spin_lock(&tmp_rq->lock);
		tmp_weight = (tmp_rq->wrr).total_weight;
		raw_spin_unlock(&tmp_rq->lock);
		if (tmp_weight > max_weight) {
			submax_weight = max_weight;
			max_cpu = tmp_cpu;
			max_weight = tmp_weight;
		} else if (tmp_weight > submax_weight)
			submax_weight = tmp_weight;
		if (tmp_weight < min_weight) {
			submin_weight = min_weight;
			min_cpu = tmp_cpu;
			min_weight = tmp_weight;
		} else if (tmp_weight < submin_weight)
			submin_weight = tmp_weight;
		cpu_cnt++;
	}

	if (cpu_cnt <= 1)
		return;
	if (max_weight <= min_weight)
		return;

	from_rq = cpu_rq(max_cpu);
	to_rq = cpu_rq(min_cpu);

	local_irq_save(flags);
	double_rq_lock(to_rq, from_rq);
	list_for_each_entry(se_wrr, &from_rq->wrr.queue, run_list) {
		p = wrr_task_of(se_wrr);
		/* The move should not cause the imbalance to reverse */
		if (max_weight - p->wrr.weight < submin_weight &&
			min_weight + p->wrr.weight > submax_weight)
			continue;
		if (!can_migrate_task_wrr(p, from_rq, to_rq))
			continue;
		move_task(p, from_rq, to_rq);
		break;
	}
	double_rq_unlock(to_rq, from_rq);
	local_irq_restore(flags);
}

__init void init_sched_wrr_class(void)
{
#ifdef CONFIG_SMP
	open_softirq(SCHED_SOFTIRQ_WRR, load_balance_wrr);
#endif
}

/* ------------------------ Periodic load balancing ------------------------ */

/* ------------------------ Idle load balancing ------------------------ */
static int idle_load_balance_wrr(struct rq *idle_rq)
{
	int i;
	unsigned int busiest_weight = 0;
	int busiest_cpu = -1;
	int pulled_tasks = 0;
	int cpu_cnt = 0;
	struct rq *busiest_rq = NULL;
	struct rq *tmp_rq;
	struct wrr_rq *tmp_wrr;
	struct task_struct *p;
	unsigned long flags;
	unsigned int tmp_weight = 0;
	struct sched_wrr_entity *wrr_se;

	raw_spin_unlock(&idle_rq->lock);
	for_each_possible_cpu(i) {
		tmp_rq = cpu_rq(i);

		if (tmp_rq == idle_rq)
			continue;

		raw_spin_lock(&tmp_rq->lock);
		tmp_wrr = &tmp_rq->wrr;
		tmp_weight = tmp_wrr->total_weight;
		raw_spin_unlock(&tmp_rq->lock);

		/* idle_rq already locked so need to lock */
		if (tmp_weight > busiest_weight) {
			busiest_cpu = i;
			busiest_weight = tmp_wrr->total_weight;
		}

		cpu_cnt++;
	}
	raw_spin_lock(&idle_rq->lock);

	busiest_rq = cpu_rq(busiest_cpu);
	if (cpu_cnt <= 0 || busiest_cpu < 0 || busiest_rq == idle_rq ||
		(&idle_rq->wrr)->total_weight != 0)
		return 0;

	local_irq_save(flags);
	double_lock_balance(idle_rq, busiest_rq);

	list_for_each_entry(wrr_se, &busiest_rq->wrr.queue, run_list) {
		p = wrr_task_of(wrr_se);
		if (!can_migrate_task_wrr(p, busiest_rq, idle_rq))
			continue;

		move_task(p, busiest_rq, idle_rq);
		pulled_tasks++;
		break;
	}

	double_unlock_balance(idle_rq, busiest_rq);
	local_irq_restore(flags);

	return pulled_tasks;
}

void idle_balance_wrr(int this_cpu, struct rq *this_rq)
{
	struct sched_domain *sd;
	struct wrr_rq *cur_wrr;
	int pulled_task = 0;

	this_rq->idle_stamp = this_rq->clock;

	rcu_read_lock();
	for_each_domain(this_cpu, sd) {
		cur_wrr = &this_rq->wrr;
		if (cur_wrr->total_weight == 0)
			pulled_task = idle_load_balance_wrr(this_rq);

		if (pulled_task) {
			this_rq->idle_stamp = 0;
			break;
		}
	}
	rcu_read_unlock();
}
/* ------------------------ Idle load balancing ------------------------ */

const struct sched_class wrr_sched_class = {
	.next				= &fair_sched_class,
	.enqueue_task		= enqueue_task_wrr,
	.dequeue_task		= dequeue_task_wrr,
	.yield_task			= yield_task_wrr,
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
