#include "sched.h"

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
	for_each_possible_cpu(i) {
		this_cpu_rq = cpu_rq(i);
		this_cpu_wrr = &this_cpu_rq->wrr;
		raw_spin_lock(&this_cpu_wrr->lock);
		if (this_cpu_wrr->total_weight < lowest_weight) {
			lightest_load_cpu = i;
			lowest_weight = this_cpu_wrr->total_weight;
		}
	}

	for (i = num_possible_cpus() - 1; i >= 0; i--) {
		this_cpu_rq = cpu_rq(i);
		this_cpu_wrr = &this_cpu_rq->wrr;
		raw_spin_unlock(&this_cpu_wrr->lock);
	}
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