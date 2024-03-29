// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <cfile.h>
#include <diffball/formats.h>
#include <diffball/defs.h>
#include "options.h"
#include <diffball/errors.h>
#include <diffball/dcbuffer.h>
#include <diffball/api.h>

static struct option long_opts[] = {
	STD_LONG_OPTIONS,
	FORMAT_LONG_OPTION("patch-format", 'f'),
	FORMAT_LONG_OPTION("max-buffer", 'b'),
	END_LONG_OPTS};

static struct usage_options help_opts[] = {
	STD_HELP_OPTIONS,
	FORMAT_HELP_OPTION("patch-format", 'f', "Override patch auto-identification"),
	FORMAT_HELP_OPTION("max-buffer", 'b', "Override the default 128KB buffer max"),
	USAGE_FLUFF("Normal usage is patcher src-file patch(s) reconstructed-file\n"
				"if you need to override the auto-identification (eg, you hit a bug), use -f.  Note this settings\n"
				"affects -all- used patches, so it's use should be limited to applying a single patch"),
	END_HELP_OPTS};

static char short_opts[] = STD_SHORT_OPTIONS "f:b:";

int main(int argc, char **argv)
{
	cfile src_cfh, out_cfh;
	cfile patch_cfh[256];
	memset(&src_cfh, 0, sizeof(cfile));
	memset(&out_cfh, 0, sizeof(cfile));
	memset(patch_cfh, 0, sizeof(cfile) * 256);

	cfile *patch_array[256];
	unsigned long x;
	char *src_name = NULL;
	char *out_name = NULL;
	unsigned long patch_count;
	char **patch_name;
	unsigned long format_id;
	signed long int recon_val = 0;
	unsigned int output_to_stdout = 0;
	char *src_format = NULL;
	int optr = 0, err;
	unsigned long reconst_size = 0xffff;

#define DUMP_USAGE(exit_code) \
	print_usage("patcher", "src_file patch(es) [trg_file|or to stdout]", help_opts, exit_code);

	while ((optr = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1)
	{
		switch (optr)
		{
			OPTIONS_COMMON_PATCH_ARGUMENTS("patcher");
		case 'b':
			reconst_size = atol(optarg);
			if (reconst_size > 0x4000000 || reconst_size == 0)
			{
				dcb_lprintf(0, "requested buffer size %lu isn't sane.  Must be greater then 0, and less then %lu\n",
							reconst_size, 0x4000000L);
				exit(EXIT_USAGE);
			}
			break;
		default:
			dcb_lprintf(0, "unknown option %s\n", argv[optind]);
			DUMP_USAGE(EXIT_USAGE);
		}
	}
	if ((src_name = (char *)get_next_arg(argc, argv)) == NULL)
	{
		if (src_name)
		{
			dcb_lprintf(0, "Must specify an existing source file!- %s not found\n", src_name);
			exit(EXIT_USAGE);
		}
		DUMP_USAGE(EXIT_USAGE);
	}
	else if (optind >= argc)
	{
		dcb_lprintf(0, "Must specify a patch file!\n");
		DUMP_USAGE(EXIT_USAGE);
	}
	patch_count = argc - optind;
	patch_name = optind + argv;
	if (output_to_stdout)
	{
		if (patch_count == 0)
		{
			dcb_lprintf(0, "Must specify an existing patch file!\n");
			DUMP_USAGE(EXIT_USAGE);
		}
	}
	else
	{
		if (patch_count == 1)
		{
			dcb_lprintf(0, "Must specify a name for the reconstructed file!\n");
			DUMP_USAGE(EXIT_USAGE);
		}
		out_name = patch_name[patch_count - 1];
		patch_name[patch_count] = NULL;
		patch_count--;
	}

	/* currently, unwilling to do bufferless for more then one patch.  overlay patches are the main 
	   concern; it shouldn't be hard completing the support, just no motivation currently :) */

	for (x = 0; x < patch_count; x++)
	{
		err = copen_path(&patch_cfh[x], patch_name[x], AUTODETECT_COMPRESSOR, CFILE_RONLY);
		check_return2(err, "copen of patch")
			patch_array[x] = &patch_cfh[x];
	}

	dcb_lprintf(1, "dcb verbosity level(%u)\n", diffball_get_logging_level());
	dcb_lprintf(1, "cfile verbosity level(%u)\n", cfile_get_logging_level());

	if ((err = copen_path(&src_cfh, src_name, AUTODETECT_COMPRESSOR, CFILE_RONLY)) != 0)
	{
		dcb_lprintf(0, "error opening source file '%s': %i\n", src_name, err);
		exit(EXIT_FAILURE);
	}

	if ((err = copen_path(&out_cfh, out_name, NO_COMPRESSOR, CFILE_WONLY | CFILE_NEW)) != 0)
	{
		dcb_lprintf(0, "error opening output file, exitting %i\n", err);
		exit(EXIT_FAILURE);
	}

	if (src_format != NULL)
	{
		format_id = check_for_format(src_format, strlen(src_format));
		if (format_id == 0)
		{
			dcb_lprintf(0, "desired forced patch format '%s' is unknown\n", src_format);
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		format_id = 0;
	}
	recon_val = simple_reconstruct(&src_cfh, patch_array, patch_count, &out_cfh, format_id, reconst_size);
	cclose(&out_cfh);
	if (recon_val != 0)
	{
		if (!output_to_stdout)
		{
			unlink(out_name);
		}
	}
	cclose(&src_cfh);
	for (x = 0; x < patch_count; x++)
	{
		cclose(&patch_cfh[x]);
	}
	if (recon_val)
	{
		dcb_lprintf(0, "Failed reconstructing the target file: error %ld\n", recon_val);
	}
	return recon_val;
}
