/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/* This version must match the gator daemon version */
#define PROTOCOL_VERSION    651
static unsigned long gator_protocol_version = PROTOCOL_VERSION;

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/utsname.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <linux/cpuhotplug.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
#include <linux/notifier.h>
#endif

#include "gator.h"
#include "generated_gator_src_md5.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
#error Kernels prior to 3.4 not supported. DS-5 v5.21 and earlier supported 2.6.32 and later.
#endif

#if defined(MODULE) && !defined(CONFIG_MODULES)
#error Cannot build a module against a kernel that does not support modules. To resolve, either rebuild the kernel to support modules or build gator as part of the kernel.
#endif

#if !defined(CONFIG_GENERIC_TRACER) && !defined(CONFIG_TRACING)
#error gator requires the kernel to have CONFIG_GENERIC_TRACER or CONFIG_TRACING defined
#endif

#ifndef CONFIG_PROFILING
#error gator requires the kernel to have CONFIG_PROFILING defined
#endif

#ifndef CONFIG_HIGH_RES_TIMERS
#error gator requires the kernel to have CONFIG_HIGH_RES_TIMERS defined to support PC sampling
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0) && defined(__arm__) && defined(CONFIG_SMP) && !defined(CONFIG_LOCAL_TIMERS)
#error gator requires the kernel to have CONFIG_LOCAL_TIMERS defined on SMP systems
#endif

#if !(GATOR_PERF_PMU_SUPPORT)
#ifndef CONFIG_PERF_EVENTS
#error gator requires the kernel to have CONFIG_PERF_EVENTS defined to support pmu hardware counters
#elif !defined CONFIG_HW_PERF_EVENTS
#error gator requires the kernel to have CONFIG_HW_PERF_EVENTS defined to support pmu hardware counters
#endif
#endif

/******************************************************************************
 * DEFINES
 ******************************************************************************/
#define SUMMARY_BUFFER_SIZE       (1*1024)
#define BACKTRACE_BUFFER_SIZE     (128*1024)
#define NAME_BUFFER_SIZE          (64*1024)
#define COUNTER_BUFFER_SIZE       (64*1024) /* counters have the core as part of the data and the core value in the frame header may be discarded */
#define BLOCK_COUNTER_BUFFER_SIZE (128*1024)
#define ANNOTATE_BUFFER_SIZE      (128*1024)    /* annotate counters have the core as part of the data and the core value in the frame header may be discarded */
#define SCHED_TRACE_BUFFER_SIZE   (128*1024)
#define IDLE_BUFFER_SIZE          (32*1024) /* idle counters have the core as part of the data and the core value in the frame header may be discarded */
#define ACTIVITY_BUFFER_SIZE      (128*1024)

#define NO_COOKIE      0U
#define UNRESOLVED_COOKIE ~0U

#define FRAME_SUMMARY       1
#define FRAME_BACKTRACE     2
#define FRAME_NAME          3
#define FRAME_COUNTER       4
#define FRAME_BLOCK_COUNTER 5
#define FRAME_ANNOTATE      6
#define FRAME_SCHED_TRACE   7
#define FRAME_IDLE          9
#define FRAME_ACTIVITY     13

#define MESSAGE_END_BACKTRACE 1

/* Name Frame Messages */
#define MESSAGE_COOKIE      1
#define MESSAGE_THREAD_NAME 2

/* Scheduler Trace Frame Messages */
#define MESSAGE_SCHED_SWITCH 1
#define MESSAGE_SCHED_EXIT   2

/* Summary Frame Messages */
#define MESSAGE_SUMMARY   1
#define MESSAGE_CORE_NAME 3

/* Activity Frame Messages */
#define MESSAGE_LINK   1
#define MESSAGE_SWITCH 2
#define MESSAGE_EXIT   3

#define MAXSIZE_PACK32     5
#define MAXSIZE_PACK64    10

#define FRAME_HEADER_SIZE 3

#if defined(__arm__)
#define PC_REG regs->ARM_pc
#elif defined(__aarch64__)
#define PC_REG regs->pc
#else
#define PC_REG regs->ip
#endif

enum {
    SUMMARY_BUF,
    BACKTRACE_BUF,
    NAME_BUF,
    COUNTER_BUF,
    BLOCK_COUNTER_BUF,
    ANNOTATE_BUF,
    SCHED_TRACE_BUF,
    IDLE_BUF,
    ACTIVITY_BUF,
    NUM_GATOR_BUFS
};

/******************************************************************************
 * Globals
 ******************************************************************************/
static unsigned long gator_cpu_cores;
/* Size of the largest buffer. Effectively constant, set in gator_op_create_files */
static unsigned long userspace_buffer_size;
static unsigned long gator_backtrace_depth;
/* How often to commit the buffers for live in nanoseconds */
static u64 gator_live_rate;

static unsigned long gator_started;
static u64 gator_monotonic_started;
static u64 gator_sync_time;
static u64 gator_hibernate_time;
static unsigned long gator_buffer_opened;
static unsigned long gator_timer_count;
static unsigned long gator_response_type;
static DEFINE_MUTEX(start_mutex);
static DEFINE_MUTEX(gator_buffer_mutex);

static bool event_based_sampling;

static DECLARE_WAIT_QUEUE_HEAD(gator_buffer_wait);
static DECLARE_WAIT_QUEUE_HEAD(gator_annotate_wait);
static struct timer_list gator_buffer_wake_up_timer;
static bool gator_buffer_wake_run;
/* Initialize semaphore unlocked to initialize memory values */
static DEFINE_SEMAPHORE(gator_buffer_wake_sem);
static struct task_struct *gator_buffer_wake_thread;
static LIST_HEAD(gator_events);

static DEFINE_PER_CPU(u64, last_timestamp);

static bool printed_monotonic_warning;

static u32 gator_cpuids[NR_CPUS];
int gator_clusterids[NR_CPUS];
static bool sent_core_name[NR_CPUS];

static DEFINE_PER_CPU(bool, in_scheduler_context);

/******************************************************************************
 * Prototypes
 ******************************************************************************/
static void gator_emit_perf_time(u64 time);
static void gator_op_create_files(struct super_block *sb, struct dentry *root);
static void gator_backtrace_handler(struct pt_regs *const regs);
static int gator_events_perf_pmu_reread(void);
static int gator_events_perf_pmu_create_files(struct super_block *sb, struct dentry *root);
static void gator_trace_power_init(void);
static int gator_trace_power_create_files(struct super_block *sb, struct dentry *root);
static int sched_trace_create_files(struct super_block *sb, struct dentry *root);
static void gator_trace_sched_init(void);

