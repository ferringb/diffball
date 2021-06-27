// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <diffball/defs.h>
#include <errno.h>
#include <string.h>
#include <diffball/adler32.h>
#include <diffball/defs.h>
#include <diffball/hash.h>
#include <diffball/bit-functions.h>

static off_u64
base_rh_bucket_lookup(RefHash *, ADLER32_SEED_CTX *);

static signed int
rh_rbucket_insert_match(RefHash *, ADLER32_SEED_CTX *, off_u64);

static signed int
base_rh_bucket_hash_insert(RefHash *, ADLER32_SEED_CTX *, off_u64);

static signed int rh_rbucket_cleanse(RefHash *rhash);

static signed int
common_rh_bucket_hash_init(RefHash *rhash, cfile *ref_cfh, unsigned int seed_len, unsigned int sample_rate, unsigned long hr_size, unsigned int type);
static signed int internal_loop_block(RefHash *rhash, cfile *ref_cfh, off_u64 ref_start, off_u64 ref_end, hash_insert_func);

#define RH_BUCKET_NEED_RESIZE(x) \
	(((x) & ((x)-1)) == 0)

int cmp_chksum_ent(const void *ce1, const void *ce2)
{
	chksum_ent *c1 = (chksum_ent *)ce1;
	chksum_ent *c2 = (chksum_ent *)ce2;
	return (c1->chksum == c2->chksum ? 0 : c1->chksum < c2->chksum ? -1
																   : 1);
}

signed int
RHash_cleanse(RefHash *rh)
{
	if (rh->cleanse_hash)
		return rh->cleanse_hash(rh);
	return 0;
}

static inline signed int
RH_bucket_find_chksum_insert_pos(unsigned short chksum, unsigned short array[],
								 unsigned short count)
{
	int low = 0, high, mid;
	if (count < 32)
	{
		while (low < count)
		{
			if (array[low] > chksum)
				return low;
			else if (array[low] == chksum)
				return -1;
			low++;
		}
		return count;
	}
	high = count - 1;
	while (low < high)
	{
		mid = (low + high) / 2;
		if (chksum < array[mid])
			high = mid - 1;
		else if (chksum > array[mid])
			low = mid + 1;
		else
		{
			return -1;
		}
	}
	if (chksum > array[low])
		low++;
	assert(low >= 0);
	return low;
}

/* ripped straight out of K&R C manual.  great book btw. */
static inline signed int
RH_bucket_find_chksum(unsigned short chksum, unsigned short array[],
					  unsigned short count)
{
	int low, high, mid;
	low = 0;
	if (count < 32)
	{
		for (; low < count; low++)
		{
			if (chksum == array[low])
				return low;
		}
		return -1;
	}

	high = count - 1;
	while (low <= high)
	{
		mid = (low + high) / 2;
		if (chksum < array[mid])
			high = mid - 1;
		else if (chksum > array[mid])
			low = mid + 1;
		else
			return mid;
	}
	return -1;
}

static off_u64
base_rh_bucket_lookup(RefHash *rhash, ADLER32_SEED_CTX *ads)
{
	bucket *hash = (bucket *)rhash->hash;
	unsigned long index, chksum;
	signed int pos;
	chksum = get_checksum(ads);
	index = chksum & RHASH_INDEX_MASK;
	if (hash->depth[index] == 0)
	{
		return 0;
	}
	chksum = ((chksum >> 16) & 0xffff);
	pos = RH_bucket_find_chksum(chksum, hash->chksum[index], hash->depth[index]);
	if (pos >= 0)
	{
		assert(hash->depth[index] > pos);
		return hash->offset[index][pos];
	}
	return 0;
}

