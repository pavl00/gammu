/* Copyright (c) 2009 by Michal Čihař */

/**
 * \file dummy.c
 * @author Michal Čihař
 */
/**
 * @addtogroup Phone
 * @{
 */
/**
 * \defgroup DummyPhone Dummy phone driver
 * This driver emulates phone operations on storage on filesystem.
 *
 * Device name defines folder, then following files and directories are used:
 *
 * - operations.log - information about performed operations, which are not
 *   saved elsewhere, eg. message sending
 * - sms/[1-5] - folders for messages, messages are stored in gammu native
 *   format
 * - pbk/ME, pbk/SM, ... - folders for phonebook entries, entries are stored
 *   in vcard format
 * - note - vNote files for notes
 * - calendar, todo - vCalendar entries for todo and calendar
 * - fs - filesystem structure
 *
 * @{
 */

#include "../../gsmstate.h"

#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef WIN32
#include <direct.h>
#endif

#include "../../gsmcomon.h"
#include "../../misc/coding/coding.h"
#include "../../misc/locales.h"
#include "../../misc/misc.h"
#include "../pfunc.h"
#include "dummy.h"
#include "../../../helper/string.h"

#ifdef WIN32
#define MKDIR(dir) _mkdir(dir)
#ifndef S_ISDIR
#define S_ISDIR(mode) ((mode & _S_IFDIR) == _S_IFDIR)
#endif
#include "../../../helper/win32-dirent.h"
#else
#define MKDIR(dir) mkdir(dir, 0755)
#include <dirent.h>
#endif

GSM_Error DUMMY_Error(GSM_StateMachine *s, const char *message)
{
	int tmp_errno;
	tmp_errno = errno;
	GSM_OSErrorInfo(s, message);
	if (tmp_errno == ENOENT) {
		return ERR_EMPTY;
	} else if (tmp_errno == EACCES) {
		return ERR_PERMISSION;
	} else if (tmp_errno == EEXIST) {
		return ERR_FILEALREADYEXIST;
	} else {
		return ERR_UNKNOWN;
	}
}

char * DUMMY_GetFilePath(GSM_StateMachine *s, const char *filename)
{
	char *log_file=NULL;
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	log_file = (char *)malloc(strlen(filename) + Priv->devlen + 2);

	strcpy(log_file, s->CurrentConfig->Device);
	strcat(log_file, "/");
	strcat(log_file, filename);

	return log_file;
}

char * DUMMY_GetFSFilePath(GSM_StateMachine *s, const unsigned char *fullname)
{
	char *path=NULL;
	char *filename=NULL;
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	filename = DecodeUnicodeString(fullname);

	path = (char *)malloc(strlen(filename) + Priv->devlen + 5);

	strcpy(path, s->CurrentConfig->Device);
	strcat(path, "/fs/");
	strcat(path, filename);

	return path;
}

char * DUMMY_GetFSPath(GSM_StateMachine *s, const char *filename, int depth)
{
	char *path=NULL;
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	path = (char *)malloc(strlen(filename) + strlen(Priv->dirnames[depth]) + 2);

	strcpy(path, Priv->dirnames[depth]);
	strcat(path, "/");
	strcat(path, filename);

	return path;
}

int DUMMY_GetCount(GSM_StateMachine *s, const char *dirname)
{
	char *full_name=NULL;
	int i=0;
	FILE *f;
	int count = 0;

	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;
	full_name = (char *)malloc(strlen(dirname) + Priv->devlen + 20);

	for (i = 1; i <= DUMMY_MAX_LOCATION; i++) {
		sprintf(full_name, "%s/%s/%d", s->CurrentConfig->Device, dirname, i);
		f = fopen(full_name, "r");
		if (f == NULL) continue;
		count++;
		fclose(f);
	}
	free(full_name);
	full_name=NULL;
	return count;
}

GSM_Error DUMMY_DeleteAll(GSM_StateMachine *s, const char *dirname)
{
	char *full_name=NULL;
	int i=0;

	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;
	full_name = (char *)malloc(strlen(dirname) + Priv->devlen + 20);

	for (i = 1; i <= DUMMY_MAX_LOCATION; i++) {
		sprintf(full_name, "%s/%s/%d", s->CurrentConfig->Device, dirname, i);
		/* @todo TODO: Maybe we should check error code here? */
		unlink(full_name);
	}
	free(full_name);
	full_name=NULL;
	return ERR_NONE;
}

int DUMMY_GetFirstFree(GSM_StateMachine *s, const char *dirname)
{
	char *full_name=NULL;
	int i=0;
	FILE *f;

	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;
	full_name = (char *)malloc(strlen(dirname) + Priv->devlen + 20);

	for (i = 1; i <= DUMMY_MAX_LOCATION; i++) {
		sprintf(full_name, "%s/%s/%d", s->CurrentConfig->Device, dirname, i);
		f = fopen(full_name, "r");

		if (f == NULL) {
			free(full_name);
			full_name=NULL;
			return i;
		}
		fclose(f);
	}
	free(full_name);
	full_name=NULL;
	return -1;
}

int DUMMY_GetNext(GSM_StateMachine *s, const char *dirname, int current)
{
	char *full_name=NULL;
	int i=0;
	FILE *f;

	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;
	full_name = (char *)malloc(strlen(dirname) + Priv->devlen + 20);

	for (i = current + 1; i <= DUMMY_MAX_LOCATION; i++) {
		sprintf(full_name, "%s/%s/%d", s->CurrentConfig->Device, dirname, i);
		f = fopen(full_name, "r");
		if (f != NULL) {
			fclose(f);
			free(full_name);
			full_name=NULL;
			return i;
		}
	}
	free(full_name);
	full_name=NULL;
	return -1;
}

char * DUMMY_GetSMSPath(GSM_StateMachine *s, GSM_SMSMessage *sms)
{
	char smspath[100]={0};

	gboolean setfolder = (sms->Folder == 0);

	while (sms->Location >= DUMMY_MAX_SMS) {
		sms->Location -= DUMMY_MAX_SMS;
		if (setfolder) {
			sms->Folder++;
		}
	}

	sprintf(smspath, "sms/%d/%d", sms->Folder, sms->Location);
	return DUMMY_GetFilePath(s, smspath);
}

char * DUMMY_MemoryPath(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	char path[100];
	sprintf(path, "pbk/%s/%d", GSM_MemoryTypeToString(entry->MemoryType), entry->Location);
	return DUMMY_GetFilePath(s, path);
}

char * DUMMY_ToDoPath(GSM_StateMachine *s, GSM_ToDoEntry *entry)
{
	char path[100];
	sprintf(path, "todo/%d",  entry->Location);
	return DUMMY_GetFilePath(s, path);
}

char * DUMMY_NotePath(GSM_StateMachine *s, GSM_NoteEntry *entry)
{
	char path[100];
	sprintf(path, "note/%d",  entry->Location);
	return DUMMY_GetFilePath(s, path);
}

char * DUMMY_CalendarPath(GSM_StateMachine *s, GSM_CalendarEntry *entry)
{
	char path[100];
	sprintf(path, "calendar/%d",  entry->Location);
	return DUMMY_GetFilePath(s, path);
}

char * DUMMY_AlarmPath(GSM_StateMachine *s, GSM_Alarm *entry)
{
	char path[100];
	sprintf(path, "alarm/%d",  entry->Location);
	return DUMMY_GetFilePath(s, path);
}

/**
 * Initialises dummy module.
 */