/* gator_buffer is protected by being per_cpu and by having IRQs
 * disabled when writing to it. Most marshal_* calls take care of this
 * except for marshal_cookie*, marshal_backtrace* and marshal_frame
 * where the caller is responsible for doing so. No synchronization is
 * needed with the backtrace buffer as it is per cpu and is only used
 * from the hrtimer. The annotate_lock must be held when using the
 * annotation buffer as it is not per cpu. collect_counters which is
 * the sole writer to the block counter frame is additionally
 * protected by the per cpu collecting flag.
 */

/* Size of the buffer, must be a power of 2. Effectively constant, set in gator_op_setup. */
static uint32_t gator_buffer_size[NUM_GATOR_BUFS];
/* gator_buffer_size - 1, bitwise and with pos to get offset into the array. Effectively constant, set in gator_op_setup. */
static uint32_t gator_buffer_mask[NUM_GATOR_BUFS];
/* Read position in the buffer. Initialized to zero in gator_op_setup and incremented after bytes are read by userspace in userspace_buffer_read */
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_read);
/* Write position in the buffer. Initialized to zero in gator_op_setup and incremented after bytes are written to the buffer */
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_write);
/* Commit position in the buffer. Initialized to zero in gator_op_setup and incremented after a frame is ready to be read by userspace */
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_commit);
/* If set to false, decreases the number of bytes returned by
 * buffer_bytes_available. Set in buffer_check_space if no space is
 * remaining. Initialized to true in gator_op_setup. This means that
 * if we run out of space, continue to report that no space is
 * available until bytes are read by userspace
 */
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], buffer_space_available);
/* The buffer. Allocated in gator_op_setup */
static DEFINE_PER_CPU(char *[NUM_GATOR_BUFS], gator_buffer);
/* The time after which the buffer should be committed for live display */
static DEFINE_PER_CPU(u64, gator_buffer_commit_time);

/* List of all gator events - new events must be added to this list */
#define GATOR_EVENTS_LIST \
    GATOR_EVENT(gator_events_block_init) \
    GATOR_EVENT(gator_events_ccn504_init) \
    GATOR_EVENT(gator_events_irq_init) \
    GATOR_EVENT(gator_events_l2c310_init) \
    GATOR_EVENT(gator_events_mali_init) \
    GATOR_EVENT(gator_events_mali_midgard_hw_init) \
    GATOR_EVENT(gator_events_mali_midgard_init) \
    GATOR_EVENT(gator_events_meminfo_init) \
    GATOR_EVENT(gator_events_mmapped_init) \
    GATOR_EVENT(gator_events_net_init) \
    GATOR_EVENT(gator_events_perf_pmu_init) \
    GATOR_EVENT(gator_events_sched_init) \

#define GATOR_EVENT(EVENT_INIT) __weak int EVENT_INIT(void);
GATOR_EVENTS_LIST
#undef GATOR_EVENT

static int (*gator_events_list[])(void) = {
#define GATOR_EVENT(EVENT_INIT) EVENT_INIT,
GATOR_EVENTS_LIST
#undef GATOR_EVENT
};

/******************************************************************************
 * Application Includes
 ******************************************************************************/
#include "gator_fs.c"
#include "gator_pmu.c"
#include "gator_buffer_write.c"
#include "gator_buffer.c"
#include "gator_marshaling.c"
#include "gator_hrtimer_gator.c"
#include "gator_cookies.c"
#include "gator_annotate.c"
#include "gator_trace_sched.c"
#include "gator_trace_power.c"
#include "gator_trace_gpu.c"
#include "gator_backtrace.c"
#include "gator_events_perf_pmu.c"

/******************************************************************************
 * Misc
 ******************************************************************************/

MODULE_PARM_DESC(gator_src_md5, "Gator driver source code md5sum");
module_param_named(src_md5, gator_src_md5, charp, 0444);

u32 gator_cpuid(void)
{
#if defined(__arm__) || defined(__aarch64__)
    u32 val;
#if !defined(__aarch64__)
    asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r" (val));
#else
    asm volatile("mrs %0, midr_el1" : "=r" (val));
#endif
    return ((val & 0xff000000) >> 12) | ((val & 0xfff0) >> 4);
#else
    return OTHER;
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0))
static void gator_buffer_wake_up(struct timer_list *t)
#else
static void gator_buffer_wake_up(unsigned long data)
#endif
{
    wake_up(&gator_buffer_wait);
}

static int gator_buffer_wake_func(void *data)
{
    for (;;) {
        if (down_killable(&gator_buffer_wake_sem))
            break;

        /* Eat up any pending events */
        while (!down_trylock(&gator_buffer_wake_sem))
            ;

        if (!gator_buffer_wake_run)
            break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0))
        gator_buffer_wake_up(NULL);
#else
        gator_buffer_wake_up(0);
#endif
    }

    return 0;
}

/******************************************************************************
 * Commit interface
 ******************************************************************************/
static bool buffer_commit_ready(int prev_cpu, int prev_buftype, int *out_cpu, int * out_buftype)
{
    int cpu_x, x;

    // simple sort of QOS/fair scheduling of buffer checking. we scan starting at the next item after the last successful one
    // up to the end, and if nothing is found from the start upto and including the last successful one.
    // that way we do not favour lower number cpu or lower number buffer.

    // do everything after (prev_cpu:prev_buftype)
    for (x = 0; x < NUM_GATOR_BUFS; x++) {
        for_each_present_cpu(cpu_x) {
            if ((cpu_x > prev_cpu) || ((cpu_x == prev_cpu) && (x > prev_buftype))) {
                if (per_cpu(gator_buffer_commit, cpu_x)[x] != per_cpu(gator_buffer_read, cpu_x)[x]) {
                    *out_cpu = cpu_x;
                    *out_buftype = x;
                    return true;
                }
            }
            else {
                goto low_half;
            }
        }
    }


    // now everything upto and including (prev_cpu:prev_buftype)
low_half:

    for (x = 0; x < NUM_GATOR_BUFS; x++) {
        for_each_present_cpu(cpu_x) {
            if ((cpu_x < prev_cpu) || ((cpu_x == prev_cpu) && (x <= prev_buftype))) {
                if (per_cpu(gator_buffer_commit, cpu_x)[x] != per_cpu(gator_buffer_read, cpu_x)[x]) {
                    *out_cpu = cpu_x;
                    *out_buftype = x;
                    return true;
                }
            }
            else {
                goto not_found;
            }
        }
    }

    // nothing found
not_found:

    *out_cpu = -1;
    *out_buftype = -1;
    return false;
}

