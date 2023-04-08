// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <diffball/defs.h>
#include <errno.h>
#include <string.h>
#include <diffball/adler32.h>
#include <diffball/diff-algs.h>
#include <diffball/hash.h>
#include <diffball/defs.h>
#include <diffball/bit-functions.h>

/* this is largely based on the algorithms detailed in randal burn's various papers.
   Obviously credit for the alg's go to him, although I'm the one who gets the dubious
   credit for bugs in the implementation of said algorithms... */

#define ERETURN(value)                                                                         \
	{                                                                                          \
		if ((value) != 0)                                                                      \
		{                                                                                      \
			eprintf("%s:%i Exiting due to nonzero return: %i\n", __FILE__, __LINE__, (value)); \
		};                                                                                     \
		return (value);                                                                        \
	}

signed int
OneHalfPassCorrecting(CommandBuffer *dcb, RefHash *rh, unsigned char rid, cfile *vcfh, unsigned char vid)
{
	ADLER32_SEED_CTX ads;
	off_u32 va, vs, vc, vm, rm, ver_len, len, ref_len, ver_start, ref_start;
	cfile_window *vcfw, *rcfw;
	unsigned long bad_match = 0, no_match = 0, good_match = 0;
	unsigned long hash_offset, x;
	int err;
	err = init_adler32_seed(&ads, rh->seed_len);
	if (err)
		ERETURN(err);
	va = vs = vc = 0;
	ver_len = cfile_len(vcfh);
	ver_start = cfile_start_offset(vcfh);
	ref_len = cfile_len(rh->ref_cfh);
	ref_start = cfile_start_offset(rh->ref_cfh);

	if (0 != cseek(vcfh, 0, CSEEK_FSTART))
	{
		ERETURN(IO_ERROR);
	}
	vcfw = expose_page(vcfh);
	if (vcfw->end == 0 && vcfw->offset != ver_len)
		ERETURN(IO_ERROR);

#define end_pos(x) ((x)->offset + (x)->end)
	while (vcfw != NULL)
	{
		if (va < vc)
		{
			va = vc;
		}
		if (va >= end_pos(vcfw))
		{
			vcfw = next_page(vcfh);
			if (vcfw == NULL)
			{
				if (vcfh->data.end + vcfh->data.offset != ver_len)
				{
					ERETURN(IO_ERROR);
				}
				continue;
				assert((vcfh->state_flags & CFILE_MEM_ALIAS) == 0);
				ERETURN(IO_ERROR);
			}
			else if (vcfw->end == 0)
			{
				// cover our asses.
				vcfw = NULL;
				continue;
			}
		}
		x = MIN(end_pos(vcfw) - va, vc + rh->seed_len - va);
		update_adler32_seed(&ads, vcfw->buff + va - vcfw->offset, x);
		va += x;
		if (vc + rh->seed_len > va)
		{
			// loop back to get refilled from above.
			continue;
		}
		// check the hash for a match
		hash_offset = lookup_offset(rh, &ads);
		if (hash_offset == 0)
		{
			vc++;
			no_match++;
			continue;
		}
		if (hash_offset != cseek(rh->ref_cfh, hash_offset, CSEEK_FSTART))
		{
			ERETURN(IO_ERROR);
		}

		rcfw = expose_page(rh->ref_cfh);
		//verify we haven't hit checksum collision
		vm = vc;
		for (x = 0; x < rh->seed_len; x++)
		{
			if (rcfw->pos == rcfw->end)
			{
				rcfw = next_page(rh->ref_cfh);
				if (rcfw == NULL || rcfw->end == 0)
				{
					ERETURN(IO_ERROR);
				}
			}
			if (ads.seed_chars[(ads.tail + x) % ads.seed_len] !=
				rcfw->buff[rcfw->pos])
			{
				bad_match++;
				vc++;
				break;
			}
			rcfw->pos++;
		}
		if (vc != vm)
			continue;
		good_match++;
		//back matching
		vm = vc;
		rm = hash_offset;
		cseek(rh->ref_cfh, rm, CSEEK_FSTART);
		cseek(vcfh, vm, CSEEK_FSTART);

		while (vm > 0 && rm > 0)
		{
			while (vcfw->offset > vm - 1)
			{
				assert(end_pos(vcfw) > vm - 1);
				vcfw = prev_page(vcfh);
				if (vcfw->end == 0)
					ERETURN(IO_ERROR);
			}
			while (rcfw->offset > rm - 1)
			{
				assert(end_pos(rcfw) > rm - 1);
				rcfw = prev_page(rh->ref_cfh);
				if (rcfw->end == 0)
					ERETURN(IO_ERROR);
			}
			assert(vm - 1 >= vcfw->offset);
			assert(rm - 1 >= rcfw->offset);
			assert(end_pos(vcfw) > vm - 1);
			assert(end_pos(rcfw) > rm - 1);
			if (vcfw->buff[vm - 1 - vcfw->offset] == rcfw->buff[rm - 1 - rcfw->offset])
			{
				rm--;
				vm--;
			}
			else
			{
				break;
			}
		}
		len = vc + rh->seed_len - vm;

		//forward matching
		//first, reposition.
		if (vm + len != cseek(vcfh, vm + len, CSEEK_FSTART))
			ERETURN(IO_ERROR);
		vcfw = expose_page(vcfh);
		if (vcfw->end == 0 && vcfw->offset != ver_len)
			ERETURN(IO_ERROR);

		if (rm + len != cseek(rh->ref_cfh, rm + len, CSEEK_FSTART))
			ERETURN(IO_ERROR);
		rcfw = expose_page(rh->ref_cfh);
		if (rcfw->end == 0 && rcfw->offset != ref_len)
			ERETURN(IO_ERROR);

		while (vm + len < ver_len && rm + len < ref_len)
		{
			if (vm + len >= end_pos(vcfw))
			{
				vcfw = next_page(vcfh);
				if (vcfw->end == 0)
				{
					if (vcfw->offset != ver_len)
						ERETURN(IO_ERROR);
					break;
				}
			}
			assert(vm + len < vcfw->offset + vcfw->end);
			assert(vm + len >= vcfw->offset);

			if (rm + len >= end_pos(rcfw))
			{
				rcfw = next_page(rh->ref_cfh);
				if (rcfw->end == 0)
				{
					if (rcfw->offset != ref_len)
						ERETURN(IO_ERROR);
					break;
				}
			}
			assert(rm + len < rcfw->offset + rcfw->end);
			assert(rm + len >= rcfw->offset);

			if (vcfw->buff[vm + len - vcfw->offset] == rcfw->buff[rm + len - rcfw->offset])
			{
				len++;
			}
			else
			{
				break;
			}
		}
		if (vs <= vm)
		{
			if (vs < vm)
			{
				DCB_add_add(dcb, ver_start + vs, vm - vs, vid);
			}
			DCB_add_copy(dcb, ref_start + rm, ver_start + vm, len, rid);
		}
		else
		{
			DCB_truncate(dcb, vs - vm);
			DCB_add_copy(dcb, ref_start + rm, ver_start + vm, len, rid);
		}
		vs = vc = vm + len;
	}
	if (vs != ver_len)
		DCB_add_add(dcb, ver_start + vs, ver_len - vs, vid);
	free_adler32_seed(&ads);
	return 0;
}