GSM_Error DUMMY_Initialise(GSM_StateMachine *s)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;
	char *log_file, *path;
	const char * const paths[] = {
		"fs",
		"fs/incoming",
		"sms",
		"sms/1",
		"sms/2",
		"sms/3",
		"sms/4",
		"sms/5",
		"pbk",
		"pbk/ME",
		"pbk/SM",
		"pbk/MC",
		"pbk/RC",
		"pbk/DC",
		"note",
		"todo",
		"calendar",
		"alarm",
	};
	size_t i;

	Priv->devlen = strlen(s->CurrentConfig->Device);

	log_file = DUMMY_GetFilePath(s, "operations.log");

	smprintf(s, "Log file %s\n", log_file);

	/* Create some directories we might need */
	for (i = 0; i < sizeof(paths) / sizeof(char *); i++) {
		path = DUMMY_GetFilePath(s, paths[i]);
		if (MKDIR(path) != 0 && errno != EEXIST) {
			smprintf(s, "Failed to create directory: %s\n", path);
			free(path);
			free(log_file);
			return ERR_DEVICENOPERMISSION;
		}
		free(path);
	}

	for (i = 0; i <= DUMMY_MAX_FS_DEPTH; i++) {
		Priv->dir[i] = NULL;
	}
	Priv->fs_depth = 0;
	Priv->log_file = fopen(log_file, "w");
	free(log_file);
	log_file=NULL;

	if (Priv->log_file == NULL) {
		i = errno;
		GSM_OSErrorInfo(s, "Failed to open log");
		if (i == ENOENT) return ERR_DEVICENOTEXIST;		/* no such file or directory */
		if (i == EACCES) return ERR_DEVICENOPERMISSION;	/* permission denied */
		return ERR_DEVICEOPENERROR;
	}

	strcpy(s->Phone.Data.IMEI, "999999999999999");
	strcpy(s->Phone.Data.Manufacturer, "Gammu");
	strcpy(s->Phone.Data.Model, "Dummy");
	strcpy(s->Phone.Data.Version, GAMMU_VERSION);
	strcpy(s->Phone.Data.VerDate, "20150101");

	EncodeUnicode(Priv->SMSC.Number, "123456", 6);
	EncodeUnicode(Priv->SMSC.Name, "Default", 7);
	Priv->SMSC.Validity.Format = SMS_Validity_NotAvailable;
	Priv->SMSC.Validity.Relative = SMS_VALID_Max_Time;
	Priv->SMSC.DefaultNumber[0] = 0;
	Priv->SMSC.DefaultNumber[1] = 0;
	Priv->SMSC.Format = SMS_FORMAT_Text;
	Priv->SMSC.Location = 1;

	s->Phone.Data.VerNum = GAMMU_VERSION_NUM;

	Priv->locale.AMPMTime = FALSE;
	Priv->locale.DateFormat = GSM_Date_DDMMYYYY;
	Priv->locale.DateSeparator = '.';

	Priv->diverts.EntriesNum = 5;

	Priv->diverts.Entries[0].DivertType = GSM_DIVERT_Busy;
	Priv->diverts.Entries[0].CallType = GSM_DIVERT_VoiceCalls;
	Priv->diverts.Entries[0].Timeout = 10;
	EncodeUnicode(Priv->diverts.Entries[0].Number, "", 0);

	Priv->diverts.Entries[1].DivertType = GSM_DIVERT_NoAnswer;
	Priv->diverts.Entries[1].CallType = GSM_DIVERT_FaxCalls;
	Priv->diverts.Entries[1].Timeout = 20;
	EncodeUnicode(Priv->diverts.Entries[1].Number, "123", 3);

	Priv->diverts.Entries[2].DivertType = GSM_DIVERT_OutOfReach;
	Priv->diverts.Entries[2].CallType = GSM_DIVERT_DataCalls;
	Priv->diverts.Entries[2].Timeout = 30;
	EncodeUnicode(Priv->diverts.Entries[2].Number, "123", 3);

	Priv->diverts.Entries[3].DivertType = GSM_DIVERT_NoAnswer;
	Priv->diverts.Entries[3].CallType = GSM_DIVERT_VoiceCalls;
	Priv->diverts.Entries[3].Timeout = 40;
	EncodeUnicode(Priv->diverts.Entries[3].Number, "123", 3);

	Priv->diverts.Entries[4].DivertType = GSM_DIVERT_AllTypes;
	Priv->diverts.Entries[4].CallType = GSM_DIVERT_AllCalls;
	Priv->diverts.Entries[4].Timeout = 0;
	EncodeUnicode(Priv->diverts.Entries[4].Number, "", 0);

	return ERR_NONE;
}

/**
 * Terminates dummy module.
 */
GSM_Error DUMMY_Terminate(GSM_StateMachine *s)
{
	int i;
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	for (i = 0; i <= DUMMY_MAX_FS_DEPTH; i++) {
		if (Priv->dir[i] != NULL) {
			closedir(Priv->dir[i]);
			Priv->dir[i] = NULL;
		}
	}
	if (Priv->log_file != NULL) {
		fclose(Priv->log_file);
	}
	return ERR_NONE;
}


/**
 * We should not dispatch any messages.
 */
GSM_Error DUMMY_DispatchMessage(GSM_StateMachine *s)
{
	return ERR_UNKNOWN;
}

GSM_Error DUMMY_GetProductCode(GSM_StateMachine *s, char *value)
{
       strcpy(value, "DUMMY-001");
       return ERR_NONE;
}

GSM_Error DUMMY_GetIMEI (GSM_StateMachine *s)
{
	strcpy(s->Phone.Data.IMEI, "999999999999999");
	return ERR_NONE;
}

GSM_Error DUMMY_GetOriginalIMEI (GSM_StateMachine *s, char *value)
{
	strcpy(value, "666666666666666");
	return ERR_NONE;
}

GSM_Error DUMMY_GetManufactureMonth (GSM_StateMachine *s, char *value)
{
	strcpy(value, "April");
	return ERR_NONE;
}

GSM_Error DUMMY_GetHardware (GSM_StateMachine *s, char *value)
{
	strcpy(value, "FOO DUMMY BAR");
	return ERR_NONE;
}

GSM_Error DUMMY_GetPPM (GSM_StateMachine *s, char *value)
{
	strcpy(value, "EN CS");
	return ERR_NONE;
}

GSM_Error DUMMY_GetFirmware(GSM_StateMachine *s)
{
	strcpy(s->Phone.Data.Version, GAMMU_VERSION);
	strcpy(s->Phone.Data.VerDate, "20150101");
	GSM_CreateFirmwareNumber(s);
	return ERR_NONE;
}

GSM_Error DUMMY_GetModel(GSM_StateMachine *s)
{
	strcpy(s->Phone.Data.Model, "Dummy");
	return ERR_NONE;
}

GSM_Error DUMMY_GetDateTime(GSM_StateMachine *s, GSM_DateTime *date_time)
{
	GSM_GetCurrentDateTime(date_time);
	return ERR_NONE;
}

GSM_Error DUMMY_GetSMS(GSM_StateMachine *s, GSM_MultiSMSMessage *sms)
{
	GSM_SMS_Backup *Backup;
	char *filename;
	GSM_Error error;
	GSM_SMSMessage *SMS;
	int location, folder;
	int i = 0;

	location = sms->SMS[0].Location;
	folder = sms->SMS[0].Folder;
	Backup = malloc(sizeof(GSM_SMS_Backup));
	if (Backup == NULL) {
		return ERR_MOREMEMORY;
	}

	filename = DUMMY_GetSMSPath(s, &(sms->SMS[0]));

	error = GSM_ReadSMSBackupFile(filename, Backup);

	free(filename);
	filename=NULL;

	if (error != ERR_NONE) {
		free(Backup);
		if (error == ERR_CANTOPENFILE) return ERR_EMPTY;
		return error;
	}

	sms->Number = 0;

	for (SMS = Backup->SMS[i]; SMS != NULL; SMS = Backup->SMS[++i]) {
		sms->Number++;
		sms->SMS[i] = *(Backup->SMS[i]);
		sms->SMS[i].Location = location + (folder * DUMMY_MAX_SMS);
		sms->SMS[i].Folder = folder;
		switch (folder) {
			case 1:
				sms->SMS[i].InboxFolder = TRUE;
				sms->SMS[i].Memory = MEM_SM;
				break;
			case 2:
				sms->SMS[i].InboxFolder = FALSE;
				sms->SMS[i].Memory = MEM_SM;
				break;
			case 3:
				sms->SMS[i].InboxFolder = TRUE;
				sms->SMS[i].Memory = MEM_ME;
				break;
			case 4:
				sms->SMS[i].InboxFolder = FALSE;
				sms->SMS[i].Memory = MEM_ME;
				break;
			case 5:
				sms->SMS[i].InboxFolder = FALSE;
				sms->SMS[i].Memory = MEM_ME;
				break;
		}
	}
	GSM_FreeSMSBackup(Backup);
	free(Backup);

	return ERR_NONE;
}

