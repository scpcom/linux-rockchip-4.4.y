/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

/* gator_events_armvX.c is used for Linux 2.6.x */
#if GATOR_PERF_PMU_SUPPORT

#include <linux/io.h>
#include <linux/perf_event.h>
#include <linux/slab.h>

/* Maximum number of per-core counters - currently reserves enough space for two full hardware PMUs for big.LITTLE */
#define CNTMAX 16
/* Maximum number of uncore counters */
#define UCCNT 32

/* Default to 0 if unable to probe the revision which was the previous behavior */
#define DEFAULT_CCI_REVISION 0

/* A gator_attr is needed for every counter */
struct gator_attr {
    /* Set once in gator_events_perf_pmu_*_init - the name of the event in the gatorfs */
    char name[40];
    /* Exposed in gatorfs - set by gatord to enable this counter */
    unsigned long enabled;
    /* Set once in gator_events_perf_pmu_*_init - the perf type to use, see perf_type_id in the perf_event.h header file. */
    unsigned long type;
    /* Exposed in gatorfs - set by gatord to select the event to collect */
    unsigned long event;
    /* Exposed in gatorfs - set by gatord with the sample period to use and enable EBS for this counter */
    unsigned long count;
    /* Exposed as read only in gatorfs - set once in __attr_init as the key to use in the APC data */
    unsigned long key;
    /* only one of the two may ever be set */
    /* The gator_cpu object that it belongs to */
    const struct gator_cpu * gator_cpu;
    /* The uncore_pmu object that it belongs to */
    const struct uncore_pmu * uncore_pmu;
};

/* Per-core counter attributes */
static struct gator_attr attrs[CNTMAX];
/* Number of initialized per-core counters */
static int attr_count;
/* Uncore counter attributes */
static struct gator_attr uc_attrs[UCCNT];
/* Number of initialized uncore counters */
static int uc_attr_count;
/* Mapping from CPU to gator_cpu object */
static const struct gator_cpu * gator_cpus_per_core[ARRAY_SIZE(gator_cpuids)];

struct gator_event {
    uint32_t curr;
    uint32_t prev;
    uint32_t prev_delta;
    bool zero;
    struct perf_event *pevent;
    struct perf_event_attr *pevent_attr;
};

static DEFINE_PER_CPU(struct gator_event[CNTMAX], events);
static struct gator_event uc_events[UCCNT];
static DEFINE_PER_CPU(int[(CNTMAX + UCCNT)*2], perf_cnt);

static void gator_events_perf_pmu_stop(void);

static int __create_files(struct super_block *sb, struct dentry *root, struct gator_attr *const attr)
{
    struct dentry *dir;

    if (attr->name[0] == '\0')
        return 0;
    dir = gatorfs_mkdir(sb, root, attr->name);
    if (!dir)
        return -1;
    gatorfs_create_ulong(sb, dir, "enabled", &attr->enabled);
    gatorfs_create_ulong(sb, dir, "count", &attr->count);
    gatorfs_create_ro_ulong(sb, dir, "key", &attr->key);
    gatorfs_create_ulong(sb, dir, "event", &attr->event);

    return 0;
}

static int gator_events_perf_pmu_create_files(struct super_block *sb, struct dentry *root)
{
    int cnt;

    for (cnt = 0; cnt < attr_count; cnt++) {
        if (__create_files(sb, root, &attrs[cnt]) != 0)
            return -1;
    }

    for (cnt = 0; cnt < uc_attr_count; cnt++) {
        if (__create_files(sb, root, &uc_attrs[cnt]) != 0)
            return -1;
    }

    return 0;
}

static void ebs_overflow_handler(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs)
{
    gator_backtrace_handler(regs);
}

static void dummy_handler(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs)
{
    /* Required as perf_event_create_kernel_counter() requires an overflow handler, even though all we do is poll */
}

static int gator_events_perf_pmu_read(int **buffer, bool sched_switch);

static int gator_events_perf_pmu_online(int **buffer, bool migrate)
{
    return gator_events_perf_pmu_read(buffer, false);
}