signed int
free_RefHash(RefHash *rhash)
{
	v2printf("free_RefHash\n");
	if (rhash->free_hash)
		rhash->free_hash(rhash);
	else if (rhash->hash)
		free(rhash->hash);
	rhash->hash = NULL;
	rhash->ref_cfh = NULL;
	rhash->free_hash = NULL;
	rhash->hash_insert = NULL;
	rhash->seed_len = rhash->hr_size = rhash->sample_rate = rhash->inserts = rhash->type = rhash->flags = rhash->duplicates = 0;
	return 0;
}

void rh_bucket_free(RefHash *rhash)
{
	unsigned long x;
	bucket *hash = (bucket *)rhash->hash;
	for (x = 0; x < rhash->hr_size; x++)
	{
		if (hash->chksum[x] != NULL)
		{
			free(hash->chksum[x]);
			free(hash->offset[x]);
		}
		else
		{
			assert(hash->depth[x] == 0);
		}
	}
	free(hash->chksum);
	free(hash->offset);
	free(hash->depth);
	free(hash);
}

void common_init_RefHash(RefHash *rhash, cfile *ref_cfh, unsigned int seed_len, unsigned int sample_rate, unsigned int type,
						 hash_insert_func hif, free_hash_func fhf, hash_lookup_offset_func hlof)
{
	rhash->flags = 0;
	rhash->type = 0;
	rhash->seed_len = seed_len;
	assert(seed_len > 0);
	rhash->sample_rate = sample_rate;
	rhash->ref_cfh = ref_cfh;
	rhash->inserts = rhash->duplicates = 0;
	rhash->hash = NULL;
	rhash->type = type;
	rhash->hr_size = 0;
	rhash->hash_insert = hif;
	rhash->insert_match = NULL;
	rhash->free_hash = fhf;
	rhash->lookup_offset = hlof;
	rhash->cleanse_hash = NULL;
}

signed int
rh_bucket_hash_init(RefHash *rhash, cfile *ref_cfh, unsigned int seed_len, unsigned int sample_rate, unsigned long hr_size)
{
	return common_rh_bucket_hash_init(rhash, ref_cfh, seed_len, sample_rate, hr_size, RH_BUCKET_HASH);
}
signed int
rh_rbucket_hash_init(RefHash *rhash, cfile *ref_cfh, unsigned int seed_len, unsigned int sample_rate, unsigned long hr_size)
{
	return common_rh_bucket_hash_init(rhash, ref_cfh, seed_len, sample_rate, hr_size, RH_RBUCKET_HASH);
}

static signed int
common_rh_bucket_hash_init(RefHash *rhash, cfile *ref_cfh, unsigned int seed_len, unsigned int sample_rate, unsigned long hr_size, unsigned int type)

{
	bucket *rh;

	common_init_RefHash(rhash, ref_cfh, seed_len, sample_rate, type, base_rh_bucket_hash_insert, rh_bucket_free, base_rh_bucket_lookup);
	if (hr_size == 0)
		hr_size = DEFAULT_RHASH_SIZE;
	if (hr_size < MIN_RHASH_SIZE)
		hr_size = MIN_RHASH_SIZE;

	assert((hr_size & ~(1 << unsignedBitsNeeded(hr_size))) == hr_size);

	rhash->hr_size = hr_size;
	rh = (bucket *)malloc(sizeof(bucket));
	if (rh == NULL)
		return MEM_ERROR;
	rh->max_depth = DEFAULT_RHASH_BUCKET_SIZE;
	if ((rh->depth = (unsigned char *)calloc(sizeof(unsigned char), rhash->hr_size)) == NULL)
	{
		free(rh);
		return MEM_ERROR;
	}
	else if ((rh->chksum = (unsigned short **)calloc(sizeof(unsigned short *), rhash->hr_size)) == NULL)
	{
		free(rh->depth);
		free(rh);
		return MEM_ERROR;
	}
	else if ((rh->offset = (off_u64 **)calloc(sizeof(off_u64 *), rhash->hr_size)) == NULL)
	{
		free(rh->chksum);
		free(rh->depth);
		free(rh);
		return MEM_ERROR;
	}
	rhash->hash = (void *)rh;
	if (type == RH_RBUCKET_HASH)
	{
		rhash->cleanse_hash = rh_rbucket_cleanse;
		rhash->flags |= RH_IS_REVLOOKUP;
		rhash->insert_match = rh_rbucket_insert_match;
	}
	return 0;
}