GSM_Error DUMMY_DeleteSMS(GSM_StateMachine *s, GSM_SMSMessage *sms)
{
	char *filename=NULL;
	GSM_Error error;

	filename = DUMMY_GetSMSPath(s, sms);

	if (unlink(filename) == 0) {
		error = ERR_NONE;
	} else {
		error = DUMMY_Error(s, "SMS unlink failed");
	}
	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_SetSMS(GSM_StateMachine *s, GSM_SMSMessage *sms)
{
	char *filename=NULL;
	GSM_Error error;
	GSM_SMS_Backup *Backup;

	Backup = malloc(sizeof(GSM_SMS_Backup));
	if (Backup == NULL) {
		return ERR_MOREMEMORY;
	}

	error = DUMMY_DeleteSMS(s, sms);

	if (error != ERR_EMPTY && error != ERR_NONE) {
		free(Backup);
		return error;
	}

	filename = DUMMY_GetSMSPath(s, sms);

	Backup->SMS[0] = sms;
	Backup->SMS[1] = NULL;

	error = GSM_AddSMSBackupFile(filename, Backup);
	free(filename);
	free(Backup);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_AddSMS(GSM_StateMachine *s, GSM_SMSMessage *sms)
{
	char dirname[20]={0};

	sprintf(dirname, "sms/%d", sms->Folder);
	sms->Location = DUMMY_GetFirstFree(s, dirname);

	if (sms->Location == -1) {
		return ERR_FULL;
	}
	return DUMMY_SetSMS(s, sms);
}

GSM_Error DUMMY_GetSignalStrength(GSM_StateMachine *s, GSM_SignalQuality *sig)
{
	sig->SignalStrength = 42;
	sig->SignalPercent = 42;
	sig->BitErrorRate = 0;
	return ERR_NONE;
}

GSM_Error DUMMY_GetSMSFolders(GSM_StateMachine *s, GSM_SMSFolders *folders)
{
	PHONE_GetSMSFolders(s,folders);
	folders->Number = 5;

	CopyUnicodeString(folders->Folder[2].Name,folders->Folder[0].Name);
	folders->Folder[2].InboxFolder = folders->Folder[0].InboxFolder;
	folders->Folder[2].OutboxFolder = folders->Folder[0].OutboxFolder;
	folders->Folder[2].Memory = MEM_ME;

	CopyUnicodeString(folders->Folder[3].Name,folders->Folder[1].Name);
	folders->Folder[3].InboxFolder = folders->Folder[1].InboxFolder;
	folders->Folder[3].OutboxFolder = folders->Folder[1].OutboxFolder;
	folders->Folder[3].Memory = MEM_ME;

	EncodeUnicode(folders->Folder[4].Name,_("Templates"),strlen(_("Templates")));
	folders->Folder[4].InboxFolder = FALSE;
	folders->Folder[4].OutboxFolder = FALSE;
	folders->Folder[4].Memory = MEM_ME;
	return ERR_NONE;
}

GSM_Error DUMMY_GetNextSMS(GSM_StateMachine *s, GSM_MultiSMSMessage *sms, gboolean start)
{
	char dirname[20]={0};

	if (GSM_IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_DISABLE_GETNEXTSMS)) {
		return ERR_NOTSUPPORTED;
	}

	if (start) {
		sms->SMS[0].Folder = 1;
		sms->SMS[0].Location = 0;
	}

folder:
	/* Convert location */
	free(DUMMY_GetSMSPath(s, &(sms->SMS[0])));

	sprintf(dirname, "sms/%d", sms->SMS[0].Folder);

	sms->SMS[0].Location = DUMMY_GetNext(s, dirname, sms->SMS[0].Location);

	if (sms->SMS[0].Location == -1) {
		if (sms->SMS[0].Folder >= 5) return ERR_EMPTY;
		sms->SMS[0].Folder++;
		goto folder;
	}

	return DUMMY_GetSMS(s, sms);
}

GSM_Error DUMMY_GetSMSStatus(GSM_StateMachine *s, GSM_SMSMemoryStatus *status)
{
	char dirname[20];
	sprintf(dirname, "sms/%d", 5);
	status->TemplatesUsed	= DUMMY_GetCount(s, dirname);

	sprintf(dirname, "sms/%d", 1);
	status->SIMUsed		= DUMMY_GetCount(s, dirname);
	sprintf(dirname, "sms/%d", 2);
	status->SIMUsed		+= DUMMY_GetCount(s, dirname);
	status->SIMUnRead 	= 0;
	status->SIMSize		= DUMMY_MAX_SMS;

	sprintf(dirname, "sms/%d", 3);
	status->PhoneUsed	= DUMMY_GetCount(s, dirname);
	sprintf(dirname, "sms/%d", 4);
	status->PhoneUsed	+= DUMMY_GetCount(s, dirname);
	status->PhoneUsed	+= status->TemplatesUsed;
	status->PhoneUnRead 	= 0;
	status->PhoneSize	= DUMMY_MAX_SMS;
	return ERR_NONE;
}

GSM_Error DUMMY_DialVoice(GSM_StateMachine *s, char *number, GSM_CallShowNumber ShowNumber)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	fprintf(Priv->log_file, "Dialling %s\n", number);

	return ERR_NONE;
}

GSM_Error DUMMY_DialService(GSM_StateMachine *s, char *number)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;
	GSM_USSDMessage ussd;
	char ussd_text[GSM_MAX_USSD_LENGTH + 1];
	size_t len;


	fprintf(Priv->log_file, "Dialling service %s\n", number);

	if (s->Phone.Data.EnableIncomingUSSD && s->User.IncomingUSSD != NULL) {
		ussd.Status = USSD_NoActionNeeded;
		len = snprintf(ussd_text, GSM_MAX_USSD_LENGTH, "Reply for %s", number);
		EncodeUnicode(ussd.Text, ussd_text, len);

		/* Notify application */
		s->User.IncomingUSSD(s, &ussd, s->User.IncomingUSSDUserData);
	}

	return ERR_NONE;
}

GSM_Error DUMMY_AnswerCall(GSM_StateMachine *s, int ID, gboolean all)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	fprintf(Priv->log_file, "Answering call %d (%d)\n", ID, all);

	return ERR_NONE;
}

GSM_Error DUMMY_GetNetworkInfo(GSM_StateMachine *s, GSM_NetworkInfo *netinfo)
{
	strcpy(netinfo->CID, "FACE");
	strcpy(netinfo->PacketCID, "DEAD");
	strcpy(netinfo->NetworkCode, "999 99");
	netinfo->State = GSM_HomeNetwork;
	strcpy(netinfo->LAC, "B00B");
	strcpy(netinfo->PacketLAC, "BEEF");
	netinfo->PacketState = GSM_HomeNetwork;
	EncodeUnicode(netinfo->NetworkName, "GammuT3l", 8);
	netinfo->GPRS = GSM_GPRS_Attached;

	return ERR_NONE;
}

GSM_Error DUMMY_GetDisplayStatus(GSM_StateMachine *s, GSM_DisplayFeatures *features)
{
	features->Number = 1;
	features->Feature[0] = GSM_KeypadLocked;
	return ERR_NONE;
}

GSM_Error DUMMY_SetAutoNetworkLogin(GSM_StateMachine *s)
{
	return ERR_NOTIMPLEMENTED;
}

GSM_Error DUMMY_PressKey(GSM_StateMachine *s, GSM_KeyCode Key, gboolean Press)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	if (Press) {
		fprintf(Priv->log_file, "Pressing key %d\n", Key);
	} else {
		fprintf(Priv->log_file, "Releasing key %d\n", Key);
	}

	return ERR_NONE;
}

GSM_Error DUMMY_Reset(GSM_StateMachine *s, gboolean hard)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	if (hard) {
		fprintf(Priv->log_file, "Performing hard reset!\n");
	} else {
		fprintf(Priv->log_file, "Performing soft reset!\n");
	}

	return ERR_NONE;
}

GSM_Error DUMMY_CancelCall(GSM_StateMachine *s, int ID, gboolean all)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	fprintf(Priv->log_file, "Canceling call %d (%d)\n", ID, all);

	return ERR_NONE;
}

GSM_Error DUMMY_SendSavedSMS(GSM_StateMachine *s, int Folder, int Location)
{
	return ERR_NOTIMPLEMENTED;
}

GSM_Error DUMMY_SendSMS(GSM_StateMachine *s, GSM_SMSMessage *sms)
{
	unsigned char buffer[1000] = {'\0'};
	int length = 0;
	GSM_Error error;

	if (sms->PDU == SMS_Deliver) {
		smprintf(s, "SMS Deliver\n");

		error = PHONE_EncodeSMSFrame(s, sms, buffer, PHONE_SMSDeliver, &length, TRUE);

		if (error != ERR_NONE) {
			return error;
		}
	} else {
		smprintf(s, "SMS Submit\n");

		error = PHONE_EncodeSMSFrame(s, sms, buffer, PHONE_SMSSubmit, &length, TRUE);

		if (error != ERR_NONE) {
			return error;
		}
	}
	s->User.SendSMSStatus(s, 0, 0xff, s->User.SendSMSStatusUserData);
	return ERR_NONE;
}