static void __online_dispatch(int cpu, bool migrate, struct gator_attr *const attr, struct gator_event *const event)
{
    const char * const cpu_core_name = (gator_cpus_per_core[cpu] != NULL ? gator_cpus_per_core[cpu]->core_name : "Unknown");
    const u32 cpu_cpuid = (gator_cpus_per_core[cpu] != NULL ? gator_cpus_per_core[cpu]->cpuid : gator_cpuids[cpu]);

    perf_overflow_handler_t handler;
    struct perf_event *pevent;

    event->zero = true;

    if (event->pevent != NULL || event->pevent_attr == NULL || migrate)
        return;

    if ((gator_cpus_per_core[cpu] == NULL) || ((gator_cpus_per_core[cpu] != attr->gator_cpu) && (attr->gator_cpu != NULL))) {
        pr_debug("gator: Counter %s does not apply to core %i (Core: %s 0x%x, Counter: %s 0x%lx)\n",
                  attr->name, cpu, cpu_core_name, cpu_cpuid, attr->gator_cpu->core_name, attr->gator_cpu->cpuid);
        return;
    }
    else {
        pr_debug("gator: Counter %s applies to core %i (%s 0x%x)\n", attr->name, cpu, cpu_core_name, cpu_cpuid);
    }

    if (attr->count > 0)
        handler = ebs_overflow_handler;
    else
        handler = dummy_handler;

    pevent = perf_event_create_kernel_counter(event->pevent_attr, cpu, NULL, handler, NULL);
    if (IS_ERR(pevent)) {
        pr_err("gator: unable to online a counter on cpu %d\n", cpu);
        return;
    }

    if (pevent->state != PERF_EVENT_STATE_ACTIVE) {
        pr_err("gator: inactive counter on cpu %d\n", cpu);
        perf_event_release_kernel(pevent);
        return;
    }

    event->pevent = pevent;
}

#define GATOR_IF_UNCORE_CPUMASK(uncore_pmu, cpu, cpumask)                                   \
    (cpumask) = (struct cpumask *) GATOR_ATOMIC_READ(&((uncore_pmu)->cpumask_atomic));      \
    if (((!(cpumask)) && ((cpu) == 0)) || ((cpumask) && cpumask_test_cpu((cpu), (cpumask))))

static void gator_events_perf_pmu_online_dispatch(int cpu, bool migrate)
{
    int cnt;

    cpu = pcpu_to_lcpu(cpu);

    for (cnt = 0; cnt < attr_count; cnt++) {
        __online_dispatch(cpu, migrate, &attrs[cnt], &per_cpu(events, cpu)[cnt]);
    }

    for (cnt = 0; cnt < uc_attr_count; cnt++) {
        struct cpumask * cpumask;
        GATOR_IF_UNCORE_CPUMASK(uc_attrs[cnt].uncore_pmu, cpu, cpumask) {
            __online_dispatch(cpu, migrate, &uc_attrs[cnt], &uc_events[cnt]);
        }
    }
}

static void __offline_dispatch(int cpu, struct gator_event *const event)
{
    struct perf_event *pe = NULL;

    if (event->pevent) {
        pe = event->pevent;
        event->pevent = NULL;
    }

    if (pe)
        perf_event_release_kernel(pe);
}

static void gator_events_perf_pmu_offline_dispatch(int cpu, bool migrate)
{
    int cnt;

    if (migrate)
        return;
    cpu = pcpu_to_lcpu(cpu);

    for (cnt = 0; cnt < attr_count; cnt++)
        __offline_dispatch(cpu, &per_cpu(events, cpu)[cnt]);

    for (cnt = 0; cnt < uc_attr_count; cnt++) {
        struct cpumask * cpumask;
        GATOR_IF_UNCORE_CPUMASK(uc_attrs[cnt].uncore_pmu, cpu, cpumask) {
            __offline_dispatch(cpu, &uc_events[cnt]);
        }
    }
}

static int __check_ebs(struct gator_attr *const attr)
{
    if (attr->count > 0) {
        if (!event_based_sampling) {
            event_based_sampling = true;
        } else {
            pr_warn("gator: Only one ebs counter is allowed\n");
            return -1;
        }
    }

    return 0;
}