/******************************************************************************
 * hrtimer interrupt processing
 ******************************************************************************/
static void gator_timer_interrupt(void)
{
    struct pt_regs *const regs = get_irq_regs();

    gator_backtrace_handler(regs);
}

static void gator_backtrace_handler(struct pt_regs *const regs)
{
    u64 time = gator_get_time();
    int cpu = get_physical_cpu();

    /* Output backtrace */
    gator_add_sample(cpu, regs, time);

    /* Collect counters */
    if (!per_cpu(collecting, cpu))
        collect_counters(time, current, false);

    /* No buffer flushing occurs during sched switch for RT-Preempt full. The block counter frame will be flushed by collect_counters, but the sched buffer needs to be explicitly flushed */
#ifdef CONFIG_PREEMPT_RT_FULL
    buffer_check(cpu, SCHED_TRACE_BUF, time);
#endif
}

static int gator_running;

/* This function runs in interrupt context and on the appropriate core */
static void gator_timer_offline(void *migrate)
{
    struct gator_interface *gi;
    int i, len, cpu = get_physical_cpu();
    int *buffer;
    u64 time;

    gator_trace_sched_offline();
    gator_trace_power_offline();

    if (!migrate)
        gator_hrtimer_offline();

    /* Offline any events and output counters */
    time = gator_get_time();
    if (marshal_event_header(time)) {
        list_for_each_entry(gi, &gator_events, list) {
            if (gi->offline) {
                len = gi->offline(&buffer, migrate);
                if (len < 0)
                    pr_err("gator: offline failed for %s\n", gi->name);
                marshal_event(len, buffer);
            }
        }
        /* Only check after writing all counters so that time and corresponding counters appear in the same frame */
        buffer_check(cpu, BLOCK_COUNTER_BUF, time);
    }

    /* Flush all buffers on this core */
    for (i = 0; i < NUM_GATOR_BUFS; i++)
        gator_commit_buffer(cpu, i, time);
}

/* This function runs in interrupt context and may be running on a core other than core 'cpu' */
static void gator_timer_offline_dispatch(int cpu, bool migrate)
{
    struct gator_interface *gi;

    list_for_each_entry(gi, &gator_events, list) {
        if (gi->offline_dispatch)
            gi->offline_dispatch(cpu, migrate);
    }
}

static void gator_timer_stop(void)
{
    int cpu;

    if (gator_running) {
        on_each_cpu(gator_timer_offline, NULL, 1);
        for_each_online_cpu(cpu) {
            gator_timer_offline_dispatch(lcpu_to_pcpu(cpu), false);
        }

        gator_running = 0;
        gator_hrtimer_shutdown();
    }
}

static int gator_get_clusterid(const u32 cpuid)
{
    int i;

    for (i = 0; i < gator_cluster_count; i++) {
        if (gator_clusters[i]->cpuid == cpuid)
            return i;
    }

    return 0;
}

static void gator_send_core_name(const int cpu, const u32 cpuid)
{
#if defined(__arm__) || defined(__aarch64__)
    if (!sent_core_name[cpu] || (cpuid != gator_cpuids[cpu])) {
        const struct gator_cpu *const gator_cpu = gator_find_cpu_by_cpuid(cpuid);
        const char *core_name = NULL;
        char core_name_buf[32];

        /* Save off this cpuid */
        gator_cpuids[cpu] = cpuid;
        gator_clusterids[cpu] = gator_get_clusterid(cpuid);
        if (gator_cpu != NULL) {
            core_name = gator_cpu->core_name;
        } else {
            if (cpuid == -1)
                snprintf(core_name_buf, sizeof(core_name_buf), "Unknown");
            else
                snprintf(core_name_buf, sizeof(core_name_buf), "Unknown (0x%.5x)", cpuid);
            core_name = core_name_buf;
        }

        marshal_core_name(cpu, cpuid, core_name);
        sent_core_name[cpu] = true;
    }
#endif
}

static void gator_read_cpuid(void *arg)
{
    const u32 cpuid = gator_cpuid();
    const int cpu = get_physical_cpu();

    pr_notice("gator: Detected CPUID for %i as 0x%x\n", cpu, cpuid);

    gator_cpuids[cpu] = cpuid;
    gator_clusterids[cpu] = gator_get_clusterid(cpuid);
}

/* This function runs in interrupt context and on the appropriate core */
static void gator_timer_online(void *migrate)
{
    struct gator_interface *gi;
    int len, cpu = get_physical_cpu();
    int *buffer;
    u64 time;

    /* Send what is currently running on this core */
    marshal_sched_trace_switch(current->pid, 0);

    gator_trace_power_online();

    /* online any events and output counters */
    time = gator_get_time();
    if (marshal_event_header(time)) {
        list_for_each_entry(gi, &gator_events, list) {
            if (gi->online) {
                len = gi->online(&buffer, migrate);
                if (len < 0)
                    pr_err("gator: online failed for %s\n", gi->name);
                marshal_event(len, buffer);
            }
        }
        /* Only check after writing all counters so that time and corresponding counters appear in the same frame */
        buffer_check(cpu, BLOCK_COUNTER_BUF, time);
    }

    if (!migrate)
        gator_hrtimer_online();

    gator_send_core_name(cpu, gator_cpuid());
}

/* This function runs in interrupt context and may be running on a core other than core 'cpu' */
static void gator_timer_online_dispatch(int cpu, bool migrate)
{
    struct gator_interface *gi;

    list_for_each_entry(gi, &gator_events, list) {
        if (gi->online_dispatch)
            gi->online_dispatch(cpu, migrate);
    }
}

#include "gator_iks.c"