GSM_Error DUMMY_SetDateTime(GSM_StateMachine *s, GSM_DateTime *date_time)
{
	return ERR_NONE;
}

GSM_Error DUMMY_SetSMSC(GSM_StateMachine *s, GSM_SMSC *smsc)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	if (smsc->Location != 1) return ERR_NOTSUPPORTED;

	Priv->SMSC = *smsc;

	return ERR_NONE;
}

GSM_Error DUMMY_GetSMSC(GSM_StateMachine *s, GSM_SMSC *smsc)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	if (smsc->Location != 1) return ERR_EMPTY;

	*smsc = Priv->SMSC;

	return ERR_NONE;
}

GSM_Error DUMMY_EnterSecurityCode(GSM_StateMachine *s, GSM_SecurityCode *Code)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	fprintf(Priv->log_file, "Security code %d: %s\n", Code->Type, Code->Code);

	return ERR_NONE;
}

GSM_Error DUMMY_GetSecurityStatus(GSM_StateMachine *s, GSM_SecurityCodeType *Status)
{
	*Status = SEC_None;
	return ERR_NONE;
}

GSM_Error DUMMY_ResetPhoneSettings(GSM_StateMachine *s, GSM_ResetSettingsType Type)
{
	if (Type == GSM_RESET_PHONESETTINGS) {
		return ERR_NONE;
	}
	return ERR_NOTIMPLEMENTED;
}

GSM_Error DUMMY_SendDTMF(GSM_StateMachine *s, char *sequence)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	fprintf(Priv->log_file, "DTMF: %s\n", sequence);
	return ERR_NONE;
}

GSM_Error DUMMY_GetSIMIMSI(GSM_StateMachine *s, char *IMSI)
{
	strcpy(IMSI, "994299429942994");
	return ERR_NONE;
}

GSM_Error DUMMY_GetCallDivert(GSM_StateMachine *s, GSM_CallDivert *request, GSM_MultiCallDivert *response)
{
	int i;
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	response->EntriesNum = 0;

	for (i = 0; i < Priv->diverts.EntriesNum; i++) {
		if (request->DivertType == Priv->diverts.Entries[i].DivertType && request->CallType == Priv->diverts.Entries[i].CallType) {
			response->Entries[response->EntriesNum] = Priv->diverts.Entries[i];
			response->EntriesNum++;
		}
	}

	return ERR_NONE;
}

GSM_Error DUMMY_SetCallDivert(GSM_StateMachine *s, GSM_CallDivert *request)
{
	int i;
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	for (i = 0; i < Priv->diverts.EntriesNum; i++) {
		if ((request->DivertType == Priv->diverts.Entries[i].DivertType) &&
		    (request->CallType == Priv->diverts.Entries[i].CallType)) {
			break;
		}
	}
	Priv->diverts.Entries[i] = *request;
	Priv->diverts.EntriesNum = i + 1;

	return ERR_NONE;
}

GSM_Error DUMMY_CancelAllDiverts(GSM_StateMachine *s)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;

	Priv->diverts.EntriesNum = 0;

	return ERR_NONE;
}

GSM_Error DUMMY_SetIncomingCall (GSM_StateMachine *s, gboolean enable)
{
	s->Phone.Data.EnableIncomingCall = enable;
	return ERR_NONE;
}

GSM_Error DUMMY_SetIncomingCB (GSM_StateMachine *s, gboolean enable)
{
	s->Phone.Data.EnableIncomingCB 	= enable;
	return ERR_NONE;
}

GSM_Error DUMMY_SetIncomingSMS (GSM_StateMachine *s, gboolean enable)
{
	s->Phone.Data.EnableIncomingSMS = enable;
	return ERR_NONE;
}

GSM_Error DUMMY_SetFastSMSSending(GSM_StateMachine *s, gboolean enable)
{
	return ERR_NONE;
}

GSM_Error DUMMY_GetManufacturer(GSM_StateMachine *s)
{
	strcpy(s->Phone.Data.Manufacturer, "Gammu");
	return ERR_NONE;
}

GSM_Error DUMMY_GetAlarm(GSM_StateMachine *s, GSM_Alarm *entry)
{
	GSM_Backup Backup;
	char *filename=NULL;
	GSM_Error error;
	int i;

	filename = DUMMY_AlarmPath(s, entry);

	error = GSM_ReadBackupFile(filename, &Backup, GSM_Backup_VCalendar);

	free(filename);
	filename=NULL;

	if (error != ERR_NONE) {
		if (error == ERR_CANTOPENFILE) return ERR_EMPTY;
		return error;
	}
	if (Backup.Calendar[0] == NULL) return ERR_EMPTY;

	entry->Repeating = FALSE;
	entry->Text[0] = 0;
	entry->Text[1] = 0;
	GSM_GetCurrentDateTime(&entry->DateTime);

	for (i = 0; i < Backup.Calendar[0]->EntriesNum; i++) {
		switch (Backup.Calendar[0]->Entries[i].EntryType) {
			case CAL_TONE_ALARM_DATETIME:
				entry->DateTime = Backup.Calendar[0]->Entries[i].Date;
				break;
			case CAL_TEXT:
				CopyUnicodeString(entry->Text, Backup.Calendar[0]->Entries[i].Text);
				break;
			case CAL_REPEAT_FREQUENCY:
				if (Backup.Calendar[0]->Entries[i].Number) {
					entry->Repeating = TRUE;
				}
				break;
			default:
				break;
		}
	}

	GSM_FreeBackup(&Backup);

	return ERR_NONE;
}

GSM_Error DUMMY_DeleteAlarm(GSM_StateMachine *s, GSM_Alarm *entry)
{
	char *filename=NULL;
	GSM_Error error;

	filename = DUMMY_AlarmPath(s, entry);

	if (unlink(filename) == 0) {
		error = ERR_NONE;
	} else {
		error = DUMMY_Error(s, "calendar unlink failed");
	}

	free(filename);
	filename=NULL;
	return error;
}