static int __start(struct gator_attr *const attr, struct gator_event *const event)
{
    u32 size = sizeof(struct perf_event_attr);

    event->pevent = NULL;
    /* Skip disabled counters */
    if (!attr->enabled)
        return 0;

    event->prev = 0;
    event->curr = 0;
    event->prev_delta = 0;
    event->pevent_attr = kmalloc(size, GFP_KERNEL);
    if (!event->pevent_attr) {
        gator_events_perf_pmu_stop();
        return -1;
    }

    memset(event->pevent_attr, 0, size);
    event->pevent_attr->type = attr->type;
    event->pevent_attr->size = size;
    event->pevent_attr->config = attr->event;
    event->pevent_attr->sample_period = attr->count;
    event->pevent_attr->pinned = 1;

    return 0;
}

static int gator_events_perf_pmu_start(void)
{
    int cnt, cpu;

    event_based_sampling = false;
    for (cnt = 0; cnt < attr_count; cnt++) {
        if (__check_ebs(&attrs[cnt]) != 0)
            return -1;
    }

    for (cnt = 0; cnt < uc_attr_count; cnt++) {
        if (__check_ebs(&uc_attrs[cnt]) != 0)
            return -1;
    }

    for_each_present_cpu(cpu) {
        for (cnt = 0; cnt < attr_count; cnt++) {
            if (__start(&attrs[cnt], &per_cpu(events, cpu)[cnt]) != 0)
                return -1;
        }
    }

    for (cnt = 0; cnt < uc_attr_count; cnt++) {
        if (__start(&uc_attrs[cnt], &uc_events[cnt]) != 0)
            return -1;
    }

    return 0;
}

static void __event_stop(struct gator_event *const event)
{
    kfree(event->pevent_attr);
    event->pevent_attr = NULL;
}

static void __attr_stop(struct gator_attr *const attr)
{
    attr->enabled = 0;
    attr->event = 0;
    attr->count = 0;
}

static void gator_events_perf_pmu_stop(void)
{
    unsigned int cnt, cpu;

    for_each_present_cpu(cpu) {
        for (cnt = 0; cnt < attr_count; cnt++)
            __event_stop(&per_cpu(events, cpu)[cnt]);
    }

    for (cnt = 0; cnt < uc_attr_count; cnt++)
        __event_stop(&uc_events[cnt]);

    for (cnt = 0; cnt < attr_count; cnt++)
        __attr_stop(&attrs[cnt]);

    for (cnt = 0; cnt < uc_attr_count; cnt++)
        __attr_stop(&uc_attrs[cnt]);
}

static void __read(int *const len, int cpu, struct gator_attr *const attr, struct gator_event *const event)
{
    uint32_t delta;
    struct perf_event *const ev = event->pevent;

    if (ev != NULL && ev->state == PERF_EVENT_STATE_ACTIVE) {
        /* After creating the perf counter in __online_dispatch, there
         * is a race condition between gator_events_perf_pmu_online and
         * gator_events_perf_pmu_read. So have
         * gator_events_perf_pmu_online call gator_events_perf_pmu_read
         * and in __read check to see if it's the first call after
         * __online_dispatch and if so, run the online code.
         */
        if (event->zero) {
            ev->pmu->read(ev);
            event->prev = event->curr = local64_read(&ev->count);
            event->prev_delta = 0;
            per_cpu(perf_cnt, cpu)[(*len)++] = attr->key;
            per_cpu(perf_cnt, cpu)[(*len)++] = 0;
            event->zero = false;
        } else {
            ev->pmu->read(ev);
            event->curr = local64_read(&ev->count);
            delta = event->curr - event->prev;
            if (delta != 0 || delta != event->prev_delta) {
                event->prev_delta = delta;
                event->prev = event->curr;
                per_cpu(perf_cnt, cpu)[(*len)++] = attr->key;
                per_cpu(perf_cnt, cpu)[(*len)++] = delta;
            }
        }
    }
}

static int gator_events_perf_pmu_read(int **buffer, bool sched_switch)
{
    int cnt, len = 0;
    const int cpu = get_logical_cpu();

    for (cnt = 0; cnt < attr_count; cnt++)
        __read(&len, cpu, &attrs[cnt], &per_cpu(events, cpu)[cnt]);

    for (cnt = 0; cnt < uc_attr_count; cnt++) {
        struct cpumask * cpumask;
        GATOR_IF_UNCORE_CPUMASK(uc_attrs[cnt].uncore_pmu, cpu, cpumask) {
            __read(&len, cpu, &uc_attrs[cnt], &uc_events[cnt]);
        }
    }

    if (buffer)
        *buffer = per_cpu(perf_cnt, cpu);

    return len;
}