signed int
MultiPassAlg(CommandBuffer *buff, cfile *ref_cfh, unsigned char ref_id,
			 cfile *ver_cfh, unsigned char ver_id,
			 unsigned long max_hash_size, unsigned int seed_len)
{
	int err;
	RefHash rhash;
	cfile ver_window;
	memset(&ver_window, 0, sizeof(cfile));
	unsigned long hash_size = 0, sample_rate = 1;
	unsigned long gap_req;
	unsigned long gap_total_len;
	unsigned char first_run = 0;
	DCLoc dc;
	assert(buff->DCBtype & DCBUFFER_LLMATCHES_TYPE);
	err = DCB_finalize(buff);
	if (err)
		ERETURN(err);

	dcb_lprintf(1, "multipass, hash_size(%lu)\n", hash_size);
	for (; seed_len >= 16; seed_len /= 2)
	{
		if (((DCB_llm *)buff->DCB)->main_head == NULL)
		{
			first_run = 1;
		}
		gap_req = seed_len;
		dcb_lprintf(1, "\nseed size(%u)...\n\n", seed_len);
		gap_total_len = 0;
		DCBufferReset(buff);

#ifdef DEBUG_DCBUFFER
		assert(DCB_test_llm_main(buff));
#endif
		if (!first_run)
		{
			while (DCB_get_next_gap(buff, gap_req, &dc))
			{
				assert(dc.len <= buff->ver_size);
				dcb_lprintf(2, "gap at %llu:%llu size %u\n", (act_off_u64)dc.offset, (act_off_u64)(dc.offset + dc.len), dc.len);
				gap_total_len += dc.len;
			}
			if (gap_total_len == 0)
			{
				dcb_lprintf(1, "not worth taking this pass, skipping to next.\n");
#ifdef DEBUG_DCBUFFER
				assert(DCB_test_llm_main(buff));
#endif
				continue;
			}
			hash_size = max_hash_size;
			sample_rate = COMPUTE_SAMPLE_RATE(hash_size, gap_total_len, seed_len);
			dcb_lprintf(1, "using hash_size(%lu), sample_rate(%lu)\n",
						hash_size, sample_rate);
			err = rh_rbucket_hash_init(&rhash, ref_cfh, seed_len, sample_rate, hash_size);
			if (err)
				ERETURN(err);
			DCBufferReset(buff);
			dcb_lprintf(1, "building hash array out of total_gap(%lu)\n",
						gap_total_len);
			while (DCB_get_next_gap(buff, gap_req, &dc))
			{
				RHash_insert_block(&rhash, ver_cfh, dc.offset, dc.len + dc.offset);
			}
			dcb_lprintf(1, "looking for matches in reference file\n");
			err = RHash_find_matches(&rhash, ref_cfh, 0, cfile_len(ref_cfh));
			if (err)
			{
				eprintf("error detected\n");
				ERETURN(err);
			}
			dcb_lprintf(1, "cleansing hash, to speed bsearch's\n");
			RHash_cleanse(&rhash);
			print_RefHash_stats(&rhash);
			dcb_lprintf(1, "beginning gap processing...\n");
			DCBufferReset(buff);
			while (DCB_get_next_gap(buff, gap_req, &dc))
			{
				dcb_lprintf(2, "handling gap %llu:%llu, size %u\n", (act_off_u64)dc.offset,
							(act_off_u64)(dc.offset + dc.len), dc.len);
				err = copen_child_cfh(&ver_window, ver_cfh, dc.offset, dc.len + dc.offset, NO_COMPRESSOR, CFILE_RONLY);
				if (err)
					ERETURN(err);
				err = DCB_llm_init_buff(buff, 128);
				if (err)
					ERETURN(err);
				err = OneHalfPassCorrecting(buff, &rhash, ref_id, &ver_window, ver_id);
				if (err)
					ERETURN(err);
				err = DCB_finalize(buff);
				if (err)
					ERETURN(err);
				cclose(&ver_window);
			}
		}
		else
		{
			first_run = 0;
			DCBufferReset(buff);
			dcb_lprintf(1, "first run\n");
			hash_size = MAX(MIN_RHASH_SIZE, MIN(max_hash_size, cfile_len(ref_cfh)));
			sample_rate = COMPUTE_SAMPLE_RATE(hash_size, cfile_len(ref_cfh), seed_len);
			dcb_lprintf(1, "using hash_size(%lu), sample_rate(%lu)\n",
						hash_size, sample_rate);
			err = rh_bucket_hash_init(&rhash, ref_cfh, seed_len, sample_rate, hash_size);
			if (err)
				ERETURN(err);
			err = RHash_insert_block(&rhash, ref_cfh, 0L, cfile_len(ref_cfh));
			if (err)
				ERETURN(err);
			print_RefHash_stats(&rhash);
			dcb_lprintf(1, "making initial run...\n");
			err = DCB_llm_init_buff(buff, 128);
			if (err)
				ERETURN(err);
			err = OneHalfPassCorrecting(buff, &rhash, ref_id, ver_cfh, ver_id);
			if (err)
				ERETURN(err);
			err = DCB_finalize(buff);
			if (err)
				ERETURN(err);
		}

#ifdef DEBUG_DCBUFFER
		assert(DCB_test_llm_main(buff));
#endif
		free_RefHash(&rhash);
	}
	return 0;
}
