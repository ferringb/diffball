// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2003-2015 Brian Harring <ferringb@gmail.com>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <cfile.h>
#include <diffball/dcbuffer.h>
#include <diffball/formats.h>
#include <diffball/defs.h>
#include "options.h"
#include <diffball/errors.h>

struct option long_opts[] = {
	STD_LONG_OPTIONS,
	FORMAT_LONG_OPTION("src-format", 's'),
	FORMAT_LONG_OPTION("trg-format", 't'),
	END_LONG_OPTS};

struct usage_options help_opts[] = {
	STD_HELP_OPTIONS,
	FORMAT_HELP_OPTION("src-format", 's', "override auto-identification and specify source patches format"),
	FORMAT_HELP_OPTION("trg-format", 't', "override default and specify new patches format"),
	USAGE_FLUFF("convert_delta either expects 2 args (src patch, and new patches name), or just the source\n"
				"patches name if the option to dump to stdout has been given\n"
				"examples\n"
				"this would convert from the (auto-identified) xdelta format, to the default switching format\n"
				"convert_delta kde.xdelta kde.patch\n\n"
				"this would convert from the (auto-identified) xdelta format, to the gdiff4 format\n"
				"convert_delta kde.xdelta -t gdiff4 kde.patch\n"),
	END_HELP_OPTS};

char short_opts[] = STD_SHORT_OPTIONS "s:t:";

