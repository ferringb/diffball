// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2013 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <diffball/defs.h>
#include <string.h>
#include <diffball/dcbuffer.h>
#include <diffball/switching.h>
#include <cfile.h>
#include <diffball/bit-functions.h>

unsigned int
check_switching_magic(cfile *patchf)
{
	unsigned char buff[SWITCHING_MAGIC_LEN + 1];
	cseek(patchf, 0, CSEEK_FSTART);
	if (SWITCHING_MAGIC_LEN != cread(patchf, buff, SWITCHING_MAGIC_LEN))
	{
		return 0;
	}
	else if (memcmp(buff, SWITCHING_MAGIC, SWITCHING_MAGIC_LEN) != 0)
	{
		return 0;
	}
	if (SWITCHING_VERSION_LEN != cread(patchf, buff, SWITCHING_VERSION_LEN))
	{
		return 0;
	}
	else if (readUBytesBE(buff, SWITCHING_VERSION_LEN) == SWITCHING_VERSION)
	{
		return 2;
	}
	return 1;
}

const unsigned long add_len_start[] = {
	0x0,
	0x40,
	0x40 + 0x4000,
	0x40 + 0x4000 + 0x400000};
const unsigned long copy_len_start[] = {
	0x0,
	0x10,
	0x10 + 0x1000,
	0x10 + 0x1000 + 0x100000};
const unsigned long copy_off_start[] = {
	0x00,
	0x100,
	0x100 + 0x10000,
	0x100 + 0x10000 + 0x1000000};
const unsigned long copy_soff_start[] = {
	0x00,
	0x80,
	0x80 + 0x8000,
	0x80 + 0x8000,
	0x80 + 0x8000 + 0x800000};