static int gator_timer_start(unsigned long sample_rate)
{
    int cpu;

    if (gator_running) {
        pr_notice("gator: already running\n");
        return 0;
    }

    gator_running = 1;

    /* event based sampling trumps hr timer based sampling */
    if (event_based_sampling)
        sample_rate = 0;

    if (gator_hrtimer_init(sample_rate, gator_timer_interrupt) == -1)
        return -1;

    /* Send off the previously saved cpuids */
    for_each_present_cpu(cpu) {
        preempt_disable();
        gator_send_core_name(cpu, gator_cpuids[cpu]);
        preempt_enable();
    }

    gator_send_iks_core_names();
    for_each_online_cpu(cpu) {
        gator_timer_online_dispatch(lcpu_to_pcpu(cpu), false);
    }
    on_each_cpu(gator_timer_online, NULL, 1);

    return 0;
}

u64 gator_get_time(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
    struct timespec64 ts;
#else
    struct timespec ts;
#endif
    u64 timestamp;
    u64 prev_timestamp;
    u64 delta;
    int cpu = smp_processor_id();

    /* Match clock_gettime(CLOCK_MONOTONIC_RAW, &ts) from userspace */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
    ktime_get_raw_ts64(&ts);
    timestamp = timespec64_to_ns(&ts);
#else
    getrawmonotonic(&ts);
    timestamp = timespec_to_ns(&ts);
#endif

    /* getrawmonotonic is not monotonic on all systems. Detect and
     * attempt to correct these cases. up to 0.5ms delta has been seen
     * on some systems, which can skew Streamline data when viewing at
     * high resolution. This doesn't work well with interrupts, but that
     * it's OK - the real concern is to catch big jumps in time
     */
    prev_timestamp = per_cpu(last_timestamp, cpu);
    if (prev_timestamp <= timestamp) {
        per_cpu(last_timestamp, cpu) = timestamp;
    } else {
        delta = prev_timestamp - timestamp;
        /* Log the error once */
        if (!printed_monotonic_warning && delta > 500000) {
            pr_err("%s: getrawmonotonic is not monotonic  cpu: %i  delta: %lli\nSkew in Streamline data may be present at the fine zoom levels\n", __func__, cpu, delta);
            printed_monotonic_warning = true;
        }
        timestamp = prev_timestamp;
    }

    return timestamp - gator_monotonic_started;
}

static void gator_emit_perf_time(u64 time)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
    if (time >= gator_sync_time) {
        marshal_event_single64(0, -1, local_clock());
        gator_sync_time += NSEC_PER_SEC;
        if (gator_live_rate <= 0)
            gator_commit_buffer(get_physical_cpu(), COUNTER_BUF, time);
    }
#endif
}

/******************************************************************************
 * cpu hotplug and pm notifiers
 ******************************************************************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)

static enum cpuhp_state gator_cpuhp_online;

static int gator_cpuhp_notify_online(unsigned int cpu)
{
    gator_timer_online_dispatch(cpu, false);
    smp_call_function_single(cpu, gator_timer_online, NULL, 1);
    return 0;
}

static int gator_cpuhp_notify_offline(unsigned int cpu)
{
    smp_call_function_single(cpu, gator_timer_offline, NULL, 1);
    gator_timer_offline_dispatch(cpu, false);
    return 0;
}

static int gator_register_hotcpu_notifier(void)
{
    int retval;

    retval = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "gator/cpuhotplug:online", gator_cpuhp_notify_online, gator_cpuhp_notify_offline);
    if (retval >= 0) {
        gator_cpuhp_online = retval;
        retval = 0;
    }
    return retval;
}

static void gator_unregister_hotcpu_notifier(void)
{
    cpuhp_remove_state(gator_cpuhp_online);
}

#else

static int gator_hotcpu_notify(struct notifier_block *self, unsigned long action, void *hcpu)
{
    int cpu = lcpu_to_pcpu((long)hcpu);

    switch (action) {
    case CPU_DOWN_PREPARE:
    case CPU_DOWN_PREPARE_FROZEN:
        smp_call_function_single(cpu, gator_timer_offline, NULL, 1);
        gator_timer_offline_dispatch(cpu, false);
        break;
    case CPU_ONLINE:
    case CPU_ONLINE_FROZEN:
        gator_timer_online_dispatch(cpu, false);
        smp_call_function_single(cpu, gator_timer_online, NULL, 1);
        break;
    }

    return NOTIFY_OK;
}

static struct notifier_block __refdata gator_hotcpu_notifier = {
    .notifier_call = gator_hotcpu_notify,
};

static int gator_register_hotcpu_notifier(void)
{
    return register_hotcpu_notifier(&gator_hotcpu_notifier);
}

static void gator_unregister_hotcpu_notifier(void)
{
    unregister_hotcpu_notifier(&gator_hotcpu_notifier);
}

#endif

/* n.b. calling "on_each_cpu" only runs on those that are online.
 * Registered linux events are not disabled, so their counters will
 * continue to collect
 */
static int gator_pm_notify(struct notifier_block *nb, unsigned long event, void *dummy)
{
    int cpu;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
    struct timespec64 ts;
#else
    struct timespec ts;
#endif

    switch (event) {
    case PM_HIBERNATION_PREPARE:
    case PM_SUSPEND_PREPARE:
        gator_unregister_hotcpu_notifier();
        unregister_scheduler_tracepoints();
        on_each_cpu(gator_timer_offline, NULL, 1);
        for_each_online_cpu(cpu) {
            gator_timer_offline_dispatch(lcpu_to_pcpu(cpu), false);
        }

        /* Record the wallclock hibernate time */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
        ktime_get_real_ts64(&ts);
        gator_hibernate_time = timespec64_to_ns(&ts) - gator_get_time();
#else
        getnstimeofday(&ts);
        gator_hibernate_time = timespec_to_ns(&ts) - gator_get_time();
#endif
        break;
    case PM_POST_HIBERNATION:
    case PM_POST_SUSPEND:
        /* Adjust gator_monotonic_started for the time spent sleeping, as gator_get_time does not account for it */
        if (gator_hibernate_time > 0) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
            ktime_get_real_ts64(&ts);
            gator_monotonic_started += gator_hibernate_time + gator_get_time() - timespec64_to_ns(&ts);
#else
            getnstimeofday(&ts);
            gator_monotonic_started += gator_hibernate_time + gator_get_time() - timespec_to_ns(&ts);
#endif
            gator_hibernate_time = 0;
        }

        for_each_online_cpu(cpu) {
            gator_timer_online_dispatch(lcpu_to_pcpu(cpu), false);
        }
        on_each_cpu(gator_timer_online, NULL, 1);
        register_scheduler_tracepoints();
        gator_register_hotcpu_notifier();
        break;
    }

    return NOTIFY_OK;
}

