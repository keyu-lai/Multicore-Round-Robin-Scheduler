#include "sched.h"
#include <linux/interrupt.h>

#define WEIGHT_FG	10
#define WEIGHT_BG 	1
#define BASE_TICKS 	1

#ifdef CONFIG_SMP
static int
select_task_rq_wrr(struct task_struct *p, int sd_flag, int flags)
{
	int i, lowest_weight = INT_MAX, lightest_load_cpu = -1;
	struct rq *this_cpu_rq;
	struct wrr_rq *this_cpu_wrr;

	/* I don't like the fact that I have to grab every lock here,
	 	so I wonder if there's a better way ... rt and cfs use rcu_read_lock
	 	can we use that here? */

	/* locking rule: when acquiring multiple wrr_rq locks,
		acquire them in cpu_id order */
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

	if (lightest_load_cpu == -1)
		return task_cpu(p);

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

static struct task_struct *pick_next_task_wrr(struct rq *rq)
{
	struct list_head *queue = &rq->wrr.queue;
	struct sched_wrr_entity *sched_ent;

	raw_spin_lock(&rq->wrr.lock); /* the locking is mainly for later, part 2 */
	if (list_empty(queue)) {
		raw_spin_unlock(&rq->wrr.lock);
		return NULL;
	}
	sched_ent = list_first_entry(queue, struct sched_wrr_entity, run_list);
	raw_spin_unlock(&rq->wrr.lock);	
	return wrr_task_of(sched_ent);
}

static void
enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{	
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *rq_wrr = &rq->wrr;

	rq_wrr->enqueues++;
	wrr_se->time_slice = wrr_se->weight * BASE_TICKS;
	raw_spin_lock(&rq_wrr->lock); /* the locking is mainly for later, part 2 */
	rq_wrr->total_weight += wrr_se->weight;
	list_add_tail(&wrr_se->run_list, &rq_wrr->queue);
	raw_spin_unlock(&rq_wrr->lock);
}

static void
dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *rq_wrr = &rq->wrr;
	rq_wrr->dequeues++;

	/* the locking is mainly for later, part 2 */
	raw_spin_lock(&rq_wrr->lock); 
	rq_wrr->total_weight -= wrr_se->weight;
	wrr_se->time_slice = 0;
	list_del(&wrr_se->run_list);
	raw_spin_unlock(&rq_wrr->lock);	
}

static void put_prev_task_wrr(struct rq *rq, struct task_struct *prev)
{
}

static void task_tick_wrr(struct rq *rq, struct task_struct *curr, int queued)
{
	struct sched_wrr_entity *wrr_se = &curr->wrr;

	wrr_se->time_slice--; 
	if (wrr_se->time_slice <= 0) {
		dequeue_task_wrr(rq, curr, 0);
		enqueue_task_wrr(rq, curr, 0);
		set_tsk_need_resched(curr);
	}
}

static void set_curr_task_wrr(struct rq *rq)
{
}

static bool is_foreground(struct task_struct *p)
{
	// TODO: actually implement this as with debug.c:96
	return true;
}

static void switched_to_wrr(struct rq *rq, struct task_struct *p)
{
	p->wrr.rq = &rq->wrr;
	p->wrr.weight = (is_foreground(p) ? WEIGHT_FG : WEIGHT_BG);
	p->wrr.time_slice = p->wrr.weight;
	//INIT_LIST_HEAD(&p->wrr.run_list);
}

static void switched_from_wrr(struct rq *rq, struct task_struct *p)
{
	// TODO: implement this?
}

static void
prio_changed_wrr(struct rq *rq, struct task_struct *p, int oldprio)
{
}

static unsigned int get_rr_interval_wrr(struct rq *rq, struct task_struct *task)
{
	// TODO: implement this?
	return 0;
}

/* ------------------------ Load Balance ------------------------ */
static void move_task(struct task_struct *p, struct rq *from_rq, 
		      struct rq *to_rq) {
	deactivate_task(from_rq, p, 0);
	set_task_cpu(p, to_rq->cpu);
	activate_task(to_rq, p, 0);
	// Not sure if we need this
	// check_preempt_curr(to_rq->cpu, p, 0);
}

static int can_migrate_task_wrr(struct task_struct *p, struct rq *from_rq,
								struct rq *to_rq) {
	/* Maybe we need to add more */
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
		if (cpu == 0) {
			// printk("Load Balancing: cpu %d!!\n", cpu);
			raise_softirq(SCHED_SOFTIRQ_WRR);
		}
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

	rcu_read_lock();

	for_each_possible_cpu(tmp_cpu) {
		tmp_weight = (cpu_rq(tmp_cpu)->wrr).total_weight;
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

	printk("Loading balance: %d %d %d %d!!", min_weight, submin_weight, submax_weight, max_weight);
	printk("max: %d; min: %d\n", max_cpu, min_cpu);

	if (cpu_cnt <= 1)
		goto unlock;
	if (max_weight <= min_weight)
		goto unlock;

	from_rq = cpu_rq(max_cpu);
	to_rq = cpu_rq(min_cpu);

	// double_rq_lock(from_rq, to_rq);
	list_for_each_entry(se_wrr, &from_rq->wrr.queue, run_list) {
		p = wrr_task_of(se_wrr);
		/* The move should not cause the imbalance to reverse */
		if (max_weight - p->wrr.weight < submin_weight &&
		    min_weight + p->wrr.weight > submax_weight)
			continue;
		if (!can_migrate_task_wrr(p, from_rq, to_rq))
			continue;
		printk("Balaning: %d to %d\n", max_cpu, min_cpu);
		move_task(p, from_rq, to_rq);
		break;
	}
	// double_rq_unlock(from_rq, to_rq);

unlock:	
	rcu_read_unlock();
}

__init void init_sched_wrr_class(void)
{
#ifdef CONFIG_SMP
	open_softirq(SCHED_SOFTIRQ_WRR, load_balance_wrr);
#endif
}

/* ------------------------ Load Balance ------------------------ */

const struct sched_class wrr_sched_class = {
	.next 				= &fair_sched_class,
	.enqueue_task		= enqueue_task_wrr,
	.dequeue_task		= dequeue_task_wrr,
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