GSM_Error DUMMY_SetAlarm(GSM_StateMachine *s, GSM_Alarm *entry)
{
	char *filename=NULL;
	GSM_Error error;
	GSM_Backup backup;
	GSM_CalendarEntry cal;

	GSM_ClearBackup(&backup);

	error = DUMMY_DeleteAlarm(s, entry);
	if (error != ERR_EMPTY && error != ERR_NONE) return error;

	filename = DUMMY_AlarmPath(s, entry);

	cal.EntriesNum = 3;
	cal.Type = GSM_CAL_ALARM;
	cal.Location = entry->Location;
	cal.Entries[0].EntryType = CAL_TONE_ALARM_DATETIME;
	cal.Entries[0].Date = entry->DateTime;
	cal.Entries[0].Date.Year = 1970;
	cal.Entries[0].Date.Month = 1;
	cal.Entries[0].Date.Day = 1;
	cal.Entries[0].Date.Timezone = 0;
	cal.Entries[1].EntryType = CAL_START_DATETIME;
	cal.Entries[1].Date = entry->DateTime;
	cal.Entries[1].Date.Year = 1970;
	cal.Entries[1].Date.Month = 1;
	cal.Entries[1].Date.Day = 1;
	cal.Entries[1].Date.Timezone = 0;
	cal.Entries[2].EntryType = CAL_TEXT;
	CopyUnicodeString(cal.Entries[2].Text, entry->Text);
	if (entry->Repeating) {
		cal.Entries[3].EntryType = CAL_REPEAT_FREQUENCY;
		cal.Entries[3].Number = 1;
	}

	backup.Calendar[0] = &cal;
	backup.Calendar[1] = NULL;

	error = GSM_SaveBackupFile(filename, &backup, GSM_Backup_VCalendar);
	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_SetIncomingUSSD(GSM_StateMachine *s, gboolean enable)
{
	s->Phone.Data.EnableIncomingUSSD = enable;
	return ERR_NONE;
}

GSM_Error DUMMY_GetRingtone(GSM_StateMachine *s, GSM_Ringtone *Ringtone, gboolean PhoneRingtone)
{
	return ERR_NOTIMPLEMENTED;
}

GSM_Error DUMMY_SetRingtone(GSM_StateMachine *s, GSM_Ringtone *Ringtone, int *maxlength)
{
	return ERR_NOTIMPLEMENTED;
}

GSM_Error DUMMY_GetWAPBookmark(GSM_StateMachine *s, GSM_WAPBookmark *bookmark)
{
	switch (bookmark->Location) {
		case 1:
			EncodeUnicode(bookmark->Address, "http://blog.cihar.com/", 22);
			EncodeUnicode(bookmark->Title, "Michals weblog", 14);
			return ERR_NONE;
		case 2:
			EncodeUnicode(bookmark->Address, "http://wammu.eu/", 16);
			EncodeUnicode(bookmark->Title, "Wammu website", 13);
			return ERR_NONE;
		default:
			return ERR_EMPTY;

	}
}

GSM_Error DUMMY_GetBitmap(GSM_StateMachine *s, GSM_Bitmap *Bitmap)
{
	return ERR_NOTIMPLEMENTED;
}

GSM_Error DUMMY_SetBitmap(GSM_StateMachine *s, GSM_Bitmap *Bitmap)
{
	return ERR_NOTIMPLEMENTED;
}

GSM_Error DUMMY_SetFileAttributes(GSM_StateMachine *s, GSM_File *File)
{
	/* We just ignore attributes */
	return ERR_NONE;
}

GSM_Error DUMMY_AddFilePart(GSM_StateMachine *s, GSM_File *File, int *Pos, int *Handle)
{
	char *path;
	FILE *file;
	size_t pos;
	GSM_Error error;

	*Handle = 0;

	pos = UnicodeLength(File->ID_FullName);
	if (pos > 0 && (File->ID_FullName[2*pos - 2] != 0 || File->ID_FullName[2*pos - 1] != '/')) {
		File->ID_FullName[2*pos + 1] = '/';
		File->ID_FullName[2*pos + 0] = 0;
		pos++;
	}
	CopyUnicodeString(File->ID_FullName + 2 * pos, File->Name);

	path = DUMMY_GetFSFilePath(s, File->ID_FullName);

	file = fopen(path, "w");
	if (file == NULL) {
		free(path);
		error = DUMMY_Error(s, "fopen(w) failed");
		if (error == ERR_EMPTY) return ERR_PERMISSION;
		return error;
	}
	if (fwrite(File->Buffer, 1, File->Used, file) != File->Used) {
		free(path);
		error = DUMMY_Error(s, "fwrite failed");
		fclose(file);
		if (error == ERR_EMPTY) return ERR_PERMISSION;
		return error;
	}
	if (fclose(file) != 0) {
		free(path);
		error = DUMMY_Error(s, "fclose failed");
		if (error == ERR_EMPTY) return ERR_PERMISSION;
		return error;
	}

	free(path);
	path=NULL;
	*Pos = File->Used;

	return ERR_EMPTY;
}

GSM_Error DUMMY_SendFilePart(GSM_StateMachine *s, GSM_File *File, int *Pos, int *Handle)
{
	EncodeUnicode(File->ID_FullName, "incoming/", 9);
	return DUMMY_AddFilePart(s, File, Pos, Handle);
}

GSM_Error DUMMY_GetFilePart(GSM_StateMachine *s, GSM_File *File, int *Handle, int *Size)
{
	char *path, *name, *pos;
	GSM_Error error;

	*Handle = 0;

	path = DUMMY_GetFSFilePath(s, File->ID_FullName);

	error = GSM_ReadFile(path, File);
	*Size = File->Used;

	name = strrchr(path, '/');
	if (name == NULL) name = path;
	else name++;
	EncodeUnicode(File->Name, name, strlen(name));
	pos = path;
	while (*pos != 0 && (pos = strchr(pos + 1, '/')) != NULL) File->Level++;

	free(path);
	path=NULL;

	if (error == ERR_NONE) return ERR_EMPTY;
	return error;
}

GSM_Error DUMMY_GetFolderListing(GSM_StateMachine *s, GSM_File *File, gboolean start)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;
	char *path;
	struct dirent *dp;
	struct stat sb;

	if (start) {
		if (Priv->dir[DUMMY_MAX_FS_DEPTH] != NULL) {
			closedir(Priv->dir[DUMMY_MAX_FS_DEPTH]);
		}
		path = DUMMY_GetFSFilePath(s, File->ID_FullName);
		smprintf(s, "listing %s\n", path);
		strcpy(Priv->dirnames[DUMMY_MAX_FS_DEPTH], path);
		Priv->dir[DUMMY_MAX_FS_DEPTH] = opendir(path);
		free(path);
		path=NULL;

		if (Priv->dir[DUMMY_MAX_FS_DEPTH] == NULL) {
			return DUMMY_Error(s, "opendir failed");
		}
	}

read_next_entry:
	dp = readdir(Priv->dir[DUMMY_MAX_FS_DEPTH]);

	if (dp == NULL) {
		closedir(Priv->dir[DUMMY_MAX_FS_DEPTH]);
		Priv->dir[DUMMY_MAX_FS_DEPTH] = NULL;
		return ERR_EMPTY;
	}

	if (strcmp(dp->d_name, "..") == 0 || strcmp(dp->d_name, ".") == 0)
		goto read_next_entry;

	/* Stat file */
	path = DUMMY_GetFSPath(s, dp->d_name, DUMMY_MAX_FS_DEPTH);
	if (stat(path, &sb) < 0) {
		free(path);
		path=NULL;
		return DUMMY_Error(s, "stat failed");
	}

	/* Fill file structure */
	File->Used = sb.st_size;
	EncodeUnicode(File->Name, dp->d_name, strlen(dp->d_name));
	File->Folder = FALSE;
	File->Level = 0; /*Priv->fs_depth + 1;*/
	File->Type = GSM_File_Other; /* @todo TODO we should somehow detect this? */
	/* We need to skip device prefix and /fs/ prefix */
	EncodeUnicode(File->ID_FullName, path + Priv->devlen + 4, strlen(path + Priv->devlen + 4));
	File->Buffer = NULL;
	Fill_GSM_DateTime(&(File->Modified), sb.st_mtime);
	File->ModifiedEmpty = FALSE;
	File->Protected = FALSE;
	File->Hidden = FALSE;
	File->System = FALSE;
	File->ReadOnly = FALSE; /* @todo TODO get this from permissions? */

	/* Open nested directory for next loop if needed */
	if (S_ISDIR(sb.st_mode)) {
		File->Folder = TRUE;
		if (Priv->fs_depth == DUMMY_MAX_FS_DEPTH - 1) {
			smprintf(s, "We hit DUMMY_MAX_FS_DEPTH limit!\n");
			free(path);
			path=NULL;
			return ERR_MOREMEMORY;
		}
		Priv->fs_depth++;
		Priv->dir[Priv->fs_depth] = opendir(path);
		if (Priv->dir[Priv->fs_depth] == NULL) {
			free(path);
			path=NULL;
			return DUMMY_Error(s, "nested opendir failed");
		}
		strcpy(Priv->dirnames[Priv->fs_depth], path);
	}
	free(path);
	path=NULL;
	return ERR_NONE;
}

