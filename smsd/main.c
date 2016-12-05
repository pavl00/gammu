/**
 * Main SMSD program
 */
/* Copyright (c) 2009 - 2015 Michal Cihar <michal@cihar.com> */
/* Licensend under GNU GPL 2 */

#include <gammu-smsd.h>
#include <assert.h>
#include <stdlib.h>
#include <signal.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif
#ifdef HAVE_WINDOWS_SERVICE
#include "winservice.h"
#endif
#ifdef HAVE_KILL
#include "pidfile.h"
#define HAVE_PIDFILE
#endif
#include <errno.h>
#include <string.h>
#include "common.h"
#if defined(HAVE_GETPWNAM) && defined(HAVE_GETGRNAM) && defined(HAVE_SETUID) && defined(HAVE_SETGID) && defined(HAVE_INITGROUPS)
#define HAVE_UID
#include "uid.h"
#endif
#ifdef HAVE_WINDOWS_EVENT_LOG
#include "log-event.h"
#endif

#if !defined(WIN32)
#define HAVE_DEFAULT_CONFIG
const char default_config[] = "/etc/gammu-smsdrc";
#endif

GSM_SMSDConfig *config;
volatile gboolean reconfigure = FALSE;
volatile gboolean standby = FALSE;

void smsd_interrupt(int signum)
{
	SMSD_Shutdown(config);
	signal(signum, SIG_IGN);
}

void smsd_reconfigure(int signum)
{
	reconfigure = TRUE;
	SMSD_Shutdown(config);
}

void smsd_standby(int signum)
{
	standby = TRUE;
	reconfigure = TRUE;
	SMSD_Shutdown(config);
}

void smsd_resume(int signum)
{
	standby = FALSE;
}

#ifdef WIN32
VOID CALLBACK TimerRoutine(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	SMSD_Shutdown(config);
}
#endif

NORETURN void version(void)
{
	printf("Gammu-smsd version %s\n", GAMMU_VERSION);
	printf("Compiled in features:\n");
	printf("OS support:\n");
#ifdef HAVE_SHM
	printf("  - %s\n", "SHM");
#endif
#ifdef HAVE_DAEMON
	printf("  - %s\n", "DAEMON");
#endif
#ifdef HAVE_PIDFILE
	printf("  - %s\n", "PID");
#endif
#if defined(HAVE_ALARM) || defined(WIN32)
	printf("  - %s\n", "ALARM");
#endif
#ifdef HAVE_UID
#endif
#ifdef HAVE_WINDOWS_SERVICE
	printf("  - %s\n", "WINDOWS_SERVICE");
#endif
#ifdef HAVE_GETOPT
	printf("  - %s\n", "GETOPT");
#endif
#ifdef HAVE_GETOPT_LONG
	printf("  - %s\n", "GETOPT_LONG");
#endif
#ifdef HAVE_WINDOWS_EVENT_LOG
	printf("  - %s\n", "EVENT_LOG");
#endif
#ifdef HAVE_SYSLOG
	printf("  - %s\n", "SYSLOG");
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
	printf("usage: gammu-smsd [OPTION]...\n");
	printf("options:\n");
	print_option("h", "help", "shows this help");
	print_option("v", "version", "shows version information");
	print_option_param("c", "config", "CONFIG_FILE", "defines path to config file");
#ifdef HAVE_DAEMON
	print_option("d", "daemon", "daemonizes program after startup");
#endif
#ifdef HAVE_PIDFILE
	print_option_param("p", "pid", "PID_FILE", "defines path to pid file");
#endif
#ifdef HAVE_UID
	print_option_param("U", "user", "USER", "run daemon as a user");
	print_option_param("G", "group", "GROUP", "run daemon as a group");
#endif
	print_option_param("f", "max-failures", "NUM", "number of failures before terminating");
#if defined(HAVE_ALARM) || defined(WIN32)
	print_option_param("X", "suicide", "SECONDS", "kills itself after number of seconds");
#endif
#ifdef HAVE_WINDOWS_SERVICE
	print_option("i", "install-service", "installs SMSD as a Windows service");
	print_option("u", "uninstall-service", "uninstalls SMSD as a Windows service");
	print_option("s", "start-service", "starts SMSD Windows service");
	print_option("k", "stop-service", "stops SMSD Windows service");
	print_option("S", "run-as-service", "runs as a SMSD Windows service");
	print_option_param("n", "service-name", "NAME", "name of a Windows service (default: GammuSMSD)");
#endif
	print_option("l", "use-log", "use logging configuration from config file (default)");
	print_option("L", "no-use-log", "do not use logging configuration from config file");
#ifdef HAVE_WINDOWS_EVENT_LOG
	print_option("e", "install-event-log", "installs Windows EventLog description to registry");
	print_option("E", "uninstall-event-log", "uninstalls Windows EventLog description to registry");
#endif
}