int main(int argc, char **argv)
{
	int out_fh;
	CommandBuffer dcbuff[2];
	cfile in_cfh[256], out_cfh;
	memset(in_cfh, 0, sizeof(cfile) * 256);
	memset(&out_cfh, 0, sizeof(cfile));
	char **patch_name;
	int optr, x;
	unsigned int patch_count;
	ECFH_ID src_id;
	signed int err;
	char *trg_file;
	unsigned long int src_format_id[256], trg_format_id = 0;
	signed long recon_val = 0, encode_result = 0;
	unsigned int output_to_stdout = 0;
	char *src_format = NULL, *trg_format = NULL;

#define DUMP_USAGE(exit_code) \
	print_usage("convert_delta", "src_patch -t format [new_patch|or to stdout]", help_opts, exit_code)

	while ((optr = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1)
	{
		switch (optr)
		{
			OPTIONS_COMMON_PATCH_ARGUMENTS("convert_delta");
		case 't':
			trg_format = optarg;
			break;

		default:
			dcb_lprintf(0, "unknown option %s\n", argv[optind]);
			DUMP_USAGE(EXIT_USAGE);
		}
	}
	patch_count = argc - optind;
	patch_name = argv + optind;

	if (output_to_stdout)
	{
		out_fh = 1;
	}
	else
	{
		if (patch_count <= 1)
		{
			dcb_lprintf(0, "you must specify at least a patch\n");
			DUMP_USAGE(EXIT_USAGE);
		}
		trg_file = patch_name[patch_count - 1];
		if ((out_fh = open(trg_file, O_WRONLY | O_TRUNC | O_CREAT, 0644)) == -1)
		{
			dcb_lprintf(0, "error creating output file '%s'\n", trg_file);
			exit(1);
		}
		patch_count--;
	}

	dcb_lprintf(1, "patch_count is %u\n", patch_count);

	if (trg_format == NULL)
	{
		dcb_lprintf(0, "new files format is required\n");
		DUMP_USAGE(EXIT_USAGE);
	}
	else
	{
		trg_format_id = check_for_format(trg_format, strlen(trg_format));
		if (trg_format_id == 0)
		{
			dcb_lprintf(0, "Unknown format '%s'\n", trg_format);
			exit(1);
		}
	}

	if (src_format != NULL)
	{
		src_format_id[0] = check_for_format(src_format, strlen(src_format));
		if (src_format_id[0] == 0)
		{
			dcb_lprintf(0, "Unknown format '%s'\n", src_format);
			exit(EXIT_FAILURE);
		}
		for (x = 1; x < patch_count; x++)
			src_format_id[x] = src_format_id[0];
	}

	for (x = 0; x < patch_count; x++)
	{
		dcb_lprintf(1, "%u, opening %s\n", x, patch_name[x]);
		if ((err = copen(in_cfh + x, patch_name[x], NO_COMPRESSOR, CFILE_RONLY)) != 0)
		{
			dcb_lprintf(0, "error opening patch '%s', %d\n", patch_name[x], err);
			exit(EXIT_FAILURE);
		}
		if (src_format == NULL)
		{
			src_format_id[x] = identify_format(in_cfh + x);
			if (src_format_id[x] == 0)
			{
				dcb_lprintf(0, "Couldn't identify the patch format, aborting\n");
				exit(EXIT_FAILURE);
			}
			else if ((src_format_id[x] >> 16) == 1)
			{
				dcb_lprintf(0, "Unsupported format version\n");
				exit(EXIT_FAILURE);
			}
			src_format_id[x] >>= 16;
		}
	}

	for (x = 0; x < patch_count; x++)
	{
		if (x == 0)
		{
			err = DCB_full_init(&dcbuff[0], 4096, 0, 0);
			check_return2(err, "DCBufferInit");
			src_id = DCB_register_fake_src(dcbuff, DC_COPY);
			check_return2(src_id, "internal_DCB_register_cfh_src");
		}
		else
		{
			err = DCB_full_init(&dcbuff[x % 2], 4096, 0, 0); //dcbuff[(x - 1) % 2].ver_size, 0);
			check_return2(err, "DCBufferInit");
			src_id = DCB_register_dcb_src(&dcbuff[x % 2], &dcbuff[(x - 1) % 2]);
			check_return2(src_id, "DCB_register_dcb_src");
		}

		if (SWITCHING_FORMAT == src_format_id[x])
		{
			recon_val = switchingReconstructDCBuff(src_id, &in_cfh[x], &dcbuff[x % 2]);
		}
		else if (GDIFF4_FORMAT == src_format_id[x])
		{
			recon_val = gdiff4ReconstructDCBuff(src_id, &in_cfh[x], &dcbuff[x % 2]);
		}
		else if (GDIFF5_FORMAT == src_format_id[x])
		{
			recon_val = gdiff5ReconstructDCBuff(src_id, &in_cfh[x], &dcbuff[x % 2]);
		}
		else if (BDIFF_FORMAT == src_format_id[x])
		{
			recon_val = bdiffReconstructDCBuff(src_id, &in_cfh[x], &dcbuff[x % 2]);
		}
		else if (XDELTA1_FORMAT == src_format_id[x])
		{
			recon_val = xdelta1ReconstructDCBuff(src_id, &in_cfh[x], &dcbuff[x % 2], 1);
		}
		else if (BDELTA_FORMAT == src_format_id[x])
		{
			recon_val = bdeltaReconstructDCBuff(src_id, &in_cfh[x], &dcbuff[x % 2]);
		}
		else if (BSDIFF_FORMAT == src_format_id[x])
		{
			dcb_lprintf(0, "Sorry, unwilling to do bsdiff conversion in this version.\n");
			dcb_lprintf(0, "Try a newer version.\n");
			exit(2);
		}
		dcb_lprintf(1, "%u: resultant ver_size was %llu\n", x, (act_off_u64)dcbuff[x].ver_size);
		if (recon_val)
		{
			dcb_lprintf(0, "error detected while processing patch-quitting\n");
			print_error(recon_val);
			exit(EXIT_FAILURE);
		}
		if (x)
		{
			DCBufferFree(&dcbuff[(x - 1) % 2]);
		}
	}

	dcb_lprintf(1, "reconstruction return=%ld\n", recon_val);
	copen_dup_fd(&out_cfh, out_fh, 0, 0, NO_COMPRESSOR, CFILE_WONLY);
	dcb_lprintf(1, "outputing patch...\n");
	if (DCBUFFER_FULL_TYPE == dcbuff[(patch_count - 1) % 2].DCBtype)
	{
		dcb_lprintf(1, "there were %lu commands\n", ((DCB_full *)dcbuff[(patch_count - 1) % 2].DCB)->cl.com_count);
	}
	if (GDIFF4_FORMAT == trg_format_id)
	{
		encode_result = gdiff4EncodeDCBuffer(&dcbuff[(patch_count - 1) % 2], &out_cfh);
	}
	else if (GDIFF5_FORMAT == trg_format_id)
	{
		encode_result = gdiff5EncodeDCBuffer(&dcbuff[(patch_count - 1) % 2], &out_cfh);
	}
	else if (BDIFF_FORMAT == trg_format_id)
	{
		encode_result = bdiffEncodeDCBuffer(&dcbuff[(patch_count - 1) % 2], &out_cfh);
	}
	else if (SWITCHING_FORMAT == trg_format_id)
	{
		encode_result = switchingEncodeDCBuffer(&dcbuff[(patch_count - 1) % 2], &out_cfh);
	}
	else if (BDELTA_FORMAT == trg_format_id)
	{
		encode_result = bdeltaEncodeDCBuffer(&dcbuff[(patch_count - 1) % 2], &out_cfh);
	}
	dcb_lprintf(1, "encoding return=%ld\n", encode_result);
	dcb_lprintf(1, "finished.\n");
	DCBufferFree(&dcbuff[(patch_count - 1) % 2]);
	for (x = 0; x < patch_count; x++)
	{
		cclose(&in_cfh[x]);
	}
	cclose(&out_cfh);
	if (encode_result)
	{
		dcb_lprintf(0, "Failed converting patch\n");
	}
	return encode_result;
}