static struct gator_interface gator_events_perf_pmu_interface = {
    .name = "perf_pmu",
    .start = gator_events_perf_pmu_start,
    .stop = gator_events_perf_pmu_stop,
    .online = gator_events_perf_pmu_online,
    .online_dispatch = gator_events_perf_pmu_online_dispatch,
    .offline_dispatch = gator_events_perf_pmu_offline_dispatch,
    .read = gator_events_perf_pmu_read,
};

static void __attr_init(struct gator_attr *const attr)
{
    attr->name[0] = '\0';
    attr->enabled = 0;
    attr->type = 0;
    attr->event = 0;
    attr->count = 0;
    attr->key = gator_events_get_key();
    attr->gator_cpu = NULL;
    attr->uncore_pmu = NULL;
}

static void gator_events_perf_pmu_uncore_init(const struct uncore_pmu *const uncore_pmu, const int type)
{
    int cnt;

    if (uncore_pmu->has_cycles_counter) {
        if (uc_attr_count < ARRAY_SIZE(uc_attrs)) {
            snprintf(uc_attrs[uc_attr_count].name, sizeof(uc_attrs[uc_attr_count].name), "%s_ccnt", uncore_pmu->core_name);
            uc_attrs[uc_attr_count].type = type;
            uc_attrs[uc_attr_count].gator_cpu = NULL;
            uc_attrs[uc_attr_count].uncore_pmu = uncore_pmu;
        }
        ++uc_attr_count;
    }

    for (cnt = 0; cnt < uncore_pmu->pmnc_counters; ++cnt, ++uc_attr_count) {
        struct gator_attr *const attr = &uc_attrs[uc_attr_count];

        if (uc_attr_count < ARRAY_SIZE(uc_attrs)) {
            snprintf(attr->name, sizeof(attr->name), "%s_cnt%d", uncore_pmu->core_name, cnt);
            attr->type = type;
            attr->gator_cpu = NULL;
            attr->uncore_pmu = uncore_pmu;
        }
    }
}

static void gator_events_perf_pmu_cpu_init(const struct gator_cpu *const gator_cpu, const int type)
{
    int cnt;

    if (gator_cluster_count < ARRAY_SIZE(gator_clusters)) {
        gator_clusters[gator_cluster_count++] = gator_cpu;

        if (attr_count < ARRAY_SIZE(attrs)) {
            snprintf(attrs[attr_count].name, sizeof(attrs[attr_count].name), "%s_ccnt", gator_cpu->pmnc_name);
            attrs[attr_count].type = type;
            attrs[attr_count].gator_cpu = gator_cpu;
            attrs[uc_attr_count].uncore_pmu = NULL;
        }
        ++attr_count;

        for (cnt = 0; cnt < gator_cpu->pmnc_counters; ++cnt, ++attr_count) {
            struct gator_attr *const attr = &attrs[attr_count];

            if (attr_count < ARRAY_SIZE(attrs)) {
                snprintf(attr->name, sizeof(attr->name), "%s_cnt%d", gator_cpu->pmnc_name, cnt);
                attr->type = type;
                attr->gator_cpu = gator_cpu;
                attr->uncore_pmu = NULL;
            }
        }
    }
}