NORETURN void wrong_params(void)
{
	fprintf(stderr, "Invalid parameter, use -h for help.\n");
	exit(1);
}

void process_commandline(int argc, char **argv, SMSD_Parameters * params)
{
	int opt;
#ifdef WIN32
	HANDLE hTimer = NULL;
#endif

#ifdef HAVE_GETOPT_LONG
	struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"version", 0, 0, 'v'},
		{"config", 1, 0, 'c'},
		{"daemon", 0, 0, 'd'},
		{"pid", 1, 0, 'p'},
		{"install-service", 0, 0, 'i'},
		{"uninstall-service", 0, 0, 'u'},
		{"start-service", 0, 0, 's'},
		{"stop-service", 0, 0, 'k'},
		{"run-as-service", 0, 0, 'S'},
		{"user", 1, 0, 'U'},
		{"group", 1, 0, 'G'},
		{"service-name", 1, 0, 'n'},
		{"suicide", 1, 0, 'X'},
		{"max-failures", 1, 0, 'f'},
		{"use-log", 0, 0, 'l'},
		{"no-use-log", 0, 0, 'L'},
		{"install-event-log", 0, 0, 'e'},
		{"uninstall-event-log", 0, 0, 'E'},
		{0, 0, 0, 0}
	};
	int option_index;

	while ((opt = getopt_long(argc, argv, "hvdc:p:iusSkU:G:n:X:f:lLeE", long_options, &option_index)) != -1) {
#elif defined(HAVE_GETOPT)
	while ((opt = getopt(argc, argv, "hvdc:p:iusSkU:G:n:X:f:lLeE")) != -1) {
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
#ifdef HAVE_PIDFILE
			case 'p':
				params->pid_file = optarg;
				break;
#endif
#ifdef HAVE_UID
			case 'U':
				if (!fill_uid(params, optarg)) {
					fprintf(stderr, "Wrong user name/ID!\n");
					exit(1);
				}
				break;
			case 'G':
				if (!fill_gid(params, optarg)) {
					fprintf(stderr, "Wrong group name/ID!\n");
					exit(1);
				}
				break;
#endif
			case 'f':
				params->max_failures = atoi(optarg);
				break;
#if defined(HAVE_ALARM) || defined(WIN32)
			case 'X':
#ifdef HAVE_ALARM
				alarm(atoi(optarg));
#elif defined(WIN32)
				CreateTimerQueueTimer(&hTimer, NULL, (WAITORTIMERCALLBACK)TimerRoutine, NULL , atoi(optarg) * 1000, 0, 0);
#endif
				break;
#endif
#ifdef HAVE_DAEMON
			case 'd':
				params->daemonize = TRUE;
				break;
#endif
#ifdef HAVE_WINDOWS_EVENT_LOG
			case 'e':
				params->install_evlog = TRUE;
				break;
			case 'E':
				params->uninstall_evlog = TRUE;
				break;
#endif
#ifdef HAVE_WINDOWS_SERVICE
			case 'i':
				params->install_service = TRUE;
				break;
			case 'u':
				params->uninstall_service = TRUE;
				break;
			case 's':
				params->start_service = TRUE;
				break;
			case 'k':
				params->stop_service = TRUE;
				break;
			case 'S':
				params->run_service = TRUE;
				break;
			case 'n':
				strncpy(smsd_service_name, optarg, SERVICE_NAME_LENGTH);
				smsd_service_name[SERVICE_NAME_LENGTH - 1] = 0;
				break;
#endif
			case 'v':
				version();
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

}

void configure_daemon(SMSD_Parameters * params)
{
	signal(SIGINT, smsd_interrupt);
	signal(SIGTERM, smsd_interrupt);
#ifdef HAVE_SIGHUP
	signal(SIGHUP, smsd_reconfigure);
#endif
#ifdef HAVE_ALARM
	signal(SIGALRM, smsd_interrupt);
#endif
#if defined(HAVE_SIGUSR1) && defined(HAVE_SIGUSR2)
	signal(SIGUSR1, smsd_standby);
	signal(SIGUSR2, smsd_resume);
#endif

#ifdef HAVE_DAEMON
	/* Daemonize has to be before writing PID as it changes it */
	if (params->daemonize) {
		if (daemon(1, 0) != 0) {
			fprintf(stderr, "daemonizing failed! (%s)\n", strerror(errno));
			exit(1);
		}
	}
#endif

#ifdef HAVE_PIDFILE
	/* Writing PID file has to happen before dropping privileges */
	if (params->pid_file != NULL && strlen(params->pid_file) > 0) {
		check_pid(params->pid_file);
		write_pid(params->pid_file);
	}
#endif

#ifdef HAVE_UID
	if (params->gid >= 0 || params->uid >= 0) {
		if (!set_uid_gid(params)) {
			fprintf(stderr, "changing uid/gid failed! (%s)\n", strerror(errno));
			exit(1);
		}
	}
#endif

#ifdef HAVE_WINDOWS_SERVICE
	if (params->run_service) {
		if (!start_smsd_service_dispatcher()) {
			printf("Error starting %s service\n", smsd_service_name);
			service_print_error("Error running service");
			exit(1);
		}

		SMSD_FreeConfig(config);

		exit(0);
	}
#endif
}

int main(int argc, char **argv)
{
	GSM_Error error;
	const char program_name[] = "gammu-smsd";

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
		TRUE,
		0
	};

	/*
	 * We don't need gettext, but need to set locales so that
	 * charset conversion works.
	 */
	GSM_InitLocales(NULL);

	process_commandline(argc, argv, &params);

#ifdef HAVE_WINDOWS_SERVICE
	if (params.stop_service) {
		if (stop_smsd_service()) {
			printf("Service %s stopped sucessfully\n", smsd_service_name);
			exit(0);
		} else {
			printf("Error stopping %s service\n", smsd_service_name);
			service_print_error("Error stopping service");
			exit(1);
		}
	}
	if (params.uninstall_service) {
		if (uninstall_smsd_service()) {
			printf("Service %s uninstalled sucessfully\n", smsd_service_name);
			exit(0);
		} else {
			printf("Error uninstalling %s service\n", smsd_service_name);
			service_print_error("Error uninstalling service");
			exit(1);
		}
	}
#endif

#ifdef HAVE_WINDOWS_EVENT_LOG
	if (params.install_evlog) {
		if (eventlog_register()) {
			printf("Installed event log description\n");
			exit(0);
		} else {
			printf("Failed to install event log description!\n");
			exit(1);
		}
	}
	if (params.uninstall_evlog) {
		if (eventlog_deregister()) {
			printf("Uninstalled event log description\n");
			exit(0);
		} else {
			printf("Failed to uninstall event log description!\n");
			exit(1);
		}
	}
#endif
	if (params.config_file == NULL) {
#ifdef HAVE_DEFAULT_CONFIG
		params.config_file = default_config;
#else
		fprintf(stderr, "No config file specified!\n");
		help();
		exit(1);
#endif
	}
#ifdef HAVE_WINDOWS_SERVICE
	if (params.install_service) {
		if (install_smsd_service(&params)) {
			printf("Service %s installed sucessfully\n", smsd_service_name);
			exit(0);
		} else {
			printf("Error installing %s service\n", smsd_service_name);
			service_print_error("Error installing service");
			exit(1);
		}
	}
	if (params.start_service) {
		if (start_smsd_service()) {
			printf("Service %s started sucessfully\n", smsd_service_name);
			exit(0);
		} else {
			printf("Error starting %s service\n", smsd_service_name);
			service_print_error("Error starting service");
			exit(1);
		}
	}
#endif

read_config:
	config = SMSD_NewConfig(program_name);
	assert(config != NULL);

	error = SMSD_ReadConfig(params.config_file, config, params.use_log);
	if (error != ERR_NONE) {
		printf("Failed to read config: %s\n", GSM_ErrorString(error));
		SMSD_FreeConfig(config);
		return 2;
	}
	SMSD_EnableGlobalDebug(config);

	if (!reconfigure)
		configure_daemon(&params);

	reconfigure = FALSE;
	standby = FALSE;
	error = SMSD_MainLoop(config, FALSE, params.max_failures);
	if (error != ERR_NONE) {
		printf("Failed to run SMSD: %s\n", GSM_ErrorString(error));
		SMSD_FreeConfig(config);
		return 2;
	}

	SMSD_FreeConfig(config);

	/*
	 * Wait while we should be suspended.
	 * Later we fall back to reconfigure below.
	 */
	while (standby) {
		sleep(1);
	}

	if (reconfigure) {
		goto read_config;
	}

	return 0;
}

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