signed int
RH_bucket_resize(bucket *hash, unsigned long index, unsigned short size)
{
	assert(RH_BUCKET_NEED_RESIZE(hash->depth[index]));
	if (hash->depth[index] == 0)
	{
		if ((hash->chksum[index] = (unsigned short *)malloc(size * sizeof(unsigned short))) == NULL)
			return MEM_ERROR;
		if ((hash->offset[index] = (off_u64 *)malloc(size * sizeof(off_u64))) == NULL)
		{
			free(hash->chksum[index]);
			return MEM_ERROR;
		}
		return 0;
	}
	if ((hash->chksum[index] = (unsigned short *)realloc(hash->chksum[index], size * sizeof(unsigned short))) == NULL)
		return MEM_ERROR;
	else if ((hash->offset[index] = (off_u64 *)realloc(hash->offset[index], size * sizeof(off_u64))) == NULL)
		return MEM_ERROR;
	return 0;
}

static signed int
base_rh_bucket_hash_insert(RefHash *rhash, ADLER32_SEED_CTX *ads, off_u64 offset)
{
	unsigned long chksum, index;
	signed int low;
	bucket *hash;
	hash = (bucket *)rhash->hash;
	chksum = get_checksum(ads);
	index = (chksum & RHASH_INDEX_MASK);
	chksum = ((chksum >> 16) & 0xffff);
	if (!hash->depth[index])
	{
		if (RH_bucket_resize(hash, index, RH_BUCKET_MIN_ALLOC))
		{
			return MEM_ERROR;
		}
		hash->chksum[index][0] = chksum;
		if (rhash->type & (RH_BUCKET_HASH))
		{
			hash->offset[index][0] = offset;
		}
		else
		{
			hash->offset[index][0] = 0;
		}
		hash->depth[index]++;
		return SUCCESSFULL_HASH_INSERT;
	}
	else if (hash->depth[index] < hash->max_depth)
	{
		low = RH_bucket_find_chksum_insert_pos(chksum, hash->chksum[index], hash->depth[index]);
		if (low != -1 && (low == hash->depth[index] || hash->chksum[index][low] != chksum))
		{
			/* expand bucket if needed */

			if (RH_BUCKET_NEED_RESIZE(hash->depth[index]))
			{
				if (RH_bucket_resize(hash, index, MIN(hash->max_depth, (hash->depth[index] * RH_BUCKET_REALLOC_RATE))))
				{
					return MEM_ERROR;
				}
			}
			if (low == hash->depth[index])
			{
				hash->chksum[index][low] = chksum;
				if (rhash->type & RH_BUCKET_HASH)
				{
					hash->offset[index][low] = offset;
				}
				else
				{
					hash->offset[index][low] = 0;
				}
				assert(hash->chksum[index][low - 1] < hash->chksum[index][low]);
			}
			else
			{
				/* shift low + 1 element to the right */
				memmove(hash->chksum[index] + low + 1, hash->chksum[index] + low, (hash->depth[index] - low) * sizeof(unsigned short));
				hash->chksum[index][low] = chksum;
				if (rhash->type & RH_BUCKET_HASH)
				{
					memmove(hash->offset[index] + low + 1, hash->offset[index] + low, (hash->depth[index] - low) * sizeof(off_u64));
					hash->offset[index][low] = offset;
				}
				else
				{
					hash->offset[index][hash->depth[index]] = 0;
				}
				assert(low == 0 || hash->chksum[index][low - 1] < hash->chksum[index][low]);
				assert(low != 0 || hash->chksum[index][0] < hash->chksum[index][1]);
			}
			hash->depth[index]++;
			if (rhash->inserts + 1 == (rhash->hr_size * hash->max_depth))
				return SUCCESSFULL_HASH_INSERT_NOW_IS_FULL;
			return SUCCESSFULL_HASH_INSERT;
		}
	}
	return FAILED_HASH_INSERT;
}