signed int switchingEncodeDCBuffer(CommandBuffer *buffer,
								   cfile *out_cfh)
{
	off_u64 fh_pos = 0;
	off_s64 s_off = 0;
	off_u64 u_off = 0;
	off_u32 delta_pos = 0, dc_pos = 0;
	unsigned int lb;
	signed int count;
	unsigned int x, commands_processed;
	unsigned char out_buff[256];
	off_u64 temp_len;
	DCommand_collapsed dcc;
	off_u32 total_add_len = 0;
	unsigned int last_com, temp;
	unsigned int is_neg = 0;
	unsigned const long *copy_off_array;
	unsigned int offset_type = ENCODING_OFFSET_DC_POS;
	if (offset_type == ENCODING_OFFSET_DC_POS)
	{
		copy_off_array = copy_soff_start;
	}
	else
	{
		copy_off_array = copy_off_start;
	}
	if (init_DCommand_collapsed(&dcc))
	{
		return MEM_ERROR;
	}

	DCBufferReset(buffer);
	if (SWITCHING_MAGIC_LEN != cwrite(out_cfh, SWITCHING_MAGIC, SWITCHING_MAGIC_LEN))
	{
		eprintf("Failed writing %i bytes to the patch file\n", SWITCHING_MAGIC_LEN);
		return IO_ERROR;
	}
	writeUBytesBE(out_buff, SWITCHING_VERSION, SWITCHING_VERSION_LEN);
	if (SWITCHING_VERSION_LEN != cwrite(out_cfh, out_buff, SWITCHING_VERSION_LEN))
	{
		eprintf("Failed writing %i bytes to the patch file\n", SWITCHING_VERSION_LEN);
		return IO_ERROR;
	}
	delta_pos += SWITCHING_MAGIC_LEN + SWITCHING_VERSION_LEN;
	total_add_len = 0;

	while ((count = DCB_get_next_collapsed_command(buffer, &dcc)) > 0)
	{
		if (DC_ADD == dcc.commands[0].type)
			total_add_len += dcc.len;
	}

	// this looks wrong.
	if (count != 0)
		return count;

	writeUBytesBE(out_buff, total_add_len, 4);
	cwrite(out_cfh, out_buff, 4);
	delta_pos += 4;

	DCBufferReset(buffer);

	while ((count = DCB_get_next_collapsed_command(buffer, &dcc)) > 0)
	{
		if (DC_ADD == dcc.commands[0].type)
		{
			for (x = 0; x < count; x++)
			{
				if (dcc.commands[x].data.len != copyDCB_add_src(buffer, dcc.commands + x, out_cfh))
				{
					return EOF_ERROR;
				}
			}
			delta_pos += dcc.len;
		}
	}

	// as does this.
	if (count < 0)
		return count;

	//	v1printf("output add block, len(%u)\n", delta_pos);
	DCBufferReset(buffer);
	last_com = DC_COPY;
	dc_pos = 0;

	commands_processed = 0;
	count = DCB_get_next_collapsed_command(buffer, &dcc);
	while (commands_processed < count)
	{
		if (DC_ADD == dcc.commands[0].type)
		{
			temp_len = dcc.len;
			if (temp_len >= add_len_start[3])
			{
				temp = 3;
				lb = 30;
			}
			else if (temp_len >= add_len_start[2])
			{
				temp = 2;
				lb = 22;
			}
			else if (temp_len >= add_len_start[1])
			{
				temp = 1;
				lb = 14;
			}
			else
			{
				temp = 0;
				lb = 6;
			}
			temp_len -= add_len_start[temp];
			writeUBitsBE(out_buff, temp_len, lb);
			out_buff[0] |= (temp << 6);
			if (temp + 1 != cwrite(out_cfh, out_buff, temp + 1))
			{
				eprintf("Failed writing %u to the patch fileu\n", temp + 1);
				return IO_ERROR;
			}
			v2printf("writing add, pos(%u), len(%u)\n", delta_pos, dcc.len);
			delta_pos += temp + 1;
			fh_pos += dcc.len;
			last_com = DC_ADD;
			commands_processed = count;
		}
		else
		{
			if (last_com == DC_COPY)
			{
				v2printf("last command was a copy, outputing blank add\n");
				out_buff[0] = 0;
				if (1 != cwrite(out_cfh, out_buff, 1))
				{
					eprintf("Failed writing to the patch file\n");
					return IO_ERROR;
				}
				delta_pos++;
			}

			//yes this is a hack.  but it works.
			if (offset_type == ENCODING_OFFSET_DC_POS)
			{
				s_off = dcc.commands[commands_processed].data.src_pos - dc_pos;
				u_off = abs(s_off);
				v2printf("off(%llu), dc_pos(%u), u_off(%llu), s_off(%lld): ",
						 (act_off_u64)dcc.commands[commands_processed].data.src_pos,
						 dc_pos, (act_off_u64)u_off, (act_off_s64)s_off);
			}
			else
			{
				u_off = dcc.commands[commands_processed].data.src_pos;
			}
			temp_len = dcc.commands[commands_processed].data.len;
			if (temp_len >= copy_len_start[3])
			{
				temp = 3;
				lb = 28;
			}
			else if (temp_len >= copy_len_start[2])
			{
				temp = 2;
				lb = 20;
			}
			else if (temp_len >= copy_len_start[1])
			{
				temp = 1;
				lb = 12;
			}
			else
			{
				temp = 0;
				lb = 4;
			}
			temp_len -= copy_len_start[temp];
			writeUBitsBE(out_buff, temp_len, lb);
			out_buff[0] |= (temp << 6);
			lb = temp + 1;

			if (u_off >= copy_off_array[3])
			{
				temp = 3;
			}
			else if (u_off >= copy_off_array[2])
			{
				temp = 2;
			}
			else if (u_off >= copy_off_array[1])
			{
				temp = 1;
			}
			else
			{
				temp = 0;
			}
			out_buff[0] |= (temp << 4);
			if (offset_type == ENCODING_OFFSET_DC_POS)
			{
				dc_pos += s_off;
				if (temp)
				{
					if (s_off > 0)
					{
						s_off -= copy_off_array[temp];
						is_neg = 0;
					}
					else
					{
						s_off += copy_off_array[temp];
						is_neg = 1;
					}
				}
				writeSBytesBE(out_buff + lb, s_off, temp + 1);
				if (is_neg)
					out_buff[lb] |= 0x80;
				is_neg = 0;
			}
			else
			{
				u_off -= copy_off_array[temp];
				writeUBytesBE(out_buff + lb, u_off, temp + 1);
			}
			if (lb + temp + 1 != cwrite(out_cfh, out_buff, lb + temp + 1))
			{
				eprintf("Failed writing to the patch file\n");
				return IO_ERROR;
			}
			v2printf("writing copy delta_pos(%u), fh_pos(%llu), offset(%lld), len(%u)\n",
					 delta_pos, (act_off_u64)fh_pos, (act_off_s64)ENCODING_OFFSET_DC_POS,
					 dcc.commands[commands_processed].data.len);
			fh_pos += dcc.commands[commands_processed].data.len;
			delta_pos += lb + temp + 1;
			last_com = DC_COPY;
		}
		commands_processed++;
		if (commands_processed >= count)
		{
			count = DCB_get_next_collapsed_command(buffer, &dcc);
			commands_processed = 0;
		}
	}
	free_DCommand_collapsed(&dcc);
	writeUBytesBE(out_buff, 0, 2);
	if (last_com == DC_COPY)
	{
		if (1 != cwrite(out_cfh, out_buff, 1))
		{
			eprintf("Failed writing to the patch file\n");
			return IO_ERROR;
		}
		delta_pos++;
	}
	if (2 != cwrite(out_cfh, out_buff, 2))
	{
		eprintf("Failed writing to the patch file\n");
		return IO_ERROR;
	}
	delta_pos += 2;
	return 0;
}

