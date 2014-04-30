/*
 * driver.c - driver support
 *
 * (C) 2006-2007 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *               Shaohua Li <shaohua.li@intel.com>
 *               Adam Belay <abelay@novell.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/clockchips.h>

#include "cpuidle.h"

DEFINE_SPINLOCK(cpuidle_driver_lock);

#ifdef CONFIG_CPU_IDLE_MULTIPLE_DRIVERS

static DEFINE_PER_CPU(struct cpuidle_driver *, cpuidle_drivers);

static struct cpuidle_driver *__cpuidle_get_cpu_driver(int cpu)
{
	return per_cpu(cpuidle_drivers, cpu);
}

static inline void __cpuidle_unset_driver(struct cpuidle_driver *drv)
{
	int cpu;

	for_each_cpu(cpu, drv->cpumask) {

		if (drv != __cpuidle_get_cpu_driver(cpu))
			continue;

		per_cpu(cpuidle_drivers, cpu) = NULL;
	}
}

static inline int __cpuidle_set_driver(struct cpuidle_driver *drv)
{
	int cpu;

	for_each_cpu(cpu, drv->cpumask) {
		if (__cpuidle_get_cpu_driver(cpu)) {
			__cpuidle_unset_driver(drv);
			return -EBUSY;
		}

		per_cpu(cpuidle_drivers, cpu) = drv;
	}
	return 0;
}

#else

static struct cpuidle_driver *cpuidle_curr_driver;
static inline struct cpuidle_driver *__cpuidle_get_cpu_driver(int cpu)
{
	return cpuidle_curr_driver;
}

static inline int __cpuidle_set_driver(struct cpuidle_driver *drv)
{
	if (cpuidle_curr_driver)
		return -EBUSY;

	cpuidle_curr_driver = drv;

	return 0;
}

static inline void __cpuidle_unset_driver(struct cpuidle_driver *drv)
{
	if (drv == cpuidle_curr_driver)
		cpuidle_curr_driver = NULL;
}

#endif

static void cpuidle_setup_broadcast_timer(void *arg)
{
	int cpu = smp_processor_id();
	clockevents_notify((long)(arg), &cpu);
}

/**
 * __cpuidle_driver_init - initialize the driver's internal data
 * @drv: a valid pointer to a struct cpuidle_driver
 */
static void __cpuidle_driver_init(struct cpuidle_driver *drv)
{
	int i;

	drv->refcnt = 0;

	if (!drv->cpumask)
		drv->cpumask = (struct cpumask *)cpu_possible_mask;

	for (i = drv->state_count - 1; i >= 0 ; i--) {
		if (drv->states[i].flags & CPUIDLE_FLAG_TIMER_STOP) {
			drv->bctimer = 1;
			break;
		}
	}
}

#ifdef CONFIG_ARCH_HAS_CPU_RELAX
static int poll_idle(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	local_irq_enable();
	if (!current_set_polling_and_test()) {
		while (!need_resched())
			cpu_relax();
	}
	current_clr_polling();

	return index;
}

static void poll_idle_init(struct cpuidle_driver *drv)
{
	struct cpuidle_state *state = &drv->states[0];

	snprintf(state->name, CPUIDLE_NAME_LEN, "POLL");
	snprintf(state->desc, CPUIDLE_DESC_LEN, "CPUIDLE CORE POLL IDLE");
	state->exit_latency = 0;
	state->target_residency = 0;
	state->power_usage = -1;
	state->flags = CPUIDLE_FLAG_TIME_VALID;
	state->enter = poll_idle;
	state->disabled = false;
}
#else
static void poll_idle_init(struct cpuidle_driver *drv) {}
#endif /* !CONFIG_ARCH_HAS_CPU_RELAX */

/**
 * __cpuidle_register_driver: register the driver
 * @drv: a valid pointer to a struct cpuidle_driver
 *
 * Do some sanity checks, initialize the driver, assign the driver to the
 * global cpuidle driver variable(s) and set up the broadcast timer if the
 * cpuidle driver has some states that shut down the local timer.
 *
 * Returns 0 on success, a negative error code otherwise:
 *  * -EINVAL if the driver pointer is NULL or no idle states are available
 *  * -ENODEV if the cpuidle framework is disabled
 *  * -EBUSY if the driver is already assigned to the global variable(s)
 */
static int __cpuidle_register_driver(struct cpuidle_driver *drv)
{
	int ret;

	if (!drv || !drv->state_count)
		return -EINVAL;

	if (cpuidle_disabled())
		return -ENODEV;

	__cpuidle_driver_init(drv);

	ret = __cpuidle_set_driver(drv);
	if (ret)
		return ret;

	if (drv->bctimer)
		on_each_cpu_mask(drv->cpumask, cpuidle_setup_broadcast_timer,
				 (void *)CLOCK_EVT_NOTIFY_BROADCAST_ON, 1);

	poll_idle_init(drv);

	return 0;
}

/**
 * cpuidle_unregister_driver - unregisters a driver
 * @drv: the driver
 */
static void __cpuidle_unregister_driver(struct cpuidle_driver *drv)
{
	if (WARN_ON(drv->refcnt > 0))
		return;

	if (drv->bctimer) {
		drv->bctimer = 0;
		on_each_cpu_mask(drv->cpumask, cpuidle_setup_broadcast_timer,
				 (void *)CLOCK_EVT_NOTIFY_BROADCAST_OFF, 1);
	}

	__cpuidle_unset_driver(drv);
}

/**
 * cpuidle_register_driver - registers a driver
 * @drv: the driver
 */
int cpuidle_register_driver(struct cpuidle_driver *drv)
{
	int ret;

	spin_lock(&cpuidle_driver_lock);
	ret = __cpuidle_register_driver(drv);
	spin_unlock(&cpuidle_driver_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(cpuidle_register_driver);

/**
 * cpuidle_unregister_driver - unregisters a driver
 * @drv: the driver
 */
void cpuidle_unregister_driver(struct cpuidle_driver *drv)
{
	spin_lock(&cpuidle_driver_lock);
	__cpuidle_unregister_driver(drv);
	spin_unlock(&cpuidle_driver_lock);
}
EXPORT_SYMBOL_GPL(cpuidle_unregister_driver);

/**
 * cpuidle_get_driver - return the current driver
 */
struct cpuidle_driver *cpuidle_get_driver(void)
{
	struct cpuidle_driver *drv;
	int cpu;

	cpu = get_cpu();
	drv = __cpuidle_get_cpu_driver(cpu);
	put_cpu();

	return drv;
}
EXPORT_SYMBOL_GPL(cpuidle_get_driver);

/**
 * cpuidle_get_cpu_driver - return the driver tied with a cpu
 */
struct cpuidle_driver *cpuidle_get_cpu_driver(struct cpuidle_device *dev)
{
	if (!dev)
		return NULL;

	return __cpuidle_get_cpu_driver(dev->cpu);
}
EXPORT_SYMBOL_GPL(cpuidle_get_cpu_driver);

struct cpuidle_driver *cpuidle_driver_ref(void)
{
	struct cpuidle_driver *drv;

	spin_lock(&cpuidle_driver_lock);

	drv = cpuidle_get_driver();
	if (drv)
		drv->refcnt++;

	spin_unlock(&cpuidle_driver_lock);
	return drv;
}

void cpuidle_driver_unref(void)
{
	struct cpuidle_driver *drv;

	spin_lock(&cpuidle_driver_lock);

	drv = cpuidle_get_driver();
	if (drv && !WARN_ON(drv->refcnt <= 0))
		drv->refcnt--;

	spin_unlock(&cpuidle_driver_lock);
}