GSM_Error DUMMY_GetNextFileFolder(GSM_StateMachine *s, GSM_File *File, gboolean start)
{
	GSM_Phone_DUMMYData	*Priv = &s->Phone.Data.Priv.DUMMY;
	char *path;
	struct dirent *dp;
	struct stat sb;
	int i;

	if (start) {
		for (i = 0; i < DUMMY_MAX_FS_DEPTH; i++) {
			if (Priv->dir[i] != NULL) {
				closedir(Priv->dir[i]);
				Priv->dir[i] = NULL;
			}
		}
		path = DUMMY_GetFilePath(s, "fs");
		strcpy(Priv->dirnames[0], path);
		Priv->dir[0] = opendir(path);
		free(path);
		path=NULL;

		if (Priv->dir[0] == NULL) {
			return DUMMY_Error(s, "opendir failed");
		}
		Priv->fs_depth = 0;
	}

read_next_entry:
	dp = readdir(Priv->dir[Priv->fs_depth]);

	if (dp == NULL) {
		closedir(Priv->dir[Priv->fs_depth]);
		Priv->dir[Priv->fs_depth] = NULL;
		if (Priv->fs_depth == 0) return ERR_EMPTY;
		Priv->fs_depth--;
		goto read_next_entry;
	}

	if (strcmp(dp->d_name, "..") == 0 || strcmp(dp->d_name, ".") == 0)
		goto read_next_entry;

	/* Stat file */
	path = DUMMY_GetFSPath(s, dp->d_name, Priv->fs_depth);
	if (stat(path, &sb) < 0) {
		free(path);
		path=NULL;
		return DUMMY_Error(s, "stat failed");
	}

	/* Fill file structure */
	File->Used = 0;
	EncodeUnicode(File->Name, dp->d_name, strlen(dp->d_name));
	File->Folder = FALSE;
	File->Level = Priv->fs_depth + 1;
	File->Type = GSM_File_Other; /* @todo TODO we should somehow detect this? */
	/* We need to skip device prefix and /fs/ prefix */
	EncodeUnicode(File->ID_FullName, path + Priv->devlen + 4, strlen(path + Priv->devlen + 4));
	File->Buffer = NULL;
	Fill_GSM_DateTime(&(File->Modified), sb.st_mtime);
	File->ModifiedEmpty = FALSE;
	File->Protected = FALSE;
	File->Hidden = FALSE;
	File->System = FALSE;
	File->ReadOnly = FALSE; /* @todo TODO get this from permissions? */

	/* Open nested directory for next loop if needed */
	if (S_ISDIR(sb.st_mode)) {
		File->Folder = TRUE;
		if (Priv->fs_depth == DUMMY_MAX_FS_DEPTH - 1) {
			smprintf(s, "We hit DUMMY_MAX_FS_DEPTH limit!\n");
			free(path);
			path=NULL;
			return ERR_MOREMEMORY;
		}
		Priv->fs_depth++;
		Priv->dir[Priv->fs_depth] = opendir(path);
		if (Priv->dir[Priv->fs_depth] == NULL) {
			free(path);
			path=NULL;
			return DUMMY_Error(s, "nested opendir failed");
		}
		strcpy(Priv->dirnames[Priv->fs_depth], path);
	}
	free(path);
	path=NULL;
	return ERR_NONE;
}

GSM_Error DUMMY_DeleteFile(GSM_StateMachine *s, unsigned char *ID)
{
	char *path;
	GSM_Error error;

	path = DUMMY_GetFSFilePath(s, ID);
	if (unlink(path) != 0) {
		free(path);
		path=NULL;
		error = DUMMY_Error(s, "unlink failed");
		if (error == ERR_EMPTY) return ERR_FILENOTEXIST;
		return error;
	}
	free(path);
	path=NULL;
	return ERR_NONE;
}

GSM_Error DUMMY_DeleteFolder(GSM_StateMachine *s, unsigned char *ID)
{
	char *path;
	GSM_Error error;

	path = DUMMY_GetFSFilePath(s, ID);
	smprintf(s, "Deleting directory %s\n", path);
	if (rmdir(path) != 0) {
		free(path);
		path=NULL;
		error = DUMMY_Error(s, "rmdir failed");
		if (error == ERR_EMPTY) return ERR_FILENOTEXIST;
		return error;
	}
	free(path);
	path=NULL;
	return ERR_NONE;
}

GSM_Error DUMMY_AddFolder(GSM_StateMachine *s, GSM_File *File)
{
	char *path;
	size_t pos;

	pos = UnicodeLength(File->ID_FullName);
	if (pos > 0 && (File->ID_FullName[2*pos - 2] != 0 || File->ID_FullName[2*pos - 1] != '/')) {
		File->ID_FullName[2*pos + 1] = '/';
		File->ID_FullName[2*pos + 0] = 0;
		pos++;
	}
	CopyUnicodeString(File->ID_FullName + 2 * pos, File->Name);

	path = DUMMY_GetFSFilePath(s, File->ID_FullName);
	if (MKDIR(path) != 0) {
		free(path);
		path=NULL;
		return DUMMY_Error(s, "mkdir failed");
	}
	free(path);
	path=NULL;
	return ERR_NONE;
}

GSM_Error DUMMY_GetMemoryStatus(GSM_StateMachine *s, GSM_MemoryStatus *Status)
{
	char dirname[20];
	sprintf(dirname, "pbk/%s", GSM_MemoryTypeToString(Status->MemoryType));
	Status->MemoryUsed = DUMMY_GetCount(s, dirname);
	Status->MemoryFree = DUMMY_MAX_MEM - Status->MemoryUsed;
	return ERR_NONE;
}

GSM_Error DUMMY_GetMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	GSM_Backup Backup;
	char *filename=NULL;
	GSM_Error error;
	int location=0,i=0;
	GSM_MemoryType type;

	type = entry->MemoryType;
	location = entry->Location;
	filename = DUMMY_MemoryPath(s, entry);

	error = GSM_ReadBackupFile(filename, &Backup, GSM_Backup_VCard);

	free(filename);
	filename=NULL;

	if (error != ERR_NONE) {
		if (error == ERR_CANTOPENFILE) return ERR_EMPTY;
		return error;
	}
	if (Backup.PhonePhonebook[0] == NULL && Backup.SIMPhonebook[0] == NULL) return ERR_EMPTY;


	if (Backup.PhonePhonebook[0] != NULL) {
		*entry = *(Backup.PhonePhonebook[0]);
	} else {
		*entry = *(Backup.SIMPhonebook[0]);
	}

	for (i = 0; i < entry->EntriesNum; i++) {
		if (entry->Entries[i].EntryType == PBK_Photo) {
			entry->Entries[i].Picture.Buffer = (unsigned char *)malloc(entry->Entries[i].Picture.Length);
			if (Backup.PhonePhonebook[0] != NULL) {
				memcpy(entry->Entries[i].Picture.Buffer, Backup.PhonePhonebook[0]->Entries[i].Picture.Buffer, entry->Entries[i].Picture.Length);
			} else {
				memcpy(entry->Entries[i].Picture.Buffer, Backup.SIMPhonebook[0]->Entries[i].Picture.Buffer, entry->Entries[i].Picture.Length);
			}
		}
	}
	entry->Location = location;
	entry->MemoryType = type;
	GSM_FreeBackup(&Backup);

	return ERR_NONE;
}

GSM_Error DUMMY_GetNextMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry, gboolean start)
{
	char dirname[20]={0};

	if (GSM_IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_DISABLE_GETNEXT)) {
		return ERR_NOTSUPPORTED;
	}

	if (start) {
		entry->Location = 0;
	}

	sprintf(dirname, "pbk/%s", GSM_MemoryTypeToString(entry->MemoryType));

	entry->Location = DUMMY_GetNext(s, dirname, entry->Location);

	return DUMMY_GetMemory(s, entry);
}

GSM_Error DUMMY_DeleteMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	char *filename=NULL;
	GSM_Error error;

	filename = DUMMY_MemoryPath(s, entry);

	if (unlink(filename) == 0) {
		error = ERR_NONE;
	} else {
		error = DUMMY_Error(s, "memory unlink failed");
	}

	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_SetMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	char *filename=NULL;
	GSM_Error error;
	GSM_Backup backup;

	GSM_ClearBackup(&backup);

	error = DUMMY_DeleteMemory(s, entry);
	if (error != ERR_EMPTY && error != ERR_NONE) return error;

	filename = DUMMY_MemoryPath(s, entry);

	backup.PhonePhonebook[0] = entry;
	backup.PhonePhonebook[1] = NULL;

	error = GSM_SaveBackupFile(filename, &backup, GSM_Backup_VCard);
	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_AddMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	char dirname[20]={0};

	sprintf(dirname, "pbk/%s", GSM_MemoryTypeToString(entry->MemoryType));
	entry->Location = DUMMY_GetFirstFree(s, dirname);

	if (entry->Location == -1) return ERR_FULL;

	return DUMMY_SetMemory(s, entry);
}

GSM_Error DUMMY_DeleteAllMemory(GSM_StateMachine *s, GSM_MemoryType type)
{
	char dirname[20];

	sprintf(dirname, "pbk/%s", GSM_MemoryTypeToString(type));

	return DUMMY_DeleteAll(s, dirname);
}

GSM_Error DUMMY_GetToDoStatus(GSM_StateMachine *s, GSM_ToDoStatus *Status)
{
	Status->Used = DUMMY_GetCount(s, "todo");
	Status->Free = DUMMY_MAX_TODO - Status->Used;
	return ERR_NONE;
}

GSM_Error DUMMY_GetToDo(GSM_StateMachine *s, GSM_ToDoEntry *entry)
{
	GSM_Backup Backup;
	char *filename=NULL;
	GSM_Error error;
	int location=0;

	location = entry->Location;
	filename = DUMMY_ToDoPath(s, entry);

	error = GSM_ReadBackupFile(filename, &Backup, GSM_Backup_VCalendar);

	free(filename);
	filename=NULL;

	if (error != ERR_NONE) {
		if (error == ERR_CANTOPENFILE) return ERR_EMPTY;
		return error;
	}
	if (Backup.ToDo[0] == NULL) return ERR_EMPTY;

	*entry = *(Backup.ToDo[0]);
	entry->Location = location;
	GSM_FreeBackup(&Backup);

	return ERR_NONE;
}

