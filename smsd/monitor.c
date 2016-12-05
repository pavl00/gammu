/**
 * SMSD message monitor program
 */
/* Copyright (c) 2009 - 2015 Michal Cihar <michal@cihar.com> */
/* Licensend under GNU GPL 2 */

#include <gammu-smsd.h>
#include <assert.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

#include "common.h"

#if !defined(WIN32) && (defined(HAVE_GETOPT) || defined(HAVE_GETOPT_LONG))
#define HAVE_DEFAULT_CONFIG
const char default_config[] = "/etc/gammu-smsdrc";
#endif

volatile gboolean terminate = FALSE;
int delay_seconds = 20;
int limit_loops = -1;
gboolean compact = FALSE;

void smsd_interrupt(int signum)
{
	terminate = TRUE;
}

NORETURN void version(void)
{
	printf("Gammu-smsd-monitor version %s\n", GAMMU_VERSION);
	printf("Compiled in features:\n");
	printf("OS support:\n");
#ifdef HAVE_SHM
	printf("  - %s\n", "SHM");
#endif
#ifdef HAVE_GETOPT
	printf("  - %s\n", "GETOPT");
#endif
#ifdef HAVE_GETOPT_LONG
	printf("  - %s\n", "GETOPT_LONG");
#endif
	printf("Backend services:\n");
	printf("  - %s\n", "NULL");
	printf("  - %s\n", "FILES");
#ifdef HAVE_MYSQL_MYSQL_H
	printf("  - %s\n", "MYSQL");
#endif
#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
	printf("  - %s\n", "POSTGRESQL");
#endif
#ifdef LIBDBI_FOUND
	printf("  - %s\n", "DBI");
#endif
#ifdef ODBC_FOUND
	printf("  - %s\n", "ODBC");
#endif
	printf("\n");
	printf("Copyright (C) 2003 - 2016 Michal Cihar <michal@cihar.com> and other authors.\n");
	printf("\n");
	printf("License GPLv2: GNU GPL version 2 <https://spdx.org/licenses/GPL-2.0>.\n");
	printf("This is free software: you are free to change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n");
	printf("\n");
	printf("Check <https://wammu.eu/gammu/> for updates.\n");
	printf("\n");
	exit(0);
}

#ifdef HAVE_GETOPT_LONG
#define print_option(name, longname, help) \
	printf("-%s / --%s - %s\n", name, longname, help);
#define print_option_param(name, longname, paramname, help) \
	printf("-%s / --%s %s - %s\n", name, longname, paramname, help);
#else
#define print_option(name, longname, help) \
	printf("-%s - %s\n", name, help);
#define print_option_param(name, longname, paramname, help) \
	printf("-%s %s - %s\n", name, paramname, help);
#endif

void help(void)
{
	printf("usage: gammu-smsd-monitor [OPTION]...\n");
	printf("options:\n");
	print_option("h", "help", "shows this help");
	print_option("v", "version", "shows version information");
	print_option("C", "csv", "CSV output");
	print_option_param("c", "config", "CONFIG_FILE",
			   "defines path to config file");
	print_option_param("d", "delay", "DELAY",
			   "delay in seconds between loops");
	print_option_param("n", "loops", "NUMBER",
			   "delay in seconds between loops");
	print_option("l", "use-log", "use logging configuration from config file");
	print_option("L", "no-use-log", "do not use logging configuration from config file (default)");
}

NORETURN void wrong_params(void)
{
	fprintf(stderr, "Invalid parameter, use -h for help.\n");
	exit(1);
}

