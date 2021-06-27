// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <diffball/defs.h>
#include <string.h>
#include <cfile.h>
#include <diffball/bit-functions.h>
#include <diffball/xdelta1.h>

unsigned int
check_xdelta1_magic(cfile *patchf)
{
	unsigned char buff[XDELTA_MAGIC_LEN];
	cseek(patchf, 0, CSEEK_FSTART);
	if (XDELTA_MAGIC_LEN != cread(patchf, buff, XDELTA_MAGIC_LEN))
	{
		return 0;
	}
	if (memcmp(buff, XDELTA_110_MAGIC, XDELTA_MAGIC_LEN) == 0)
	{
		return 2;
	}
	else if (memcmp(buff, XDELTA_104_MAGIC, XDELTA_MAGIC_LEN) == 0)
	{
		return 1;
	}
	else if (memcmp(buff, XDELTA_100_MAGIC, XDELTA_MAGIC_LEN) == 0)
	{
		return 1;
	}
	else if (memcmp(buff, XDELTA_020_MAGIC, XDELTA_MAGIC_LEN) == 0)
	{
		return 1;
	}
	else if (memcmp(buff, XDELTA_018_MAGIC, XDELTA_MAGIC_LEN) == 0)
	{
		return 1;
	}
	else if (memcmp(buff, XDELTA_014_MAGIC, XDELTA_MAGIC_LEN) == 0)
	{
		return 1;
	}
	return 0;
}

signed int
xdelta1EncodeDCBuffer(CommandBuffer *buffer, unsigned int version,
					  cfile *out_cfh)
{
	return -1;
}

signed int
xdelta1ReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff,
						 unsigned int version)
{
	cfile *add_cfh = NULL, *ctrl_cfh = NULL;
	unsigned long control_offset, control_end, flags;
	unsigned long len, offset, x, count, proc_count;
	unsigned long add_start, add_pos;
	unsigned char buff[32];
	EDCB_SRC_ID ref_id, add_id;
	unsigned char add_is_sequential, copy_is_sequential;
	cseek(patchf, XDELTA_MAGIC_LEN, CSEEK_FSTART);
	cread(patchf, buff, 4);
	flags = readUBytesBE(buff, 4);
	cread(patchf, buff, 4);

	dcbuff->ver_size = 0;

	// the header is 32 bytes, then 2 word's, each the length of the
	// src/trg file name.
	add_start = 32 + readUBytesBE(buff, 2) + readUBytesBE(buff + 2, 2);
	cseek(patchf, -12, CSEEK_END);
	control_end = ctell(patchf, CSEEK_FSTART);
	cread(patchf, buff, 4);
	control_offset = readUBytesBE(buff, 4);

	cseek(patchf, control_offset, CSEEK_FSTART);

	if (flags & XD_COMPRESSED_FLAG)
	{
		dcb_lprintf(2, "compressed segments detected\n");
		if ((ctrl_cfh = (cfile *)calloc(1, sizeof(cfile))) == NULL)
		{
			return MEM_ERROR;
		}
		copen_child_cfh(ctrl_cfh, patchf, control_offset, control_end,
						GZIP_COMPRESSOR, CFILE_RONLY);
	}
	else
	{
		ctrl_cfh = patchf;
	}

	/* kludge. skipping 8 byte unknown, and to_file md5.*/
	cseek(ctrl_cfh, 24, CSEEK_CUR);

	/* read the frigging to length, since it's variable */
	x = creadHighBitVariableIntLE(ctrl_cfh);
	dcb_lprintf(2, "to_len(%lu)\n", x);

	/* two bytes here I don't know about... */
	cseek(ctrl_cfh, 2, CSEEK_CUR);
	/* get and skip the segment name's len and md5 */
	x = creadHighBitVariableIntLE(ctrl_cfh);

	cseek(ctrl_cfh, x + 16, CSEEK_CUR);

	/* read the damned segment patch len. */
	x = creadHighBitVariableIntLE(ctrl_cfh);

	// skip the seq/has data bytes
	// handle sequential/has_data info
	cread(ctrl_cfh, buff, 2);
	add_is_sequential = buff[1];

	dcb_lprintf(2, "patch sequential? (%u)\n", add_is_sequential);

	/* get and skip the next segment name len and md5. */
	x = creadHighBitVariableIntLE(ctrl_cfh);
	cseek(ctrl_cfh, x + 16, CSEEK_CUR);

	/* read the damned segment patch len. */
	x = creadHighBitVariableIntLE(ctrl_cfh);
	dcb_lprintf(2, "seg2_len(%lu)\n", x);

	/* handle sequential/has_data */
	cread(ctrl_cfh, buff, 2);
	copy_is_sequential = buff[1];
	dcb_lprintf(2, "copy is sequential? (%u)\n", copy_is_sequential);
	/* next get the number of instructions (eg copy | adds) */
	count = creadHighBitVariableIntLE(ctrl_cfh);
	proc_count = 0;
	/* so starts the commands... */
	dcb_lprintf(2, "supposedly %lu commands...\nstarting command processing at %zi\n",
			 count, ctell(ctrl_cfh, CSEEK_FSTART));
	if (flags & XD_COMPRESSED_FLAG)
	{
		add_pos = 0;
		if ((add_cfh = (cfile *)calloc(1, sizeof(cfile))) == NULL)
		{
			return MEM_ERROR;
		}
		int err = copen_child_cfh(add_cfh, patchf, add_start, control_offset,
								  GZIP_COMPRESSOR, CFILE_RONLY);
		if (err)
		{
			eprintf("Nonzero exit code opening the compressed segment of this xdelta patch: %i\n", err);
			return err;
		}
		add_id = DCB_REGISTER_ADD_SRC(dcbuff, add_cfh, NULL, 1);
	}
	else
	{
		add_pos = add_start;
		add_id = DCB_REGISTER_VOLATILE_ADD_SRC(dcbuff, patchf, NULL, 0);
	}
	ref_id = src_id;
	while (proc_count++ != count)
	{
		x = creadHighBitVariableIntLE(ctrl_cfh);
		offset = creadHighBitVariableIntLE(ctrl_cfh);
		len = creadHighBitVariableIntLE(ctrl_cfh);
		if (x == XD_INDEX_COPY)
		{
			DCB_add_copy(dcbuff, offset, 0, len, ref_id);
		}
		else
		{
			if (add_is_sequential != 0)
			{
				offset += add_pos;
				add_pos += len;
			}
			else
			{
				offset += add_pos;
			}
			DCB_add_add(dcbuff, offset, len, add_id);
		}
	}
	dcb_lprintf(2, "finishing position was %zi\n", ctell(ctrl_cfh, CSEEK_FSTART));
	dcb_lprintf(2, "processed %lu of %lu commands\n", proc_count, count);
	dcbuff->ver_size = dcbuff->reconstruct_pos;
	if (flags & XD_COMPRESSED_FLAG)
	{
		cclose(ctrl_cfh);
		free(ctrl_cfh);
	}
	return 0;
}
