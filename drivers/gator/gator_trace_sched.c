/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <trace/events/sched.h>
#include <trace/events/task.h>

#include "gator.h"

#define TASK_MAP_ENTRIES        1024    /* must be power of 2 */
#define TASK_MAX_COLLISIONS     2

enum {
    STATE_WAIT_ON_OTHER = 0,
    STATE_CONTENTION,
    STATE_WAIT_ON_IO,
    CPU_WAIT_TOTAL
};

static DEFINE_PER_CPU(uint64_t *, taskname_keys);
static DEFINE_PER_CPU(int, collecting);

/* this array is never read as the cpu charts are derived
 * counters the files are needed, nonetheless, to show that these
 * counters are available
 */
static const char *sched_wait_event_names[] = {
    "Linux_cpu_wait_contention",
    "Linux_cpu_wait_io",
};
static ulong sched_wait_enabled[ARRAY_SIZE(sched_wait_event_names)];
static ulong sched_wait_keys[ARRAY_SIZE(sched_wait_event_names)];

static const char *sched_activity_event_names[] = {
    "system",
    "user",
};
static ulong sched_activity_enabled[ARRAY_SIZE(sched_activity_event_names)][GATOR_CLUSTER_COUNT];
static ulong sched_activity_keys[ARRAY_SIZE(sched_activity_event_names)][GATOR_CLUSTER_COUNT];

static int sched_trace_create_files(struct super_block *sb, struct dentry *root)
{
    struct dentry *dir;
    int i;
    int j;
    char buf[40];

    for (i = 0; i < ARRAY_SIZE(sched_wait_event_names); ++i) {
        dir = gatorfs_mkdir(sb, root, sched_wait_event_names[i]);
        if (!dir)
            return -1;
        gatorfs_create_ulong(sb, dir, "enabled", &sched_wait_enabled[i]);
        gatorfs_create_ro_ulong(sb, dir, "key", &sched_wait_keys[i]);
    }

    for (i = 0; i < ARRAY_SIZE(sched_activity_event_names); ++i) {
        for (j = 0; j < gator_cluster_count; j++) {
            snprintf(buf, sizeof(buf), "%s_%s", gator_clusters[j]->pmnc_name, sched_activity_event_names[i]);
            dir = gatorfs_mkdir(sb, root, buf);
            if (!dir)
                return -1;
            gatorfs_create_ulong(sb, dir, "enabled", &sched_activity_enabled[i][j]);
            gatorfs_create_ro_ulong(sb, dir, "key", &sched_activity_keys[i][j]);
        }
    }

    return 0;
}

static void emit_pid_name(const char *comm, struct task_struct *task)
{
    bool found = false;
    char taskcomm[TASK_COMM_LEN + 3];
    unsigned long x, cpu = get_physical_cpu();
    uint64_t *keys = &(per_cpu(taskname_keys, cpu)[(task->pid & 0xFF) * TASK_MAX_COLLISIONS]);
    uint64_t value;

    value = gator_chksum_crc32(comm);
    value = (value << 32) | (uint32_t)task->pid;

    /* determine if the thread name was emitted already */
    for (x = 0; x < TASK_MAX_COLLISIONS; x++) {
        if (keys[x] == value) {
            found = true;
            break;
        }
    }

    if (!found) {
        /* shift values, new value always in front */
        uint64_t oldv, newv = value;

        for (x = 0; x < TASK_MAX_COLLISIONS; x++) {
            oldv = keys[x];
            keys[x] = newv;
            newv = oldv;
        }

        /* emit pid names, cannot use get_task_comm, as it's not exported on all kernel versions */
        if (strlcpy(taskcomm, comm, TASK_COMM_LEN) == TASK_COMM_LEN - 1) {
            /* append ellipses if comm has length of TASK_COMM_LEN - 1 */
            strcat(taskcomm, "...");
        }

        marshal_thread_name(task->pid, taskcomm);
    }
}

static void collect_counters(u64 time, struct task_struct *task, bool sched_switch)
{
    int *buffer, len, cpu = get_physical_cpu();
    long long *buffer64;
    struct gator_interface *gi;

    if (marshal_event_header(time)) {
        list_for_each_entry(gi, &gator_events, list) {
            if (gi->read) {
                len = gi->read(&buffer, sched_switch);
                if (len < 0)
                    pr_err("gator: read failed for %s\n", gi->name);
                marshal_event(len, buffer);
            } else if (gi->read64) {
                len = gi->read64(&buffer64, sched_switch);
                if (len < 0)
                    pr_err("gator: read64 failed for %s\n", gi->name);
                marshal_event64(len, buffer64);
            }
            if (gi->read_proc && task != NULL) {
                len = gi->read_proc(&buffer64, task);
                if (len < 0)
                    pr_err("gator: read_proc failed for %s\n", gi->name);
                marshal_event64(len, buffer64);
            }
        }
        if (cpu == 0)
            gator_emit_perf_time(time);
        /* Only check after writing all counters so that time and corresponding counters appear in the same frame */
        buffer_check(cpu, BLOCK_COUNTER_BUF, time);

        /* Commit buffers on timeout */
        if (gator_live_rate > 0 && time >= per_cpu(gator_buffer_commit_time, cpu)) {
            static const int buftypes[] = { NAME_BUF, COUNTER_BUF, BLOCK_COUNTER_BUF, SCHED_TRACE_BUF, ACTIVITY_BUF };
            int i;

            for (i = 0; i < ARRAY_SIZE(buftypes); ++i)
                gator_commit_buffer(cpu, buftypes[i], time);

            /* spinlocks are noops on uniprocessor machines and mutexes do
             * not work in sched_switch context in RT-Preempt full, so
             * disable proactive flushing of the annotate frame on
             * uniprocessor machines.
             */
#ifdef CONFIG_SMP
            /* Try to preemptively flush the annotate buffer to reduce the chance of the buffer being full */
            if (on_primary_core() && spin_trylock(&annotate_lock)) {
                gator_commit_buffer(0, ANNOTATE_BUF, time);
                spin_unlock(&annotate_lock);
            }
#endif
        }
    }
}

