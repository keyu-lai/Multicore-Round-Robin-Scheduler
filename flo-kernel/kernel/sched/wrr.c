#include "sched.h"

#define WEIGHT_FG	10
#define WEIGHT_BG 	1
#define BASE_TICKS 	1

#ifdef CONFIG_SMP
static int
select_task_rq_wrr(struct task_struct *p, int sd_flag, int flags)
{
	/* we might need to change this when load balancing? */
	/* i'm not sure yet when this function gets called. */
	return task_cpu(p);
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

	if (list_empty(queue))
		return NULL;

	sched_ent = list_first_entry(queue, struct sched_wrr_entity, run_list);
	return wrr_task_of(sched_ent);
}

static void
enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{	
	struct sched_wrr_entity *wrr_se = &p->wrr;

	wrr_se->time_slice = wrr_se->weight * BASE_TICKS;
	raw_spin_lock(&wrr_se->rq->lock); /* the locking is mainly for later, part 2 */
	list_add_tail(&wrr_se->run_list, &rq->wrr.queue);
	raw_spin_unlock(&wrr_se->rq->lock);
}

static void
dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
}

static void put_prev_task_wrr(struct rq *rq, struct task_struct *prev)
{
}

static void task_tick_wrr(struct rq *rq, struct task_struct *curr, int queued)
{
	curr->wrr.time_slice--; 
	if (curr->wrr.time_slice == 0) {
		raw_spin_lock(&task_rq(curr)->lock);
		resched_task(curr);
		raw_spin_unlock(&task_rq(curr)->lock);
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
	/* i think this is all we need to do when switching a task to use WRR */
	p->wrr.rq = &rq->wrr;
	p->wrr.weight = (is_foreground(p) ? WEIGHT_FG : WEIGHT_BG);
	p->wrr.time_slice = 0;
	INIT_LIST_HEAD(&p->wrr.run_list);
}

static void
prio_changed_wrr(struct rq *rq, struct task_struct *p, int oldprio)
{
	BUG();
}

static unsigned int get_rr_interval_wrr(struct rq *rq, struct task_struct *task)
{
	// TODO: implement this
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
#endif
	.set_curr_task      = set_curr_task_wrr,
	.task_tick			= task_tick_wrr,
	.get_rr_interval	= get_rr_interval_wrr,
	.prio_changed		= prio_changed_wrr,
	.switched_to		= switched_to_wrr,
};