/*
 * Copyright (C) 2009 Novell Inc.
 *
 * Author: Jan Blunck <jblunck@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free  Software Foundation.
 */

#include <ust/marker.h>
#include <ust/tracepoint.h>

/* FIXME: We have to define at least one trace_mark and
 * one tracepoint here. If we don't, the __start... and
 * __stop... symbols won't be defined and the constructors
 * won't be compilable. We should find a linker trick to
 * avoid this.
 */

DECLARE_TRACE(ust_dummytp, TPPROTO(void), TPARGS());
DEFINE_TRACE(ust_dummytp);

void dummy_libust_initializer_func(void)
{
	trace_mark(ust, dummymark, MARK_NOARGS);
	trace_ust_dummytp();
}

MARKER_LIB;
TRACEPOINT_LIB;
