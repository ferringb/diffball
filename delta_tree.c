// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <cfile.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <string.h>
#include <diffball/formats.h>
#include <diffball/defs.h>
#include <diffball/api.h>
#include "options.h"
#include <diffball/errors.h>

unsigned int global_verbosity = 0;

char *patch_format;

#define SRC_EXCLUDE_FILE 254
#define TRG_EXCLUDE_FILE 255

struct option long_opts[] = {
	STD_LONG_OPTIONS,
	DIFF_LONG_OPTIONS,
	FORMAT_LONG_OPTION("patch-format", 'f'),
	FORMAT_LONG_OPTION("source-exclude", 'S'),
	{"source-exclude-file", 1, 0, SRC_EXCLUDE_FILE},
	FORMAT_LONG_OPTION("target-exclude", 'T'),
	{"target-exclude-file", 1, 0, TRG_EXCLUDE_FILE},
	END_LONG_OPTS};

struct usage_options help_opts[] = {
	STD_HELP_OPTIONS,
	DIFF_HELP_OPTIONS,
	FORMAT_HELP_OPTION("patch-format", 'f', "format to output the patch in"),
	FORMAT_HELP_OPTION("source-exclude", 'S', "a file glob used to filter what source files are considered; this option is cumulative."),
	{0, "source-exclude-file", "read source-exclude patterns from the given file."},
	FORMAT_HELP_OPTION("target-exclude", 'T', "a file glob used to filter what target files are considered; this option is cumulative."),
	{0, "target-exclude-file", "read target-exclude patterns from the given file."},
	USAGE_FLUFF("delta_tree expects 3 args- source, target, name for the patch\n"
				"if output to stdout is enabled, only 2 args required- source, target\n"
				"Example usage: delta_tree older-version newerer-version upgrade-patch"),
	END_HELP_OPTS};

char short_opts[] = STD_SHORT_OPTIONS DIFF_SHORT_OPTIONS "f:T:S:";

struct exclude_list
{
	char **array;
	unsigned long count;
	unsigned long size;
	int is_src;
};

static void
exclude_list_free(struct exclude_list *l)
{
	unsigned long x;
	for (x = 0; x < l->count; x++)
	{
		free(l->array[x]);
	}
	if (l->array)
	{
		free(l->array);
	}
	l->array = NULL;
	l->size = l->count = 0;
}

static int
exclude_list_add(struct exclude_list *l, const char *pattern)
{
	if (l->count == l->size)
	{
		unsigned long new_size = l->size ? l->size * 2 : 16;
		char **tmp = realloc(l->array, new_size * sizeof(char *));
		if (!tmp)
		{
			v0printf("Failed allocating exclude array\n");
			return 1;
		}
		l->array = tmp;
		memset(l->array + l->size, 0, new_size - l->size);
		l->size = new_size;
	}
	l->array[l->count] = strdup(pattern);
	if (!l->array[l->count])
	{
		v0printf("Failed allocating exclude array\n");
		return 1;
	}
	l->count++;
	return 0;
}

static int
exclude_list_add_from_file(struct exclude_list *l, const char *filepath)
{
	cfile cfh;
	memset(&cfh, 0, sizeof(cfile));
	int err = copen(&cfh, filepath, NO_COMPRESSOR, CFILE_RONLY);
	if (err)
	{
		v0printf("Failed opening excludes file %s\n", filepath);
		return 1;
	}
	unsigned char *result = cfile_read_string_delim(&cfh, '\n', 1);
	while (result)
	{
		err = exclude_list_add(l, (const char *)result);
		free(result);
		if (err)
		{
			return 1;
		}
		result = cfile_read_string_delim(&cfh, '\n', 1);
	}
	return 0;
}

