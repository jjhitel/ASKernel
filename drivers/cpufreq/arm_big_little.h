/*
 * ARM big.LITTLE platform's CPUFreq header file
 *
 * Copyright (C) 2013 ARM Ltd.
 * Sudeep KarkadaNagesha <sudeep.karkadanagesha@arm.com>
 *
 * Copyright (C) 2013 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef CPUFREQ_ARM_BIG_LITTLE_H
#define CPUFREQ_ARM_BIG_LITTLE_H

#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/types.h>

/* Currently we support only two clusters */
#define A15_CLUSTER	0
#define A7_CLUSTER	1
#define MAX_CLUSTERS	2

#ifdef CONFIG_BL_SWITCHER
extern bool bL_switching_enabled;
#define is_bL_switching_enabled()		bL_switching_enabled
#define set_switching_enabled(x) 		(bL_switching_enabled = (x))
#else
#define is_bL_switching_enabled()		false
#define set_switching_enabled(x) 		do { } while (0)
#endif

struct cpufreq_arm_bL_ops {
	char name[CPUFREQ_NAME_LEN];
	int (*get_transition_latency)(struct device *cpu_dev);

	/*
	 * This must set opp table for cpu_dev in a similar way as done by
	 * of_init_opp_table().
	 */
	int (*init_opp_table)(struct device *cpu_dev);
};

static inline int cpu_to_cluster(int cpu)
{
	return is_bL_switching_enabled() ? MAX_CLUSTERS:
		topology_physical_package_id(cpu);
}

int bL_cpufreq_register(struct cpufreq_arm_bL_ops *ops);
void bL_cpufreq_unregister(struct cpufreq_arm_bL_ops *ops);

#endif /* CPUFREQ_ARM_BIG_LITTLE_H */