static struct notifier_block gator_pm_notifier = {
    .notifier_call = gator_pm_notify,
};

static int gator_notifier_start(void)
{
    int retval = gator_register_hotcpu_notifier();
    if (retval == 0)
        retval = register_pm_notifier(&gator_pm_notifier);
    return retval;
}

static void gator_notifier_stop(void)
{
    unregister_pm_notifier(&gator_pm_notifier);
    gator_unregister_hotcpu_notifier();
}

/******************************************************************************
 * Main
 ******************************************************************************/
static void gator_summary(void)
{
    u64 timestamp, uptime;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
    struct timespec64 ts;
#else
    struct timespec ts;
#endif
    char uname_buf[100];

    snprintf(uname_buf, sizeof(uname_buf), "%s %s %s %s %s GNU/Linux", utsname()->sysname, utsname()->nodename, utsname()->release, utsname()->version, utsname()->machine);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0))
    ktime_get_real_ts64(&ts);
    timestamp = timespec64_to_ns(&ts);
#else
    getnstimeofday(&ts);
    timestamp = timespec_to_ns(&ts);
#endif

    /* Similar to reading /proc/uptime from fs/proc/uptime.c, calculate uptime */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
    uptime =  ktime_get_boottime_ns();
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
    {
        void (*m2b)(struct timespec *ts);

        do_posix_clock_monotonic_gettime(&ts);
        /* monotonic_to_bootbased is not defined for some versions of Android */
        m2b = symbol_get(monotonic_to_bootbased);
        if (m2b)
            m2b(&ts);
    }
#else
    get_monotonic_boottime(&ts);
#endif
    uptime = timespec_to_ns(&ts);
#endif

    /* Disable preemption as gator_get_time calls smp_processor_id to verify time is monotonic */
    preempt_disable();
    /* Set monotonic_started to zero as gator_get_time is uptime minus monotonic_started */
    gator_monotonic_started = 0;
    gator_monotonic_started = gator_get_time();

    marshal_summary(timestamp, uptime, gator_monotonic_started, uname_buf);
    gator_sync_time = 0;
    gator_emit_perf_time(gator_monotonic_started);
    /* Always flush COUNTER_BUF so that the initial perf_time is received before it's used */
    gator_commit_buffer(get_physical_cpu(), COUNTER_BUF, 0);
    preempt_enable();
}

int gator_events_install(struct gator_interface *interface)
{
    list_add_tail(&interface->list, &gator_events);

    return 0;
}

int gator_events_get_key(void)
{
    /* key 0 is reserved as a timestamp. key 1 is reserved as the marker
     * for thread specific counters. key 2 is reserved as the marker for
     * core. Odd keys are assigned by the driver, even keys by the
     * daemon.
     */
    static int key = 3;
    const int ret = key;

    key += 2;
    return ret;
}

static int gator_init(void)
{
    calc_first_cluster_size();

    return 0;
}

static void gator_exit(void)
{
    struct gator_interface *gi;

    list_for_each_entry(gi, &gator_events, list)
        if (gi->shutdown)
            gi->shutdown();
}

static int gator_start(void)
{
    unsigned long cpu, i;
    struct gator_interface *gi;

    gator_buffer_wake_run = true;
    gator_buffer_wake_thread = kthread_run(gator_buffer_wake_func, NULL, "gator_bwake");
    if (IS_ERR(gator_buffer_wake_thread))
        goto bwake_failure;

    if (gator_migrate_start())
        goto migrate_failure;

    /* Initialize the buffer with the frame type and core */
    for_each_present_cpu(cpu) {
        for (i = 0; i < NUM_GATOR_BUFS; i++)
            marshal_frame(cpu, i);
        per_cpu(last_timestamp, cpu) = 0;
    }
    printed_monotonic_warning = false;

    /* Capture the start time */
    gator_summary();

    /* start all events */
    list_for_each_entry(gi, &gator_events, list) {
        if (gi->start && gi->start() != 0) {
            struct list_head *ptr = gi->list.prev;

            while (ptr != &gator_events) {
                gi = list_entry(ptr, struct gator_interface, list);

                if (gi->stop)
                    gi->stop();

                ptr = ptr->prev;
            }
            goto events_failure;
        }
    }

    /* cookies shall be initialized before trace_sched_start() and gator_timer_start() */
    if (cookies_initialize())
        goto cookies_failure;
    if (gator_annotate_start())
        goto annotate_failure;
    if (gator_trace_sched_start())
        goto sched_failure;
    if (gator_trace_power_start())
        goto power_failure;
    if (gator_trace_gpu_start())
        goto gpu_failure;
    if (gator_timer_start(gator_timer_count))
        goto timer_failure;
    if (gator_notifier_start())
        goto notifier_failure;

    return 0;

notifier_failure:
    gator_timer_stop();
timer_failure:
    gator_trace_gpu_stop();
gpu_failure:
    gator_trace_power_stop();
power_failure:
    gator_trace_sched_stop();
sched_failure:
    gator_annotate_stop();
annotate_failure:
    cookies_release();
cookies_failure:
    /* stop all events */
    list_for_each_entry(gi, &gator_events, list)
        if (gi->stop)
            gi->stop();
events_failure:
    gator_migrate_stop();
migrate_failure:
    gator_buffer_wake_run = false;
    up(&gator_buffer_wake_sem);
    gator_buffer_wake_thread = NULL;
bwake_failure:

    return -1;
}

static void gator_stop(void)
{
    struct gator_interface *gi;

    gator_annotate_stop();
    gator_trace_sched_stop();
    gator_trace_power_stop();
    gator_trace_gpu_stop();

    /* stop all interrupt callback reads before tearing down other interfaces */
    gator_notifier_stop();  /* should be called before gator_timer_stop to avoid re-enabling the hrtimer after it has been offlined */
    gator_timer_stop();

    /* stop all events */
    list_for_each_entry(gi, &gator_events, list)
        if (gi->stop)
            gi->stop();

    gator_migrate_stop();

    gator_buffer_wake_run = false;
    up(&gator_buffer_wake_sem);
    gator_buffer_wake_thread = NULL;
}

/******************************************************************************
 * Filesystem
 ******************************************************************************/
