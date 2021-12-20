/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef _MTK_FTRACE_H
#define  _MTK_FTRACE_H

#include <linux/string.h>
#include <linux/seq_file.h>

#ifdef CONFIG_MTK_KERNEL_MARKER
void mt_kernel_trace_begin(char *name);
void mt_kernel_trace_counter(char *name, int count);
void mt_kernel_trace_end(void);
#else
#define mt_kernel_trace_begin(name)
#define mt_kernel_trace_counter(name, count)
#define mt_kernel_trace_end()
#endif

#if defined(CONFIG_MTK_HIBERNATION) && defined(CONFIG_MTK_SCHED_TRACERS)
int resize_ring_buffer_for_hibernation(int enable);
#else
#define resize_ring_buffer_for_hibernation(on) (0)
#endif				/* CONFIG_MTK_HIBERNATION */

extern bool ring_buffer_expanded;
ssize_t tracing_resize_ring_buffer(struct trace_array *tr,
				   unsigned long size, int cpu_id);

#ifdef CONFIG_MTK_SCHED_TRACERS
void print_enabled_events(struct seq_file *m);
#else
#define print_enabled_events(m)
#endif/* CONFIG_TRACING && CONFIG_MTK_SCHED_TRACERS */
#endif