signed int
RHash_insert_block(RefHash *rhash, cfile *ref_cfh, off_u64 ref_start, off_u64 ref_end)
{
	return internal_loop_block(rhash, ref_cfh, ref_start, ref_end, rhash->hash_insert);
}

signed int
RHash_find_matches(RefHash *rhash, cfile *ref_cfh, off_u64 ref_start, off_u64 ref_end)
{
	if (rhash->flags & ~RH_IS_REVLOOKUP)
		return 0;
	return internal_loop_block(rhash, ref_cfh, ref_start, ref_end, rhash->insert_match);
}

static signed int
internal_loop_block(RefHash *rhash, cfile *ref_cfh, off_u64 ref_start, off_u64 ref_end, hash_insert_func hif)
{
	ADLER32_SEED_CTX ads;
	unsigned long index, skip = 0;
	unsigned long len;
	signed int result;
	cfile_window *cfw;
	if (init_adler32_seed(&ads, rhash->seed_len))
		return MEM_ERROR;
	cseek(ref_cfh, ref_start, CSEEK_FSTART);
	cfw = expose_page(ref_cfh);
	if (cfw == NULL)
		return IO_ERROR;
	if (cfw->end == 0)
	{
		return 0;
	}

	if (cfw->pos + rhash->seed_len < cfw->end)
	{
		update_adler32_seed(&ads, cfw->buff + cfw->pos, rhash->seed_len);
		cfw->pos += rhash->seed_len;
	}
	else
	{
		len = rhash->seed_len;
		while (len)
		{
			skip = MIN(cfw->end - cfw->pos, len);
			update_adler32_seed(&ads, cfw->buff + cfw->pos, skip);
			len -= skip;
			if (len)
				cfw = next_page(ref_cfh);
			else
				cfw->pos += skip;
			if (cfw == NULL || cfw->end == 0)
			{
				return EOF_ERROR;
			}
		}
	}

	while (cfw->offset + cfw->pos <= ref_end)
	{
		if (cfw->pos > cfw->end)
		{
			cfw = next_page(ref_cfh);
			if (cfw == NULL || cfw->end == 0)
			{
				return MEM_ERROR;
			}
		}
		skip = 0;
		len = 1;
		result = hif(rhash, &ads, cfw->offset + cfw->pos - rhash->seed_len);
		if (result < 0)
		{
			return result;
		}
		else if (result == SUCCESSFULL_HASH_INSERT)
		{
			rhash->inserts++;
			if (rhash->sample_rate <= 1)
			{
				len = 1;
			}
			else if (rhash->sample_rate > rhash->seed_len)
			{
				len = rhash->seed_len;
				skip = rhash->sample_rate - rhash->seed_len;
			}
			else if (rhash->sample_rate > 1)
			{
				len = rhash->sample_rate;
			}
		}
		else if (result == FAILED_HASH_INSERT)
		{
			rhash->duplicates++;
		}
		else if (result == SUCCESSFULL_HASH_INSERT_NOW_IS_FULL)
		{
			rhash->inserts++;
			free_adler32_seed(&ads);
			return 0;
		}
		if (cfw->pos + cfw->offset + skip + len > ref_end)
		{
			break;
		}

		/* position ourself */
		while (cfw->pos + skip >= cfw->end)
		{
			skip -= (cfw->end - cfw->pos);
			cfw = next_page(ref_cfh);
			if (cfw == NULL)
			{
				free_adler32_seed(&ads);
				return IO_ERROR;
			}
		}
		cfw->pos += skip;

		// loop till we've updated the chksum.
		while (len)
		{
			index = MIN(cfw->end - cfw->pos, len);
			update_adler32_seed(&ads, cfw->buff + cfw->pos, index);
			cfw->pos += index;
			len -= index;

			// get next page if we still need more
			if (len)
			{
				cfw = next_page(ref_cfh);
				if (cfw == NULL)
				{
					free_adler32_seed(&ads);
					return IO_ERROR;
				}
			}
		}
	}
	free_adler32_seed(&ads);
	return 0;
}