/* fopen("buffer") */
static int gator_op_setup(void)
{
    int err = 0;
    int cpu, i;

    mutex_lock(&start_mutex);

    gator_buffer_size[SUMMARY_BUF] = SUMMARY_BUFFER_SIZE;
    gator_buffer_mask[SUMMARY_BUF] = SUMMARY_BUFFER_SIZE - 1;

    gator_buffer_size[BACKTRACE_BUF] = BACKTRACE_BUFFER_SIZE;
    gator_buffer_mask[BACKTRACE_BUF] = BACKTRACE_BUFFER_SIZE - 1;

    gator_buffer_size[NAME_BUF] = NAME_BUFFER_SIZE;
    gator_buffer_mask[NAME_BUF] = NAME_BUFFER_SIZE - 1;

    gator_buffer_size[COUNTER_BUF] = COUNTER_BUFFER_SIZE;
    gator_buffer_mask[COUNTER_BUF] = COUNTER_BUFFER_SIZE - 1;

    gator_buffer_size[BLOCK_COUNTER_BUF] = BLOCK_COUNTER_BUFFER_SIZE;
    gator_buffer_mask[BLOCK_COUNTER_BUF] = BLOCK_COUNTER_BUFFER_SIZE - 1;

    gator_buffer_size[ANNOTATE_BUF] = ANNOTATE_BUFFER_SIZE;
    gator_buffer_mask[ANNOTATE_BUF] = ANNOTATE_BUFFER_SIZE - 1;

    gator_buffer_size[SCHED_TRACE_BUF] = SCHED_TRACE_BUFFER_SIZE;
    gator_buffer_mask[SCHED_TRACE_BUF] = SCHED_TRACE_BUFFER_SIZE - 1;

    gator_buffer_size[IDLE_BUF] = IDLE_BUFFER_SIZE;
    gator_buffer_mask[IDLE_BUF] = IDLE_BUFFER_SIZE - 1;

    gator_buffer_size[ACTIVITY_BUF] = ACTIVITY_BUFFER_SIZE;
    gator_buffer_mask[ACTIVITY_BUF] = ACTIVITY_BUFFER_SIZE - 1;

    /* Initialize percpu per buffer variables */
    for (i = 0; i < NUM_GATOR_BUFS; i++) {
        /* Verify buffers are a power of 2 */
        if (gator_buffer_size[i] & (gator_buffer_size[i] - 1)) {
            err = -ENOEXEC;
            goto setup_error;
        }

        for_each_present_cpu(cpu) {
            per_cpu(gator_buffer_read, cpu)[i] = 0;
            per_cpu(gator_buffer_write, cpu)[i] = 0;
            per_cpu(gator_buffer_commit, cpu)[i] = 0;
            per_cpu(buffer_space_available, cpu)[i] = true;
            per_cpu(gator_buffer_commit_time, cpu) = gator_live_rate;

            /* Annotation is a special case that only uses a single buffer */
            if (cpu > 0 && i == ANNOTATE_BUF) {
                per_cpu(gator_buffer, cpu)[i] = NULL;
                continue;
            }

            per_cpu(gator_buffer, cpu)[i] = vmalloc(gator_buffer_size[i]);
            if (!per_cpu(gator_buffer, cpu)[i]) {
                err = -ENOMEM;
                goto setup_error;
            }
        }
    }

setup_error:
    mutex_unlock(&start_mutex);
    return err;
}

/* Actually start profiling (echo 1>/dev/gator/enable) */
static int gator_op_start(void)
{
    int err = 0;

    mutex_lock(&start_mutex);

    if (gator_started || gator_start())
        err = -EINVAL;
    else
        gator_started = 1;

    mutex_unlock(&start_mutex);

    return err;
}

/* echo 0>/dev/gator/enable */
static void gator_op_stop(void)
{
    mutex_lock(&start_mutex);

    if (gator_started) {
        gator_stop();

        mutex_lock(&gator_buffer_mutex);

        gator_started = 0;
        gator_monotonic_started = 0;
        cookies_release();
        wake_up(&gator_buffer_wait);

        mutex_unlock(&gator_buffer_mutex);
    }

    mutex_unlock(&start_mutex);
}

static void gator_shutdown(void)
{
    int cpu, i;

    mutex_lock(&start_mutex);

    for_each_present_cpu(cpu) {
        mutex_lock(&gator_buffer_mutex);
        for (i = 0; i < NUM_GATOR_BUFS; i++) {
            vfree(per_cpu(gator_buffer, cpu)[i]);
            per_cpu(gator_buffer, cpu)[i] = NULL;
            per_cpu(gator_buffer_read, cpu)[i] = 0;
            per_cpu(gator_buffer_write, cpu)[i] = 0;
            per_cpu(gator_buffer_commit, cpu)[i] = 0;
            per_cpu(buffer_space_available, cpu)[i] = true;
            per_cpu(gator_buffer_commit_time, cpu) = 0;
        }
        mutex_unlock(&gator_buffer_mutex);
    }

    memset(&sent_core_name, 0, sizeof(sent_core_name));

    mutex_unlock(&start_mutex);
}

static int gator_set_backtrace(unsigned long val)
{
    int err = 0;

    mutex_lock(&start_mutex);

    if (gator_started)
        err = -EBUSY;
    else
        gator_backtrace_depth = val;

    mutex_unlock(&start_mutex);

    return err;
}

static ssize_t enable_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    return gatorfs_ulong_to_user(gator_started, buf, count, offset);
}

static ssize_t enable_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
    unsigned long val = 0;
    int retval = 0;

    if (*offset)
        return -EINVAL;

    retval = gatorfs_ulong_from_user(&val, buf, count);
    if (retval)
        return retval;

    if (val)
        retval = gator_op_start();
    else
        gator_op_stop();

    if (retval)
        return retval;
    return count;
}

static const struct file_operations enable_fops = {
    .read = enable_read,
    .write = enable_write,
};

static int userspace_buffer_open(struct inode *inode, struct file *file)
{
    int err = -EPERM;

    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;

    if (test_and_set_bit_lock(0, &gator_buffer_opened))
        return -EBUSY;

    err = gator_op_setup();
    if (err)
        goto fail;

    /* NB: the actual start happens from userspace
     * echo 1 >/dev/gator/enable
     */

    return 0;

fail:
    __clear_bit_unlock(0, &gator_buffer_opened);
    return err;
}