GSM_Error DUMMY_GetNextToDo(GSM_StateMachine *s, GSM_ToDoEntry *entry, gboolean start)
{
	if (GSM_IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_DISABLE_GETNEXT)) {
		return ERR_NOTSUPPORTED;
	}

	if (start) {
		entry->Location = 0;
	}

	entry->Location = DUMMY_GetNext(s, "todo", entry->Location);

	return DUMMY_GetToDo(s, entry);
}

GSM_Error DUMMY_DeleteToDo(GSM_StateMachine *s, GSM_ToDoEntry *entry)
{
	char *filename=NULL;
	GSM_Error error;

	filename = DUMMY_ToDoPath(s, entry);

	if (unlink(filename) == 0) {
		error = ERR_NONE;
	} else {
		error = DUMMY_Error(s, "todo unlink failed");
	}

	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_SetToDo(GSM_StateMachine *s, GSM_ToDoEntry *entry)
{
	char *filename=NULL;
	GSM_Error error;
	GSM_Backup backup;

	GSM_ClearBackup(&backup);

	error = DUMMY_DeleteToDo(s, entry);
	if (error != ERR_EMPTY && error != ERR_NONE) return error;

	filename = DUMMY_ToDoPath(s, entry);

	backup.ToDo[0] = entry;
	backup.ToDo[1] = NULL;

	error = GSM_SaveBackupFile(filename, &backup, GSM_Backup_VCalendar);
	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_AddToDo(GSM_StateMachine *s, GSM_ToDoEntry *entry)
{
	entry->Location = DUMMY_GetFirstFree(s, "todo");

	if (entry->Location == -1) return ERR_FULL;

	return DUMMY_SetToDo(s, entry);
}

GSM_Error DUMMY_DeleteAllToDo(GSM_StateMachine *s)
{
	return DUMMY_DeleteAll(s, "todo");
}

GSM_Error DUMMY_GetCalendarStatus(GSM_StateMachine *s, GSM_CalendarStatus *Status)
{
	Status->Used = DUMMY_GetCount(s, "calendar");
	Status->Free = DUMMY_MAX_TODO - Status->Used;
	return ERR_NONE;
}

GSM_Error DUMMY_GetCalendar(GSM_StateMachine *s, GSM_CalendarEntry *entry)
{
	GSM_Backup Backup;
	char *filename=NULL;
	GSM_Error error;
	int location=0;

	location = entry->Location;
	filename = DUMMY_CalendarPath(s, entry);

	error = GSM_ReadBackupFile(filename, &Backup, GSM_Backup_VCalendar);

	free(filename);
	filename=NULL;

	if (error != ERR_NONE) {
		if (error == ERR_CANTOPENFILE) return ERR_EMPTY;
		return error;
	}
	if (Backup.Calendar[0] == NULL) return ERR_EMPTY;

	*entry = *(Backup.Calendar[0]);
	entry->Location = location;
	GSM_FreeBackup(&Backup);

	return ERR_NONE;
}

GSM_Error DUMMY_GetNextCalendar(GSM_StateMachine *s, GSM_CalendarEntry *entry, gboolean start)
{
	if (GSM_IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_DISABLE_GETNEXT)) {
		return ERR_NOTSUPPORTED;
	}

	if (start) {
		entry->Location = 0;
	}

	entry->Location = DUMMY_GetNext(s, "calendar", entry->Location);

	return DUMMY_GetCalendar(s, entry);
}

GSM_Error DUMMY_DeleteCalendar(GSM_StateMachine *s, GSM_CalendarEntry *entry)
{
	char *filename=NULL;
	GSM_Error error;

	filename = DUMMY_CalendarPath(s, entry);

	if (unlink(filename) == 0) {
		error = ERR_NONE;
	} else {
		error = DUMMY_Error(s, "calendar unlink failed");
	}

	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_SetCalendar(GSM_StateMachine *s, GSM_CalendarEntry *entry)
{
	char *filename=NULL;
	GSM_Error error;
	GSM_Backup backup;

	GSM_ClearBackup(&backup);

	error = DUMMY_DeleteCalendar(s, entry);
	if (error != ERR_EMPTY && error != ERR_NONE) return error;

	filename = DUMMY_CalendarPath(s, entry);

	backup.Calendar[0] = entry;
	backup.Calendar[1] = NULL;

	error = GSM_SaveBackupFile(filename, &backup, GSM_Backup_VCalendar);
	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_AddCalendar(GSM_StateMachine *s, GSM_CalendarEntry *entry)
{
	entry->Location = DUMMY_GetFirstFree(s, "calendar");

	if (entry->Location == -1) return ERR_FULL;

	return DUMMY_SetCalendar(s, entry);
}

GSM_Error DUMMY_DeleteAllCalendar(GSM_StateMachine *s)
{
	return DUMMY_DeleteAll(s, "calendar");
}

GSM_Error DUMMY_GetCalendarSettings(GSM_StateMachine *s, GSM_CalendarSettings *settings)
{
	settings->StartDay = 1;
	settings->AutoDelete = 0;
	return ERR_NONE;
}

GSM_Error DUMMY_SetCalendarSettings(GSM_StateMachine *s, GSM_CalendarSettings *settings)
{
	return ERR_NONE;
}

GSM_Error DUMMY_GetNoteStatus(GSM_StateMachine *s, GSM_ToDoStatus *Status)
{
	Status->Used = DUMMY_GetCount(s, "note");
	Status->Free = DUMMY_MAX_TODO - Status->Used;
	return ERR_NONE;
}

GSM_Error DUMMY_GetNote(GSM_StateMachine *s, GSM_NoteEntry *entry)
{
	GSM_Backup Backup;
	char *filename=NULL;
	GSM_Error error;
	int location=0;

	location = entry->Location;
	filename = DUMMY_NotePath(s, entry);

	error = GSM_ReadBackupFile(filename, &Backup, GSM_Backup_VNote);

	free(filename);
	filename=NULL;

	if (error != ERR_NONE) {
		if (error == ERR_CANTOPENFILE) return ERR_EMPTY;
		return error;
	}
	if (Backup.Note[0] == NULL) return ERR_EMPTY;

	*entry = *(Backup.Note[0]);
	entry->Location = location;
	GSM_FreeBackup(&Backup);

	return ERR_NONE;
}

GSM_Error DUMMY_GetNextNote(GSM_StateMachine *s, GSM_NoteEntry *entry, gboolean start)
{
	if (GSM_IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_DISABLE_GETNEXT)) {
		return ERR_NOTSUPPORTED;
	}

	if (start) {
		entry->Location = 0;
	}

	entry->Location = DUMMY_GetNext(s, "note", entry->Location);

	return DUMMY_GetNote(s, entry);
}

GSM_Error DUMMY_DeleteNote(GSM_StateMachine *s, GSM_NoteEntry *entry)
{
	char *filename=NULL;
	GSM_Error error;

	filename = DUMMY_NotePath(s, entry);

	if (unlink(filename) == 0) {
		error = ERR_NONE;
	} else {
		error = DUMMY_Error(s, "note unlink failed");
	}
	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_SetNote(GSM_StateMachine *s, GSM_NoteEntry *entry)
{
	char *filename=NULL;
	GSM_Error error;
	GSM_Backup backup;

	GSM_ClearBackup(&backup);

	error = DUMMY_DeleteNote(s, entry);
	if (error != ERR_EMPTY && error != ERR_NONE) return error;

	filename = DUMMY_NotePath(s, entry);

	backup.Note[0] = entry;
	backup.Note[1] = NULL;

	error = GSM_SaveBackupFile(filename, &backup, GSM_Backup_VNote);
	free(filename);
	filename=NULL;
	return error;
}

GSM_Error DUMMY_AddNote(GSM_StateMachine *s, GSM_NoteEntry *entry)
{
	entry->Location = DUMMY_GetFirstFree(s, "note");

	if (entry->Location == -1) return ERR_FULL;

	return DUMMY_SetNote(s, entry);
}

GSM_Error DUMMY_DeleteAllNote(GSM_StateMachine *s)
{
	return DUMMY_DeleteAll(s, "note");
}


GSM_Error DUMMY_GetLocale(GSM_StateMachine *s, GSM_Locale *locale)
{
	GSM_Phone_DUMMYData *Priv = &s->Phone.Data.Priv.DUMMY;
	*locale = Priv->locale;
	return ERR_NONE;
}

GSM_Error DUMMY_SetLocale(GSM_StateMachine *s, GSM_Locale *locale)
{
	GSM_Phone_DUMMYData *Priv = &s->Phone.Data.Priv.DUMMY;
	Priv->locale = *locale;
	return ERR_NONE;
}

GSM_Error DUMMY_GetFileSystemStatus(GSM_StateMachine *s, GSM_FileSystemStatus *Status)
{
	Status->Free = 10101;
	Status->Used = 1000000;
	Status->UsedImages = 0;
	Status->UsedSounds = 0;
	Status->UsedThemes = 0;
	return ERR_NONE;
}

GSM_Error DUMMY_GetBatteryCharge(GSM_StateMachine *s, GSM_BatteryCharge *bat)
{
	bat->BatteryPercent = 100;
	bat->ChargeState = GSM_BatteryConnected;
	bat->BatteryVoltage = 4200;
	bat->ChargeVoltage = 4200;
	bat->ChargeCurrent = 0;
	bat->PhoneCurrent = 500;
	bat->BatteryTemperature = 22;
	bat->PhoneTemperature = 22;
	bat->BatteryCapacity = 2000;
	bat->BatteryType = GSM_BatteryLiPol;
	return ERR_NONE;
}

/*@}*/

GSM_Reply_Function DUMMYReplyFunctions[] = {
{NULL,				"\x00"			,0x00,0x00,ID_None		 }
};

GSM_Phone_Functions DUMMYPhone = {
	"dummy",
	DUMMYReplyFunctions,
	NOTSUPPORTED,			/* 	Install			*/
	DUMMY_Initialise,
	DUMMY_Terminate,
	DUMMY_DispatchMessage,
	NOTSUPPORTED,			/* 	ShowStartInfo		*/
	DUMMY_GetManufacturer,
	DUMMY_GetModel,
	DUMMY_GetFirmware,
	DUMMY_GetIMEI,
	DUMMY_GetOriginalIMEI,
	DUMMY_GetManufactureMonth,
        DUMMY_GetProductCode,
        DUMMY_GetHardware,
        DUMMY_GetPPM,
	DUMMY_GetSIMIMSI,
	DUMMY_GetDateTime,
	DUMMY_SetDateTime,
	DUMMY_GetAlarm,
	DUMMY_SetAlarm,
	DUMMY_GetLocale,
	DUMMY_SetLocale,
	DUMMY_PressKey,
	DUMMY_Reset,
	DUMMY_ResetPhoneSettings,
	DUMMY_EnterSecurityCode,
	DUMMY_GetSecurityStatus,
	DUMMY_GetDisplayStatus,
	DUMMY_SetAutoNetworkLogin,
	DUMMY_GetBatteryCharge,
	DUMMY_GetSignalStrength,
	DUMMY_GetNetworkInfo,
 	NOTSUPPORTED,       		/*  	GetCategory 		*/
 	NOTSUPPORTED,       		/*  	AddCategory 		*/
 	NOTSUPPORTED,        		/*  	GetCategoryStatus 	*/
	DUMMY_GetMemoryStatus,
	DUMMY_GetMemory,
	DUMMY_GetNextMemory,
	DUMMY_SetMemory,
	DUMMY_AddMemory,
	DUMMY_DeleteMemory,
	DUMMY_DeleteAllMemory,
	NOTSUPPORTED,			/* 	GetSpeedDial		*/
	NOTSUPPORTED,			/* 	SetSpeedDial		*/
	DUMMY_GetSMSC,
	DUMMY_SetSMSC,
	DUMMY_GetSMSStatus,
	DUMMY_GetSMS,
	DUMMY_GetNextSMS,
	DUMMY_SetSMS,
	DUMMY_AddSMS,
	DUMMY_DeleteSMS,
	DUMMY_SendSMS,
	DUMMY_SendSavedSMS,
	DUMMY_SetFastSMSSending,
	DUMMY_SetIncomingSMS,
	DUMMY_SetIncomingCB,
	DUMMY_GetSMSFolders,
 	NOTSUPPORTED,			/* 	AddSMSFolder		*/
 	NOTSUPPORTED,			/* 	DeleteSMSFolder		*/
	DUMMY_DialVoice,
	DUMMY_DialService,
	DUMMY_AnswerCall,
	DUMMY_CancelCall,
 	NOTSUPPORTED,			/* 	HoldCall 		*/
 	NOTSUPPORTED,			/* 	UnholdCall 		*/
 	NOTSUPPORTED,			/* 	ConferenceCall 		*/
 	NOTSUPPORTED,			/* 	SplitCall		*/
 	NOTSUPPORTED,			/* 	TransferCall		*/
 	NOTSUPPORTED,			/* 	SwitchCall		*/
	DUMMY_GetCallDivert,
 	DUMMY_SetCallDivert,
 	DUMMY_CancelAllDiverts,
	DUMMY_SetIncomingCall,
	DUMMY_SetIncomingUSSD,
	DUMMY_SendDTMF,
	DUMMY_GetRingtone,
	DUMMY_SetRingtone,
	NOTSUPPORTED,			/* 	GetRingtonesInfo	*/
	NOTSUPPORTED,			/* 	DeleteUserRingtones	*/
	NOTSUPPORTED,			/* 	PlayTone		*/
	DUMMY_GetWAPBookmark,
	NOTSUPPORTED,			/* 	SetWAPBookmark		*/
	NOTSUPPORTED,			/* 	DeleteWAPBookmark	*/
	NOTSUPPORTED,			/* 	GetWAPSettings		*/
	NOTSUPPORTED,			/* 	SetWAPSettings		*/
	NOTSUPPORTED,			/*	GetSyncMLSettings	*/
	NOTSUPPORTED,			/*	SetSyncMLSettings	*/
	NOTSUPPORTED,			/*	GetChatSettings		*/
	NOTSUPPORTED,			/*	SetChatSettings		*/
	NOTSUPPORTED,			/* 	GetMMSSettings		*/
	NOTSUPPORTED,			/* 	SetMMSSettings		*/
	NOTSUPPORTED,			/*	GetMMSFolders		*/
	NOTSUPPORTED,			/*	GetNextMMSFileInfo	*/
	DUMMY_GetBitmap,
	DUMMY_SetBitmap,
	DUMMY_GetToDoStatus,
	DUMMY_GetToDo,
	DUMMY_GetNextToDo,
	DUMMY_SetToDo,
	DUMMY_AddToDo,
	DUMMY_DeleteToDo,
	DUMMY_DeleteAllToDo,
	DUMMY_GetCalendarStatus,
	DUMMY_GetCalendar,
	DUMMY_GetNextCalendar,
	DUMMY_SetCalendar,
	DUMMY_AddCalendar,
	DUMMY_DeleteCalendar,
	DUMMY_DeleteAllCalendar,
	DUMMY_GetCalendarSettings,
	DUMMY_SetCalendarSettings,
	DUMMY_GetNoteStatus,
	DUMMY_GetNote,
	DUMMY_GetNextNote,
	DUMMY_SetNote,
	DUMMY_AddNote,
	DUMMY_DeleteNote,
	DUMMY_DeleteAllNote,
	NOTSUPPORTED,			/* 	GetProfile		*/
	NOTSUPPORTED,			/* 	SetProfile		*/
	NOTSUPPORTED,			/* 	GetFMStation		*/
	NOTSUPPORTED,			/* 	SetFMStation		*/
	NOTSUPPORTED,			/* 	ClearFMStations		*/
	DUMMY_GetNextFileFolder,
	DUMMY_GetFolderListing,
	NOTSUPPORTED,			/*	GetNextRootFolder	*/
	DUMMY_SetFileAttributes,
	DUMMY_GetFilePart,
	DUMMY_AddFilePart,
	DUMMY_SendFilePart,
	DUMMY_GetFileSystemStatus,
	DUMMY_DeleteFile,
	DUMMY_AddFolder,
	DUMMY_DeleteFolder,
	NOTSUPPORTED,			/* 	GetGPRSAccessPoint	*/
	NOTSUPPORTED,			/* 	SetGPRSAccessPoint	*/
	NOTSUPPORTED,			/* 	GetScreenshot		*/
	NOTSUPPORTED,			/* 	SetPower		*/
	NOTSUPPORTED			/* 	PostConnect	*/
};

/*@}*/
/*@}*/

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