static signed int
rh_rbucket_insert_match(RefHash *rhash, ADLER32_SEED_CTX *ads, off_u64 offset)
{
	bucket *hash = (bucket *)rhash->hash;
	unsigned long index, chksum;
	signed int pos;
	chksum = get_checksum(ads);
	index = (chksum & RHASH_INDEX_MASK);
	if (hash->depth[index])
	{
		chksum = ((chksum >> 16) & 0xffff);
		pos = RH_bucket_find_chksum(chksum, hash->chksum[index], hash->depth[index]);
		if (pos >= 0 && hash->offset[index][pos] == 0)
		{
			hash->offset[index][pos] = offset;
			return SUCCESSFULL_HASH_INSERT;
		}
	}
	return FAILED_HASH_INSERT;
}

static signed int
rh_rbucket_cleanse(RefHash *rhash)
{
	bucket *hash = (bucket *)rhash->hash;
	unsigned long x = 0, y = 0, shift = 0;
	rhash->inserts = 0;
	for (x = 0; x < rhash->hr_size; x++)
	{
		if (hash->depth[x] > 0)
		{
			shift = 0;
			for (y = 0; y < hash->depth[x]; y++)
			{
				assert(y == 0 || hash->chksum[x][y - 1] < hash->chksum[x][y]);
				if (hash->offset[x][y] == 0)
				{
					shift++;
				}
				else if (shift)
				{
					hash->offset[x][y - shift] = hash->offset[x][y];
					hash->chksum[x][y - shift] = hash->chksum[x][y];
				}
			}
			hash->depth[x] -= shift;
			if (hash->depth[x] == 0)
			{
				assert(NULL != hash->chksum[x]);
				assert(NULL != hash->offset[x]);
				free(hash->chksum[x]);
				free(hash->offset[x]);
				hash->chksum[x] = NULL;
				hash->offset[x] = NULL;
				continue;
			}
			rhash->inserts += hash->depth[x];
			if ((hash->chksum[x] = (unsigned short *)realloc(hash->chksum[x], sizeof(unsigned short) * hash->depth[x])) == NULL ||
				(hash->offset[x] = (off_u64 *)realloc(hash->offset[x], sizeof(off_u64) * hash->depth[x])) == NULL)
			{
				return MEM_ERROR;
			}
		}
	}
	rhash->flags |= RH_FINALIZED;
	return 0;
}

void print_RefHash_stats(RefHash *rhash)
{
	v1printf("hash stats: inserts(%lu), duplicates(%lu), hash size(%lu)\n",
			 rhash->inserts, rhash->duplicates, rhash->hr_size);
	v1printf("hash stats: load factor(%f%%)\n",
			 ((float)rhash->inserts / rhash->hr_size * 100));
	v1printf("hash stats: duplicate rate(%f%%)\n",
			 ((float)rhash->duplicates / (rhash->inserts + rhash->duplicates) * 100));
#ifdef DEBUG_HASH
	v1printf("hash stats: bad duplicates(%f%%)\n", ((float)
														rhash->bad_duplicates /
													rhash->duplicates * 100));
	v1printf("hash stats: good duplicates(%f%%)\n", 100.0 - ((float)
																 rhash->bad_duplicates /
															 rhash->duplicates * 100));
#endif
	v1printf("hash stats: seed_len(%u), sample_rate(%u)\n", rhash->seed_len,
			 rhash->sample_rate);
}