static int userspace_buffer_release(struct inode *inode, struct file *file)
{
    gator_op_stop();
    gator_shutdown();
    __clear_bit_unlock(0, &gator_buffer_opened);
    return 0;
}

static ssize_t userspace_buffer_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    int commit, length1, length2, read;
    char *buffer1;
    char *buffer2;
    int cpu, buftype;
    int written = 0;

    /* ensure there is enough space for a whole frame */
    if (count < userspace_buffer_size || *offset)
        return -EINVAL;

    /* sleep until the condition is true or a signal is received the
     * condition is checked each time gator_buffer_wait is woken up
     */
    wait_event_interruptible(gator_buffer_wait, buffer_commit_ready(-1, -1, &cpu, &buftype) || !gator_started);

    if (signal_pending(current))
        return -EINTR;

    if (buftype == -1 || cpu == -1)
        return 0;

    mutex_lock(&gator_buffer_mutex);

    do {
        read = per_cpu(gator_buffer_read, cpu)[buftype];
        commit = per_cpu(gator_buffer_commit, cpu)[buftype];

        /* May happen if the buffer is freed during pending reads. */
        if (!per_cpu(gator_buffer, cpu)[buftype])
            break;

        /* determine the size of two halves */
        length1 = commit - read;
        length2 = 0;
        buffer1 = &(per_cpu(gator_buffer, cpu)[buftype][read]);
        buffer2 = &(per_cpu(gator_buffer, cpu)[buftype][0]);
        if (length1 < 0) {
            length1 = gator_buffer_size[buftype] - read;
            length2 = commit;
        }

        if (length1 + length2 > count - written)
            break;

        /* start, middle or end */
        if (length1 > 0 && copy_to_user(&buf[written], buffer1, length1))
            break;

        /* possible wrap around */
        if (length2 > 0 && copy_to_user(&buf[written + length1], buffer2, length2))
            break;

        per_cpu(gator_buffer_read, cpu)[buftype] = commit;
        written += length1 + length2;

        /* Wake up annotate_write if more space is available */
        if (buftype == ANNOTATE_BUF)
            wake_up(&gator_annotate_wait);
    } while (buffer_commit_ready(cpu, buftype, &cpu, &buftype));

    mutex_unlock(&gator_buffer_mutex);

    /* kick just in case we've lost an SMP event */
    wake_up(&gator_buffer_wait);

    return written > 0 ? written : -EFAULT;
}

static const struct file_operations gator_event_buffer_fops = {
    .open = userspace_buffer_open,
    .release = userspace_buffer_release,
    .read = userspace_buffer_read,
};

static ssize_t depth_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    return gatorfs_ulong_to_user(gator_backtrace_depth, buf, count, offset);
}

static ssize_t depth_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
    unsigned long val;
    int retval;

    if (*offset)
        return -EINVAL;

    retval = gatorfs_ulong_from_user(&val, buf, count);
    if (retval)
        return retval;

    retval = gator_set_backtrace(val);

    if (retval)
        return retval;
    return count;
}

static const struct file_operations depth_fops = {
    .read = depth_read,
    .write = depth_write
};

static void gator_op_create_files(struct super_block *sb, struct dentry *root)
{
    struct dentry *dir;
    int cpu;

    /* reinitialize default values */
    gator_cpu_cores = 0;
    for_each_present_cpu(cpu) {
        gator_cpu_cores++;
    }
    userspace_buffer_size = BACKTRACE_BUFFER_SIZE;
    gator_response_type = 1;
    gator_live_rate = 0;

    gatorfs_create_file(sb, root, "enable", &enable_fops);
    gatorfs_create_file(sb, root, "buffer", &gator_event_buffer_fops);
    gatorfs_create_file(sb, root, "backtrace_depth", &depth_fops);
    gatorfs_create_ro_ulong(sb, root, "cpu_cores", &gator_cpu_cores);
    gatorfs_create_ro_ulong(sb, root, "buffer_size", &userspace_buffer_size);
    gatorfs_create_ulong(sb, root, "tick", &gator_timer_count);
    gatorfs_create_ulong(sb, root, "response_type", &gator_response_type);
    gatorfs_create_ro_ulong(sb, root, "version", &gator_protocol_version);
    gatorfs_create_ro_u64(sb, root, "started", &gator_monotonic_started);
    gatorfs_create_u64(sb, root, "live_rate", &gator_live_rate);

    gator_annotate_create_files(sb, root);

    /* Linux Events */
    dir = gatorfs_mkdir(sb, root, "events");
    gator_pmu_create_files(sb, root, dir);
}