static int gator_events_perf_pmu_reread(void)
{
    struct perf_event_attr pea;
    struct perf_event *pe;
    const struct gator_cpu *gator_cpu;
    const struct uncore_pmu *uncore_pmu;
    int type;
    int cpu;
    int cnt;

    for (cnt = 0; cnt < ARRAY_SIZE(attrs); cnt++)
        __attr_init(&attrs[cnt]);
    for (cnt = 0; cnt < ARRAY_SIZE(uc_attrs); cnt++)
        __attr_init(&uc_attrs[cnt]);
    for (cnt = 0; cnt < ARRAY_SIZE(gator_cpus_per_core); cnt++)
        gator_cpus_per_core[cnt] = NULL;

    memset(&pea, 0, sizeof(pea));
    pea.size = sizeof(pea);
    pea.config = 0xFF;
    attr_count = 0;
    uc_attr_count = 0;
    for (type = PERF_TYPE_MAX; type < 0x20; ++type) {
        pea.type = type;

        /* A particular PMU may work on some but not all cores, so try on each core */
        pe = NULL;
        for_each_present_cpu(cpu) {
            pe = perf_event_create_kernel_counter(&pea, cpu, NULL, dummy_handler, NULL);
            if (!IS_ERR(pe))
                break;
        }
        /* Assume that valid PMUs are contiguous */
        if (IS_ERR(pe)) {
            pea.config = 0xff00;
            pe = perf_event_create_kernel_counter(&pea, 0, NULL, dummy_handler, NULL);
            if (IS_ERR(pe))
                break;
        }

        if (pe->pmu != NULL && type == pe->pmu->type) {
            pr_notice("gator: perf pmu: %s\n", pe->pmu->name);
            if ((uncore_pmu = gator_find_uncore_pmu(pe->pmu->name)) != NULL) {
                pr_notice("gator: Adding uncore counters for %s (%s) with type %i\n", uncore_pmu->core_name, uncore_pmu->pmnc_name, type);
                gator_events_perf_pmu_uncore_init(uncore_pmu, type);
            } else if ((gator_cpu = gator_find_cpu_by_pmu_name(pe->pmu->name)) != NULL) {
                pr_notice("gator: Adding cpu counters for %s (%s) with type %i\n", gator_cpu->core_name, gator_cpu->pmnc_name, type);
                gator_events_perf_pmu_cpu_init(gator_cpu, type);
            }
            /* Initialize gator_attrs for dynamic PMUs here */
        }

        perf_event_release_kernel(pe);
    }

    /* Now use CPUID to create CPU->PMU mapping, and add any missing PMUs using PERF_TYPE_RAW */
    for (cpu = 0; cpu < ARRAY_SIZE(gator_cpuids); ++cpu) {
        if (gator_cpuids[cpu] != ((u32) -1)) {
            bool found_cpu = false;
            const struct gator_cpu *gator_cpu = gator_find_cpu_by_cpuid(gator_cpuids[cpu]);

#if defined(__arm__) || defined(__aarch64__)
            if (gator_cpu == NULL) {
                pr_err("gator: This CPU is not recognized, using the Arm architected counters\n");
                gator_cpu = &gator_pmu_other;
            }
#else
            if (gator_cpu == NULL) {
                pr_err("gator: This CPU is not recognized\n");
                return -1;
            }
#endif
            for (cnt = 0; cnt < gator_cluster_count; ++cnt) {
                if (gator_clusters[cnt] == gator_cpu) {
                    found_cpu = true;
                    break;
                }
            }

            if (!found_cpu) {
                pr_notice("gator: Adding cpu counters (based on cpuid) for %s\n", gator_cpu->core_name);
                gator_events_perf_pmu_cpu_init(gator_cpu, PERF_TYPE_RAW);
            }

            gator_cpus_per_core[cpu] = gator_cpu;
        }
    }

    /* Log the PMUs used per core */
    for (cpu = 0; cpu < ARRAY_SIZE(gator_cpuids); ++cpu) {
        if (gator_cpus_per_core[cpu] != NULL) {
            pr_notice("gator: Using %s (0x%lx) for cpu %i\n", gator_cpus_per_core[cpu]->core_name, gator_cpus_per_core[cpu]->cpuid, cpu);
        }
    }

    /* Initialize gator_attrs for non-dynamic PMUs here */
    if (attr_count > CNTMAX) {
        pr_err("gator: Too many perf counters, please increase CNTMAX\n");
        return -1;
    }

    if (uc_attr_count > UCCNT) {
        pr_err("gator: Too many perf uncore counters, please increase UCCNT\n");
        return -1;
    }

    return 0;
}

int gator_events_perf_pmu_init(void)
{
    return gator_events_install(&gator_events_perf_pmu_interface);
}

#else

static int gator_events_perf_pmu_reread(void)
{
    return 0;
}

static int gator_events_perf_pmu_create_files(struct super_block *sb, struct dentry *root)
{
    return 0;
}

#endif