signed int
switchingReconstructDCBuff(DCB_SRC_ID src_id, cfile *patchf, CommandBuffer *dcbuff)
{
	const unsigned int buff_size = 4096;
	unsigned char buff[buff_size];
	EDCB_SRC_ID ref_id, add_id;
	off_u32 len;
	off_u32 dc_pos = 0;
	off_u64 u_off;
	off_s64 s_off;
	off_u32 last_com;
	off_u32 add_off, com_start;
	unsigned int ob, lb;
	unsigned int end_of_patch = 0;
	unsigned const long *copy_off_array;
	unsigned int offset_type = ENCODING_OFFSET_DC_POS;

	dcbuff->ver_size = 0;

	if (offset_type == ENCODING_OFFSET_DC_POS)
	{
		v2printf("using ENCODING_OFFSET_DC_POS\n");
		copy_off_array = copy_soff_start;
	}
	else if (offset_type == ENCODING_OFFSET_START)
	{
		v2printf("using ENCODING_OFFSET_START\n");
		copy_off_array = copy_off_start;
	}
	else
	{
		return FORMAT_ERROR;
	}
	cseek(patchf, SWITCHING_MAGIC_LEN + SWITCHING_VERSION_LEN, CSEEK_FSTART);
	assert(ctell(patchf, CSEEK_FSTART) == SWITCHING_MAGIC_LEN +
											  SWITCHING_VERSION_LEN);
	dc_pos = 0;
	v2printf("starting pos=%llu\n", (act_off_u64)ctell(patchf, CSEEK_ABS));
	cread(patchf, buff, 4);
	com_start = readUBytesBE(buff, 4);
	cseek(patchf, com_start, CSEEK_CUR);
	add_off = SWITCHING_MAGIC_LEN + SWITCHING_VERSION_LEN + 4;
	last_com = DC_COPY;
	add_id = DCB_REGISTER_VOLATILE_ADD_SRC(dcbuff, patchf, NULL, 0);
	if (add_id < 0)
	{
		eprintf("Internal error: failed registering the patch into the command buffer: %i\n", add_id);
		return add_id;
	}
	ref_id = src_id;
	v2printf("add data block size(%u), starting commands at pos(%u)\n", com_start,
			 (off_u32)ctell(patchf, CSEEK_ABS));

	while (end_of_patch == 0 && cread(patchf, buff, 1) == 1)
	{
		v2printf("processing(%u) at pos(%u): ", buff[0], (off_u32)ctell(patchf, CSEEK_ABS) - 1);
		if (last_com != DC_ADD)
		{
			lb = (buff[0] >> 6) & 0x3;
			len = buff[0] & 0x3f;
			if (lb)
			{
				cread(patchf, buff, lb);
				len = (len << (lb * 8)) + readUBytesBE(buff, lb);
				len += add_len_start[lb];
			}
			if (len)
			{
				DCB_add_add(dcbuff, add_off, len, add_id);
				add_off += len;
			}
			last_com = DC_ADD;
			v2printf("add len(%u)\n", len);
		}
		else if (last_com != DC_COPY)
		{
			lb = (buff[0] >> 6) & 0x3;
			ob = (buff[0] >> 4) & 0x3;
			len = buff[0] & 0x0f;
			if (lb)
			{
				cread(patchf, buff, lb);
				len = (len << (lb * 8)) + readUBytesBE(buff, lb);
				len += copy_len_start[lb];
			}
			v2printf("ob(%u): ", ob);
			cread(patchf, buff, ob + 1);
			if (offset_type == ENCODING_OFFSET_DC_POS)
			{
				s_off = readSBytesBE(buff, ob + 1);

				// positive or negative 0?  Yes, for this, there is a difference...
				if (buff[0] & 0x80)
				{
					s_off -= copy_off_array[ob];
				}
				else
				{
					s_off += copy_off_array[ob];
				}
				u_off = dc_pos + s_off;
				v2printf("u_off(%llu), dc_pos(%u), s_off(%lld): ", (act_off_u64)u_off, dc_pos, (act_off_s64)s_off);
				dc_pos = u_off;
			}
			else
			{
				u_off = readUBytesBE(buff, ob + 1);
				u_off += copy_off_start[ob];
			}
			if (lb == 0 && ob == 0 && len == 0 && u_off == 0)
			{
				v2printf("zero length, zero offset copy found.\n");
				end_of_patch = 1;
				continue;
			}
			if (len)
			{
				DCB_add_copy(dcbuff, u_off, 0, len, ref_id);
			}
			last_com = DC_COPY;
			v2printf("copy off(%llu), len(%u)\n", (act_off_u64)u_off, len);
		}
	}
	dcbuff->ver_size = dcbuff->reconstruct_pos;
	v2printf("closing command was (%u)\n", *buff);
	v2printf("cread fh_pos(%zi)\n", ctell(patchf, CSEEK_ABS));
	v2printf("ver_pos(%llu)\n", (act_off_u64)dcbuff->reconstruct_pos);

	return 0;
}
