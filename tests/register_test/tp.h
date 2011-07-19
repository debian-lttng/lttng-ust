#undef TRACE_SYSTEM
#define TRACE_SYSTEM tp

#if !defined(_TRACE_TP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TP_H

/*
 * Copyright (C) 2011  Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <ust/tracepoint.h>

TRACEPOINT_EVENT(hello_tptest,
		 TP_PROTO(int anint),
		 TP_ARGS(anint),
		 TP_FIELDS());

#endif /* _TRACE_TP_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE tp

/* This part must be outside protection */
#include <ust/tracepoint_event.h>
