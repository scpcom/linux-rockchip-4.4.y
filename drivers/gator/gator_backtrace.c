/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator_builtin.h"

#if defined(__arm__) || defined(__aarch64__)
#define GATOR_KERNEL_UNWINDING                      1
#define GATOR_USER_UNWINDING                        1
#else
#define GATOR_KERNEL_UNWINDING                      0
#define GATOR_USER_UNWINDING                        0
#endif

/* on 4.10 walk_stackframe was unexported so use save_stack_trace instead */
#if defined(MODULE) && defined(__aarch64__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#define GATOR_KERNEL_UNWINDING_USE_WALK_STACKFRAME  0
#else
#define GATOR_KERNEL_UNWINDING_USE_WALK_STACKFRAME  1
#endif

#if (!GATOR_KERNEL_UNWINDING_USE_WALK_STACKFRAME) && !defined(CONFIG_STACKTRACE)
#error "CONFIG_STACKTRACE is required for kernel unwinding"
#endif

/* Uncomment the following line to enable kernel stack unwinding within gator, note it can also be defined from the Makefile */
/* #define GATOR_KERNEL_STACK_UNWINDING */

#if GATOR_KERNEL_UNWINDING

#include <asm/stacktrace.h>
#if !GATOR_KERNEL_UNWINDING_USE_WALK_STACKFRAME
#include <linux/stacktrace.h>
#endif

#if !defined(GATOR_KERNEL_STACK_UNWINDING)
/* Disabled by default */
MODULE_PARM_DESC(kernel_stack_unwinding, "Allow kernel stack unwinding.");
static bool kernel_stack_unwinding;
module_param(kernel_stack_unwinding, bool, 0644);
#endif

#if GATOR_KERNEL_UNWINDING_USE_WALK_STACKFRAME

/* -------------------------- KERNEL UNWINDING USING walk_stackframe -------------------------- */

static int report_trace(struct stackframe *frame, void *d)
{
    unsigned int *depth = d, cookie = NO_COOKIE;
    unsigned long addr = frame->pc;

    if (*depth) {
#if defined(MODULE)
        unsigned int cpu = get_physical_cpu();
        struct module *mod = __gator_module_address(addr);

        if (mod) {
            cookie = get_cookie(cpu, current, mod->name, false);
            addr = addr -
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
              (unsigned long)mod->module_core;
#else
              (unsigned long)mod->core_layout.base;
#endif
        }
#endif
        marshal_backtrace(addr & ~1, cookie, 1);
        (*depth)--;
    }

    return *depth == 0;
}

static void kernel_backtrace(int cpu, struct pt_regs *const regs)
{
#ifdef GATOR_KERNEL_STACK_UNWINDING
    int depth = gator_backtrace_depth;
#else
    int depth = (kernel_stack_unwinding ? gator_backtrace_depth : 1);
#endif
    struct stackframe frame;

    if (depth == 0)
        depth = 1;
#if defined(__arm__)
    frame.fp = regs->ARM_fp;
    frame.sp = regs->ARM_sp;
    frame.lr = regs->ARM_lr;
    frame.pc = regs->ARM_pc;
#else
    frame.fp = regs->regs[29];
    frame.sp = regs->sp;
    frame.pc = regs->pc;
#endif

#if defined(__aarch64__) && LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
    walk_stackframe(current, &frame, report_trace, &depth);
#else
    walk_stackframe(&frame, report_trace, &depth);
#endif
}

#else /* GATOR_KERNEL_UNWINDING_USE_WALK_STACKFRAME */

/* -------------------------- KERNEL UNWINDING USING save_stack_trace_regs -------------------------- */

#define GATOR_KMOD_STACK_MAX_SIZE 32
struct gator_kmod_stack { unsigned long addresses[GATOR_KMOD_STACK_MAX_SIZE]; };
static DEFINE_PER_CPU(struct gator_kmod_stack, gator_kmod_stack);

static void report_trace(unsigned int cpu, unsigned long *entries, unsigned int nr_entries)
{
    unsigned int cookie = NO_COOKIE;
    unsigned int index;

    for (index = 0; index < nr_entries; ++index) {
        unsigned long addr = entries[index];
#if defined(MODULE)
        struct module * mod = __gator_module_address(addr);
        if (mod) {
            cookie = get_cookie(cpu, current, mod->name, false);
            addr = addr -
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
              (unsigned long) mod->module_core;
#else
              (unsigned long) mod->core_layout.base;
#endif
        }
#endif
        marshal_backtrace(addr & ~1, cookie, 1);
    }
}

static void kernel_backtrace(int cpu, struct pt_regs *const regs)
{
    unsigned int max_entries;
    unsigned int stack_len;
    unsigned long *entries;
#ifndef CONFIG_ARCH_STACKWALK
    struct stack_trace trace;
    trace.skip = 0;
    trace.nr_entries = 0;
#endif
    entries = per_cpu(gator_kmod_stack, cpu).addresses;
#ifdef GATOR_KERNEL_STACK_UNWINDING
    max_entries = gator_backtrace_depth;
#else
    max_entries = (kernel_stack_unwinding ? gator_backtrace_depth : 1);
#endif
    if (max_entries < 1) {
        max_entries = 1;
    }
    else if (max_entries > GATOR_KMOD_STACK_MAX_SIZE) {
        max_entries = GATOR_KMOD_STACK_MAX_SIZE;
    }

#ifdef CONFIG_ARCH_STACKWALK
    stack_len = stack_trace_save(entries, max_entries, 0);
#else
    trace.entries = entries;
    trace.max_entries = max_entries;
    save_stack_trace(&trace);
    stack_len = trace->nr_entries;
#endif

    report_trace(cpu, entries, stack_len);
}


