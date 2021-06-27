// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2014 Brian Harring <ferringb@gmail.com>
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
	FORMAT_LONG_OPTION("tmp-dir", 't'),
	END_LONG_OPTS};

static struct usage_options help_opts[] = {
	STD_HELP_OPTIONS,
	FORMAT_HELP_OPTION("patch-format", 'f', "Override patch auto-identification"),
	FORMAT_HELP_OPTION("max-buffer", 'b', "Override the default 128KB buffer max"),
	FORMAT_HELP_OPTION("tmp-dir", 't', "Override the environmental TMPDIR for any temp usage"),
	USAGE_FLUFF("Normal usage is delta_patcher src-file patch(s) reconstructed-file\n"
				"if you need to override the auto-identification (eg, you hit a bug), use -f.  Note this settings\n"
				"affects -all- used patches, so it's use should be limited to applying a single patch"),
	END_HELP_OPTS};

static char short_opts[] = STD_SHORT_OPTIONS "f:b:t:";

int main(int argc, char **argv)
{
	cfile patch_cfh;
	memset(&patch_cfh, 0, sizeof(cfile));
	char *src_name = NULL;
	char *out_name = NULL;
	char *patch_name;
	unsigned long format_id;
	signed long int recon_val = 0;

	unsigned int output_to_stdout = 0;

	char *patch_format = NULL;
	int optr = 0, err;
	unsigned long reconst_size = 0xffff;

#define DUMP_USAGE(exit_code) \
	print_usage("delta_patcher", "src_file patch(es) [trg_file|or to stdout]", help_opts, exit_code);

	while ((optr = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1)
	{
		switch (optr)
		{
		case OVERSION:
			print_version("delta_patcher");
			exit(0);
		case OUSAGE:
		case OHELP:
			DUMP_USAGE(0);
		case OVERBOSE:
			diffball_increase_logging_level();
			break;
		case OSTDOUT:
			output_to_stdout = 1;
			break;
		case 'f':
			patch_format = optarg;
			break;
		case 'b':
			reconst_size = atol(optarg);
			if (reconst_size > 0x4000000 || reconst_size == 0)
			{
				dcb_lprintf(0, "requested buffer size %lu isn't sane.  Must be greater then 0, and less then %lu\n",
							reconst_size, 0x4000000L);
				exit(EXIT_USAGE);
			}
			break;
		case 't':
			if (setenv("TMPDIR", optarg, 1))
			{
				dcb_lprintf(0, "Failed setting TMPDIR to %s; does it contain a '='?\n", optarg);
				exit(1);
			}
			break;
		default:
			dcb_lprintf(0, "unknown option %s\n", argv[optind]);
			DUMP_USAGE(EXIT_USAGE);
		}
	}

	src_name = (char *)get_next_arg(argc, argv);
	patch_name = (char *)get_next_arg(argc, argv);
	out_name = (char *)get_next_arg(argc, argv);

	if (!src_name || !patch_name || !out_name)
	{
		dcb_lprintf(0, "Wrong argument count\n");
		DUMP_USAGE(EXIT_USAGE);
	}

	struct stat st;
	if (stat(src_name, &st) || !S_ISDIR(st.st_mode))
	{
		dcb_lprintf(0, "source argument must be a directory: %s\n", src_name);
		exit(EXIT_USAGE);
	}
	if (stat(out_name, &st) || !S_ISDIR(st.st_mode))
	{
		dcb_lprintf(0, "target %s must exist and be a directory\n", out_name);
		exit(EXIT_USAGE);
	}

	err = copen(&patch_cfh, patch_name, AUTODETECT_COMPRESSOR, CFILE_RONLY);
	check_return2(err, "copen of patch");

	dcb_lprintf(1, "dcb: verbosity level(%u)\n", diffball_get_logging_level());
	dcb_lprintf(1, "cfile: verbosity level(%u)\n", cfile_get_logging_level());

	if (patch_format != NULL)
	{
		format_id = check_for_format(patch_format, strlen(patch_format));
		if (format_id == 0)
		{
			dcb_lprintf(0, "desired forced patch format '%s' is unknown\n", patch_format);
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		format_id = 0;
	}
	recon_val = treeReconstruct(src_name, &patch_cfh, out_name, NULL);

	if (recon_val != 0)
	{
		if (!output_to_stdout)
		{
			unlink(out_name);
		}
	}

	cclose(&patch_cfh);

	return recon_val;
}