/* special case used during a suspend of the system */
static void trace_sched_insert_idle(void)
{
    marshal_sched_trace_switch(0, 0);
}

static void gator_trace_emit_link(struct task_struct *p)
{
    int cookie;
    int cpu = get_physical_cpu();

    cookie = get_exec_cookie(cpu, p);
    emit_pid_name(p->comm, p);

    marshal_link(cookie, p->tgid, p->pid);
}

GATOR_DEFINE_PROBE(sched_process_fork, TP_PROTO(struct task_struct *parent, struct task_struct *child))
{
    gator_trace_emit_link(child);
}

GATOR_DEFINE_PROBE(sched_process_exec, TP_PROTO(struct task_struct *p, pid_t old_pid, struct linux_binprm *bprm))
{
    gator_trace_emit_link(p);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
GATOR_DEFINE_PROBE(task_rename, TP_PROTO(struct task_struct *task, char *comm))
#else
GATOR_DEFINE_PROBE(task_rename, TP_PROTO(struct task_struct *task, const char *comm))
#endif
{
    emit_pid_name(comm, task);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct task_struct *prev, struct task_struct *next))
#else
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(bool preempt, struct task_struct *prev, struct task_struct *next))
#endif
{
    int state;
    int cpu = get_physical_cpu();

    per_cpu(in_scheduler_context, cpu) = true;

    /* do as much work as possible before disabling interrupts */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 14, 0)
    if (prev->state == TASK_RUNNING)
#else
    if (task_is_running(prev))
#endif
        state = STATE_CONTENTION;
    else if (prev->in_iowait)
        state = STATE_WAIT_ON_IO;
    else
        state = STATE_WAIT_ON_OTHER;

    per_cpu(collecting, cpu) = 1;
    collect_counters(gator_get_time(), prev, true);
    per_cpu(collecting, cpu) = 0;

    marshal_sched_trace_switch(next->pid, state);

    per_cpu(in_scheduler_context, cpu) = false;
}

GATOR_DEFINE_PROBE(sched_process_free, TP_PROTO(struct task_struct *p))
{
    marshal_sched_trace_exit(p->tgid, p->pid);
}

static void do_nothing(void *info)
{
    /* Intentionally do nothing */
    (void)info;
}

static int register_scheduler_tracepoints(void)
{
    /* register tracepoints */
    if (GATOR_REGISTER_TRACE(sched_process_fork))
        goto fail_sched_process_fork;
    if (GATOR_REGISTER_TRACE(sched_process_exec))
        goto fail_sched_process_exec;
    if (GATOR_REGISTER_TRACE(task_rename))
        goto fail_task_rename;
    if (GATOR_REGISTER_TRACE(sched_switch))
        goto fail_sched_switch;
    if (GATOR_REGISTER_TRACE(sched_process_free))
        goto fail_sched_process_free;
    pr_debug("gator: registered tracepoints\n");

    /* Now that the scheduler tracepoint is registered, force a context
     * switch on all cpus to capture what is currently running.
     */
    on_each_cpu(do_nothing, NULL, 0);

    return 0;

    /* unregister tracepoints on error */
fail_sched_process_free:
    GATOR_UNREGISTER_TRACE(sched_switch);
fail_sched_switch:
    GATOR_UNREGISTER_TRACE(task_rename);
fail_task_rename:
    GATOR_UNREGISTER_TRACE(sched_process_exec);
fail_sched_process_exec:
    GATOR_UNREGISTER_TRACE(sched_process_fork);
fail_sched_process_fork:
    pr_err("gator: tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

    return -1;
}

static void unregister_scheduler_tracepoints(void)
{
    GATOR_UNREGISTER_TRACE(sched_process_fork);
    GATOR_UNREGISTER_TRACE(sched_process_exec);
    GATOR_UNREGISTER_TRACE(task_rename);
    GATOR_UNREGISTER_TRACE(sched_switch);
    GATOR_UNREGISTER_TRACE(sched_process_free);
    pr_debug("gator: unregistered tracepoints\n");
}

static void gator_trace_sched_stop(void)
{
    int cpu;

    unregister_scheduler_tracepoints();

    for_each_present_cpu(cpu) {
        kfree(per_cpu(taskname_keys, cpu));
    }
}

static int gator_trace_sched_start(void)
{
    int cpu, size;
    int ret;

    for_each_present_cpu(cpu) {
        size = TASK_MAP_ENTRIES * TASK_MAX_COLLISIONS * sizeof(uint64_t);
        per_cpu(taskname_keys, cpu) = kmalloc(size, GFP_KERNEL);
        if (!per_cpu(taskname_keys, cpu))
            return -1;
        memset(per_cpu(taskname_keys, cpu), 0, size);
    }

    ret = register_scheduler_tracepoints();

    return ret;
}

static void gator_trace_sched_offline(void)
{
    trace_sched_insert_idle();
}

static void gator_trace_sched_init(void)
{
    int i;
    int j;

    for (i = 0; i < ARRAY_SIZE(sched_wait_enabled); i++) {
        sched_wait_enabled[i] = 0;
        sched_wait_keys[i] = gator_events_get_key();
    }

    for (i = 0; i < ARRAY_SIZE(sched_activity_enabled); i++) {
        for (j = 0; j < gator_cluster_count; j++) {
            sched_activity_enabled[i][j] = 0;
            sched_activity_keys[i][j] = gator_events_get_key();
        }
    }
}