#endif

#else /* GATOR_KERNEL_UNWINDING */

/* -------------------------- NO KERNEL UNWINDING -------------------------- */

static void kernel_backtrace(int cpu, struct pt_regs *const regs)
{
    marshal_backtrace(PC_REG & ~1, NO_COOKIE, 1);
}

#endif /* GATOR_KERNEL_UNWINDING */


/* -------------------------- USER SPACE UNWINDING -------------------------- */

static void gator_add_trace(int cpu, unsigned long address)
{
    off_t offset = 0;
    unsigned long cookie = get_address_cookie(cpu, current, address & ~1, &offset);

    if (cookie == NO_COOKIE || cookie == UNRESOLVED_COOKIE)
        offset = address;

    marshal_backtrace(offset & ~1, cookie, 0);
}

#if GATOR_USER_UNWINDING

/*
 * EABI backtrace stores {fp,lr} on the stack.
 */
struct stack_frame_eabi {
    union {
        struct {
            unsigned long fp;
            /* May be the fp in the case of a leaf function or clang */
            unsigned long lr;
            /* If lr is really the fp, lr2 is the corresponding lr */
            unsigned long lr2;
        };
        /* Used to read 32 bit fp/lr from a 64 bit kernel */
        struct {
            u32 fp_32;
            /* same as lr above */
            u32 lr_32;
            /* same as lr2 above */
            u32 lr2_32;
        };
    };
};

static void arm_backtrace_eabi(int cpu, struct pt_regs *const regs, unsigned int depth)
{
    struct stack_frame_eabi *curr;
    struct stack_frame_eabi bufcurr;
#if defined(__arm__)
    const bool is_compat = false;
    unsigned long fp = regs->ARM_fp;
    unsigned long sp = regs->ARM_sp;
    unsigned long lr = regs->ARM_lr;
    const int gcc_frame_offset = sizeof(unsigned long);
#else
    /* Is userspace aarch32 (32 bit) */
    const bool is_compat = compat_user_mode(regs);
    unsigned long fp = (is_compat ? regs->regs[11] : regs->regs[29]);
    unsigned long sp = (is_compat ? regs->compat_sp : regs->sp);
    unsigned long lr = (is_compat ? regs->compat_lr : regs->regs[30]);
    const int gcc_frame_offset = (is_compat ? sizeof(u32) : 0);
#endif
    /* clang frame offset is always zero */
    int is_user_mode = user_mode(regs);

    /* pc (current function) has already been added */

    if (!is_user_mode)
        return;

    /* Add the lr (parent function), entry preamble may not have
     * executed
     */
    gator_add_trace(cpu, lr);

    /* check fp is valid */
    if (fp == 0 || fp < sp)
        return;

    /* Get the current stack frame */
    curr = (struct stack_frame_eabi *)(fp - gcc_frame_offset);
    if ((unsigned long)curr & 3)
        return;

    while (depth-- && curr) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
        if (!access_ok(curr, sizeof(struct stack_frame_eabi)) ||
#else
        if (!access_ok(VERIFY_READ, curr, sizeof(struct stack_frame_eabi)) ||
#endif
                __copy_from_user_inatomic(&bufcurr, curr, sizeof(struct stack_frame_eabi))) {
            return;
        }

        fp = (is_compat ? bufcurr.fp_32 : bufcurr.fp);
        lr = (is_compat ? bufcurr.lr_32 : bufcurr.lr);

#define calc_next(reg) ((reg) - gcc_frame_offset)
        /* Returns true if reg is a valid fp */
#define validate_next(reg, curr) \
        ((reg) != 0 && (calc_next(reg) & 3) == 0 && (unsigned long)(curr) < calc_next(reg))

        /* Try lr from the stack as the fp because gcc leaf functions do
         * not push lr. If gcc_frame_offset is non-zero, the lr will also
         * be the clang fp. This assumes code is at a lower address than
         * the stack
         */
        if (validate_next(lr, curr)) {
            fp = lr;
            lr = (is_compat ? bufcurr.lr2_32 : bufcurr.lr2);
        }

        gator_add_trace(cpu, lr);

        if (!validate_next(fp, curr))
            return;

        /* Move to the next stack frame */
        curr = (struct stack_frame_eabi *)calc_next(fp);
    }
}

#else /* GATOR_USER_UNWINDING */

static void arm_backtrace_eabi(int cpu, struct pt_regs *const regs, unsigned int depth)
{
    /* NO OP */
}

#endif /* GATOR_USER_UNWINDING */

/* -------------------------- STACK SAMPLING -------------------------- */

static void gator_add_sample(int cpu, struct pt_regs *const regs, u64 time)
{
    bool in_kernel;
    unsigned long exec_cookie;

    if (!regs)
        return;

    in_kernel = !user_mode(regs);
    exec_cookie = get_exec_cookie(cpu, current);

    if (!marshal_backtrace_header(exec_cookie, current->tgid, current->pid, time))
        return;

    if (in_kernel) {
        kernel_backtrace(cpu, regs);
    } else {
        /* Cookie+PC */
        gator_add_trace(cpu, PC_REG);

        /* Backtrace */
        if (gator_backtrace_depth)
            arm_backtrace_eabi(cpu, regs, gator_backtrace_depth);
    }

    marshal_backtrace_footer(time);
}