static int
exclude_filter(void *data, const char *filepath, struct stat *st)
{
	struct exclude_list *excludes = (struct exclude_list *)data;
	const char *basename = NULL;
	unsigned long x;
	for (x = 0; x < excludes->count; x++)
	{
		int result;
		if (excludes->array[x][0] != '/')
		{
			// Relative path.
			if (!basename)
			{
				basename = filepath + 1;
				const char *tmp = NULL;
				while ((tmp = strchr(basename, '/')))
				{
					basename = tmp + 1;
				}
			}
			result = fnmatch(excludes->array[x], basename, 0);
		}
		else
		{
			result = fnmatch(excludes->array[x], filepath, 0);
		}
		if (result == 0)
		{
			v1printf("Filtering file %s from the %s directory\n", filepath, excludes->is_src ? "source" : "target");
			return 1;
		}
		else if (result != FNM_NOMATCH)
		{
			v0printf("Failed invocation of fnmatch for %s; returned %i, bailing\n", filepath, result);
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	cfile out_cfh, ref_cfh, ver_cfh;
	memset(&out_cfh, 0, sizeof(cfile));
	memset(&ref_cfh, 0, sizeof(cfile));
	memset(&ver_cfh, 0, sizeof(cfile));

	int out_fh;

	int optr;
	char *src_file = NULL;
	char *trg_file = NULL;
	char *patch_name = NULL;
	unsigned long patch_id = 0;
	signed long encode_result = 0;
	struct exclude_list src_excludes = {NULL, 0, 0, 1}, trg_excludes = {NULL, 0, 0, 0};
	int err;
	unsigned long sample_rate = 0;
	unsigned long seed_len = 0;
	unsigned long hash_size = 0;
	unsigned int patch_to_stdout = 0;

#define DUMP_USAGE(exit_code) \
	print_usage("delta_tree", "src_file trg_file [patch_file|or to stdout]", help_opts, exit_code);

	while ((optr = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1)
	{
		switch (optr)
		{
		case OVERSION:
			print_version("delta_tree");
			exit(0);
		case OUSAGE:
		case OHELP:
			DUMP_USAGE(0);
		case OVERBOSE:
			global_verbosity++;
			break;
		case OSAMPLE:
			sample_rate = atol(optarg);
			if (sample_rate == 0 || sample_rate > MAX_SAMPLE_RATE)
				DUMP_USAGE(EXIT_USAGE);
			break;
		case OSEED:
			seed_len = atol(optarg);
			if (seed_len == 0 || seed_len > MAX_SEED_LEN)
				DUMP_USAGE(EXIT_USAGE);
			break;
		case OHASH:
			hash_size = atol(optarg);
			if (hash_size == 0 || hash_size > MAX_HASH_SIZE)
				DUMP_USAGE(EXIT_USAGE);
			break;
		case OSTDOUT:
			patch_to_stdout = 1;
			break;
		case 'f':
			patch_format = optarg;
			break;
		case 'S':
			if (exclude_list_add(&src_excludes, optarg))
			{
				exit(1);
			}
			break;
		case 'T':
			if (exclude_list_add(&trg_excludes, optarg))
			{
				exit(1);
			}
			break;
		case SRC_EXCLUDE_FILE:
			if (exclude_list_add_from_file(&src_excludes, optarg))
			{
				exit(1);
			}
			break;
		case TRG_EXCLUDE_FILE:
			if (exclude_list_add_from_file(&trg_excludes, optarg))
			{
				exit(1);
			}
			break;
		default:
			v0printf("invalid arg- %s\n", argv[optind]);
			DUMP_USAGE(EXIT_USAGE);
		}
	}
	err = 0;
	src_file = (char *)get_next_arg(argc, argv);
	if (!src_file)
	{
		DUMP_USAGE(EXIT_USAGE);
	}
	err = copen_multifile_directory(&ref_cfh, src_file,
									(src_excludes.count ? exclude_filter : NULL), (src_excludes.count ? &src_excludes : 0));
	if (err)
	{
		v0printf("Walking directory %s failed\n", src_file);
		exit(EXIT_USAGE);
	}
	trg_file = (char *)get_next_arg(argc, argv);
	if (!trg_file)
	{
		DUMP_USAGE(EXIT_USAGE);
	}
	err = copen_multifile_directory(&ver_cfh, trg_file,
									(trg_excludes.count ? exclude_filter : NULL), (trg_excludes.count ? &trg_excludes : 0));
	if (err)
	{
		v0printf("Walking directory %s failed\n", src_file);
		exit(EXIT_USAGE);
	}
	if (patch_to_stdout != 0)
	{
		out_fh = 1;
	}
	else
	{
		if ((patch_name = (char *)get_next_arg(argc, argv)) == NULL)
			DUMP_USAGE(EXIT_USAGE);
		if ((out_fh = open(patch_name, O_WRONLY | O_TRUNC | O_CREAT, 0644)) == -1)
		{
			v0printf("error creating patch file '%s' (open failed)\n", patch_name);
			exit(1);
		}
	}
	if (NULL != get_next_arg(argc, argv))
	{
		DUMP_USAGE(EXIT_USAGE);
	}

	if (patch_format == NULL)
	{
		patch_id = TREE_FORMAT;
	}
	else
	{
		patch_id = check_for_format(patch_format, strlen(patch_format));
		if (patch_id == 0)
		{
			v0printf("Unknown format '%s'\n", patch_format);
			exit(EXIT_FAILURE);
		}
	}
	if (copen_dup_fd(&out_cfh, out_fh, 0, 0, NO_COMPRESSOR, CFILE_WONLY))
	{
		v0printf("error allocing needed memory for output, exiting\n");
		exit(EXIT_FAILURE);
	}

	v1printf("using patch format %lu\n", patch_id);
	v1printf("using seed_len(%lu), sample_rate(%lu), hash_size(%lu)\n",
			 seed_len, sample_rate, hash_size);
	v1printf("verbosity level(%u)\n", global_verbosity);
	v1printf("initializing Command Buffer...\n");

	encode_result = simple_difference(&ref_cfh, &ver_cfh, &out_cfh, patch_id, seed_len, sample_rate, hash_size);
	v1printf("flushing and closing out file\n");
	cclose(&out_cfh);
	close(out_fh);
	if (err)
	{
		if (!patch_to_stdout)
		{
			unlink(patch_name);
		}
	}
	v1printf("encode_result=%ld\n", encode_result);
	v1printf("exiting\n");
	v1printf("closing reference file\n");
	cclose(&ref_cfh);
	v1printf("closing version file\n");
	cclose(&ver_cfh);
	close(out_fh);
	exclude_list_free(&src_excludes);
	exclude_list_free(&trg_excludes);
	check_return2(encode_result, "encoding result was nonzero") return 0;
}