void process_commandline(int argc, char **argv, SMSD_Parameters * params)
{
	int opt;

#ifdef HAVE_GETOPT_LONG
	struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"version", 0, 0, 'v'},
		{"config", 1, 0, 'c'},
		{"delay", 1, 0, 'd'},
		{"loops", 1, 0, 'n'},
		{"use-log", 0, 0, 'l'},
		{"no-use-log", 0, 0, 'L'},
		{0, 0, 0, 0}
	};
	int option_index;

	while ((opt =
		getopt_long(argc, argv, "+hvc:d:n:ClL", long_options,
			    &option_index)) != -1) {
#elif defined(HAVE_GETOPT)
	while ((opt = getopt(argc, argv, "+hvc:d:n:ClL")) != -1) {
#else
	/* Poor mans getopt replacement */
	int i;

#define optarg argv[++i]

	for (i = 1; i < argc; i++) {
		if (strlen(argv[i]) != 2 || argv[i][0] != '-') {
			wrong_params();
		}
		opt = argv[i][1];
#endif
		switch (opt) {
			case 'c':
				params->config_file = optarg;
				break;
			case 'v':
				version();
				break;
			case 'C':
				compact = TRUE;
				break;
			case 'd':
				delay_seconds = atoi(optarg);
				break;
			case 'n':
				limit_loops = atoi(optarg);
				break;
			case 'l':
				params->use_log = TRUE;
				break;
			case 'L':
				params->use_log = FALSE;
				break;
			case '?':
				wrong_params();
			case 'h':
				help();
				exit(0);
			default:
				fprintf(stderr, "Parameter -%c not known!\n", opt);
				wrong_params();
				break;
		}
	}

#if defined(HAVE_GETOPT) || defined(HAVE_GETOPT_LONG)
	if (optind < argc) {
		wrong_params();
	}
#endif

	return;

}

#ifndef WIN32
#endif

int main(int argc, char **argv)
{
	GSM_Error error;
	GSM_SMSDConfig *config;
	GSM_SMSDStatus status;
	const char program_name[] = "gammu-smsd-monitor";
	SMSD_Parameters params = {
		NULL,
		NULL,
		-1,
		-1,
		NULL,
		NULL,
		FALSE,
		FALSE,
		FALSE,
		FALSE,
		FALSE,
		FALSE,
		FALSE,
		FALSE,
		FALSE,
		0
	};


	/*
	 * We don't need gettext, but need to set locales so that
	 * charset conversion works.
	 */
	GSM_InitLocales(NULL);

	process_commandline(argc, argv, &params);

	if (params.config_file == NULL) {
#ifdef HAVE_DEFAULT_CONFIG
		params.config_file = default_config;
#else
		fprintf(stderr, "No config file specified!\n");
		help();
		exit(1);
#endif
	}

	signal(SIGINT, smsd_interrupt);
	signal(SIGTERM, smsd_interrupt);

	config = SMSD_NewConfig(program_name);
	assert(config != NULL);

	error = SMSD_ReadConfig(params.config_file, config, params.use_log);
	if (error != ERR_NONE) {
		printf("Failed to read config: %s\n", GSM_ErrorString(error));
		SMSD_FreeConfig(config);
		return 2;
	}
	SMSD_EnableGlobalDebug(config);

	while (!terminate && (limit_loops == -1 || limit_loops-- > 0)) {
		error = SMSD_GetStatus(config, &status);
		if (error != ERR_NONE) {
			printf("Failed to get status: %s\n", GSM_ErrorString(error));
			SMSD_FreeConfig(config);
			return 3;
		}
		if (compact) {
			printf("%s;%s;%s;%s;%d;%d;%d;%d;%d\n",
				 status.Client,
				 status.PhoneID,
				 status.IMEI,
				 status.IMSI,
				 status.Sent,
				 status.Received,
				 status.Failed,
				 status.Charge.BatteryPercent,
				 status.Network.SignalPercent);
		} else {
			printf("Client: %s\n", status.Client);
			printf("PhoneID: %s\n", status.PhoneID);
			printf("IMEI: %s\n", status.IMEI);
			printf("IMSI: %s\n", status.IMSI);
			printf("Sent: %d\n", status.Sent);
			printf("Received: %d\n", status.Received);
			printf("Failed: %d\n", status.Failed);
			printf("BatterPercent: %d\n", status.Charge.BatteryPercent);
			printf("NetworkSignal: %d\n", status.Network.SignalPercent);
			printf("\n");
		}
		sleep(delay_seconds);
	}

	SMSD_FreeConfig(config);

	return 0;
}

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
