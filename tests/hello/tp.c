/*
 * Copyright (C) 2009  Pierre-Marc Fournier
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

#include <ust/marker.h>
#include "usterr.h"

#define TRACEPOINT_CREATE_PROBES
#include "tp.h"

struct hello_trace_struct {
	char *message;
};

struct hello_trace_struct hello_struct = {
	.message = "ehlo\n",
};

void tptest_probe(void *data, int anint)
{
	struct hello_trace_struct *hello;
	hello = (struct hello_trace_struct *)data;
	DBG("in tracepoint probe...");
	printf("this is the message: %s\n", hello->message);
}

void tptest2_probe(void *data)
{
}

static void __attribute__((constructor)) init()
{
	DBG("connecting tracepoint...\n");
	/*
	 * Note: this is an internal API that will be used within
	 * TRACEPOINT_EVENT only eventually.
	 */
	__register_tracepoint(hello_tptest, tptest_probe, &hello_struct);
	__register_tracepoint(hello_tptest2, tptest2_probe, &hello_struct);
}
