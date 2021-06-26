// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2005 Brian Harring <ferringb@gmail.com>
#include <cfile.h>
#include <diffball/dcbuffer.h>

unsigned long
default_dcb_src_cfh_read_func(u_dcb_src usrc, unsigned long src_pos,
							  unsigned char *buf, unsigned long len)
{
	if (src_pos != cseek(usrc.cfh, src_pos, CSEEK_FSTART))
	{
		return 0;
	}
	return cread(usrc.cfh, buf, len);
}

unsigned long
default_dcb_src_cfh_copy_func(DCommand *dc, cfile *out_cfh)
{
	return copy_cfile_block(out_cfh, dc->dcb_src->src_ptr.cfh,
							(unsigned long)dc->data.src_pos, dc->data.len);
}