/******************************************************************************
 * Module
 ******************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
static void gator_for_each_tracepoint_range(
		tracepoint_ptr_t *begin, tracepoint_ptr_t *end,
		void (*fct)(struct tracepoint *tp, void *priv),
		void *priv)
{
	tracepoint_ptr_t *iter;

	if (!begin)
		return;
	for (iter = begin; iter < end; iter++)
		fct(tracepoint_ptr_deref(iter), priv);
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)

#define GATOR_TRACEPOINTS \
    GATOR_HANDLE_TRACEPOINT(block_rq_complete); \
    GATOR_HANDLE_TRACEPOINT(cpu_frequency); \
    GATOR_HANDLE_TRACEPOINT(cpu_idle); \
    GATOR_HANDLE_TRACEPOINT(cpu_migrate_begin); \
    GATOR_HANDLE_TRACEPOINT(cpu_migrate_current); \
    GATOR_HANDLE_TRACEPOINT(cpu_migrate_finish); \
    GATOR_HANDLE_TRACEPOINT(irq_handler_exit); \
    GATOR_HANDLE_TRACEPOINT(mali_hw_counter); \
    GATOR_HANDLE_TRACEPOINT(mali_job_slots_event); \
    GATOR_HANDLE_TRACEPOINT(mali_mmu_as_in_use); \
    GATOR_HANDLE_TRACEPOINT(mali_mmu_as_released); \
    GATOR_HANDLE_TRACEPOINT(mali_page_fault_insert_pages); \
    GATOR_HANDLE_TRACEPOINT(mali_pm_status); \
    GATOR_HANDLE_TRACEPOINT(mali_sw_counter); \
    GATOR_HANDLE_TRACEPOINT(mali_sw_counters); \
    GATOR_HANDLE_TRACEPOINT(mali_timeline_event); \
    GATOR_HANDLE_TRACEPOINT(mali_total_alloc_pages_change); \
    GATOR_HANDLE_TRACEPOINT(mm_page_alloc); \
    GATOR_HANDLE_TRACEPOINT(mm_page_free); \
    GATOR_HANDLE_TRACEPOINT(mm_page_free_batched); \
    GATOR_HANDLE_TRACEPOINT(sched_process_exec); \
    GATOR_HANDLE_TRACEPOINT(sched_process_fork); \
    GATOR_HANDLE_TRACEPOINT(sched_process_free); \
    GATOR_HANDLE_TRACEPOINT(sched_switch); \
    GATOR_HANDLE_TRACEPOINT(softirq_exit); \
    GATOR_HANDLE_TRACEPOINT(task_rename); \

#define GATOR_HANDLE_TRACEPOINT(probe_name) \
    struct tracepoint *gator_tracepoint_##probe_name
GATOR_TRACEPOINTS;
#undef GATOR_HANDLE_TRACEPOINT

static void gator_save_tracepoint(struct tracepoint *tp, void *priv)
{
    pr_debug("gator: gator_save_tracepoint(%s)\n", tp->name);
#define GATOR_HANDLE_TRACEPOINT(probe_name) \
    do { \
        if (strcmp(tp->name, #probe_name) == 0) { \
            gator_tracepoint_##probe_name = tp; \
            return; \
        } \
    } while (0)
GATOR_TRACEPOINTS;
#undef GATOR_HANDLE_TRACEPOINT
}

#if defined(CONFIG_MODULES)

static void gator_unsave_tracepoint(struct tracepoint *tp, void *priv)
{
    pr_debug("gator: gator_unsave_tracepoint(%s)\n", tp->name);
#define GATOR_HANDLE_TRACEPOINT(probe_name) \
    do { \
        if (strcmp(tp->name, #probe_name) == 0) { \
            gator_tracepoint_##probe_name = NULL; \
            return; \
        } \
    } while (0)
GATOR_TRACEPOINTS;
#undef GATOR_HANDLE_TRACEPOINT
}

int gator_new_tracepoint_module(struct notifier_block * nb, unsigned long action, void * data)
{
    struct tp_module * tp_mod = (struct tp_module *) data;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
    tracepoint_ptr_t * begin = tp_mod->mod->tracepoints_ptrs;
    tracepoint_ptr_t * end = tp_mod->mod->tracepoints_ptrs + tp_mod->mod->num_tracepoints;
#else
    struct tracepoint * const * begin = tp_mod->mod->tracepoints_ptrs;
    struct tracepoint * const * end = tp_mod->mod->tracepoints_ptrs + tp_mod->mod->num_tracepoints;
#endif

    pr_debug("gator: new tracepoint module registered %s\n", tp_mod->mod->name);

    if (action == MODULE_STATE_COMING)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	gator_for_each_tracepoint_range(begin, end, gator_save_tracepoint, NULL);
#else
        for (; begin != end; ++begin) {
            gator_save_tracepoint(*begin, NULL);
        }
#endif
    }
    else if (action == MODULE_STATE_GOING)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
        gator_for_each_tracepoint_range(begin, end, gator_unsave_tracepoint, NULL);
#else
        for (; begin != end; ++begin) {
            gator_unsave_tracepoint(*begin, NULL);
        }
#endif
    }
    else
    {
        pr_debug("gator: unexpected action value in gator_new_tracepoint_module: 0x%lx\n", action);
    }

    return 0;
}

static struct notifier_block tracepoint_notifier_block = {
    .notifier_call = gator_new_tracepoint_module
};

#endif

#endif


/* The user may define 'CONFIG_GATOR_DO_NOT_ONLINE_CORES_AT_STARTUP' to prevent offline cores from being
 * started when gator is loaded, or alternatively they can set 'disable_cpu_onlining' when module is loaded */
#if defined(CONFIG_SMP) && !defined(CONFIG_GATOR_DO_NOT_ONLINE_CORES_AT_STARTUP)
MODULE_PARM_DESC(disable_cpu_onlining, "Do not online all cores when module starts");
static bool disable_cpu_onlining;
module_param(disable_cpu_onlining, bool, 0644);
#endif

static int __init gator_module_init(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
    /* scan kernel built in tracepoints */
    for_each_kernel_tracepoint(gator_save_tracepoint, NULL);
#if defined(CONFIG_MODULES)
    /* register for notification of new tracepoint modules */
    register_tracepoint_module_notifier(&tracepoint_notifier_block);
#endif
#endif

    if (gatorfs_register())
        return -1;

    if (gator_init()) {
        gatorfs_unregister();
        return -1;
    }

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0))
    timer_setup(&gator_buffer_wake_up_timer, gator_buffer_wake_up, TIMER_DEFERRABLE);
#else
    setup_deferrable_timer_on_stack(&gator_buffer_wake_up_timer, gator_buffer_wake_up, 0);
#endif

#if defined(CONFIG_SMP) && !defined(CONFIG_GATOR_DO_NOT_ONLINE_CORES_AT_STARTUP)
    /* Online all cores */
    if (!disable_cpu_onlining) {
        int cpu;
        for_each_present_cpu(cpu) {
            if (!cpu_online(cpu)) {
                pr_notice("gator: Onlining cpu %i\n", cpu);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)
                add_cpu(cpu);
#else
                cpu_up(cpu);
#endif
            }
        }
    }
#endif

    /* Initialize the list of cpuids */
    memset(gator_cpuids, -1, sizeof(gator_cpuids));
    on_each_cpu(gator_read_cpuid, NULL, 1);

    return 0;
}

static void __exit gator_module_exit(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)) && defined(CONFIG_MODULES)
    /* unregister for notification of new tracepoint modules */
    unregister_tracepoint_module_notifier(&tracepoint_notifier_block);
#endif

    del_timer_sync(&gator_buffer_wake_up_timer);
    tracepoint_synchronize_unregister();
    gator_exit();
    gatorfs_unregister();
    gator_pmu_exit();
}

module_init(gator_module_init);
module_exit(gator_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arm Ltd");
MODULE_DESCRIPTION("Gator system profiler");
#define STRIFY2(ARG) #ARG
#define STRIFY(ARG) STRIFY2(ARG)
MODULE_VERSION(STRIFY(PROTOCOL_VERSION));
