#define _GNU_SOURCE /* For strcasestr */
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif

#include "../helper/locales.h"

#include "message-display.h"
#include "memory-display.h"
#include "formats.h"

#include <gammu.h>

/**
 * Prints location information about message.
 */
void PrintSMSLocation(const GSM_SMSMessage *sms, const GSM_SMSFolders *folders)
{
	printf(_("Location %i, folder \"%s\""),
			sms->Location,
			DecodeUnicodeConsole(folders->Folder[sms->Folder - 1].Name)
			);

	switch (sms->Memory) {
		case MEM_SM:
			printf(", %s", _("SIM memory"));
			break;
		case MEM_ME:
			printf(", %s", _("phone memory"));
			break;
		case MEM_MT:
			printf(", %s", _("phone or SIM memory"));
			break;
		default:
			break;
	}

	if (sms->InboxFolder) {
		printf(", %s", _("Inbox folder"));
	}

	printf("\n");
}

/**
 * Searches for memory entry in NULL terminated entries list.
 */
const GSM_MemoryEntry *SearchPhoneNumber(const unsigned char *number, const GSM_MemoryEntry **List, int *pos)
{
	int i;
	for (i = 0; List[i] != NULL; i++) {
		for (*pos = 0; *pos <  List[i]->EntriesNum; (*pos)++) {
			switch (List[i]->Entries[*pos].EntryType) {
				case PBK_Number_General:
				case PBK_Number_Mobile:
				case PBK_Number_Fax:
				case PBK_Number_Pager:
				case PBK_Number_Other:
					if (mywstrncmp(List[i]->Entries[*pos].Text,number,-1)) {
						return List[i];
					}
				default:
					break;
			}
		}
	}
	return NULL;
}

/**
 * Prints single phone number optionally showing name of contact from backup data.
 */
#ifdef GSM_ENABLE_BACKUP
void PrintPhoneNumber(const unsigned char *number, const GSM_Backup *Info)
#else
void PrintPhoneNumber(const unsigned char *number, const void *Info)
#endif
{
#ifdef GSM_ENABLE_BACKUP
	const GSM_MemoryEntry *pbk;
	int pos;
#endif

	printf("\"%s\"", DecodeUnicodeConsole(number));

	if (Info == NULL) return;

#ifdef GSM_ENABLE_BACKUP
	/* First try phone phonebook */
	pbk = SearchPhoneNumber(number, (const GSM_MemoryEntry **)Info->PhonePhonebook, &pos);
	if (pbk == NULL) {
		/* Fall back to SIM */
		pbk = SearchPhoneNumber(number, (const GSM_MemoryEntry **)Info->SIMPhonebook, &pos);
	}

	/* Nothing found */
	if (pbk == NULL) return;

	/* Print name */
	printf("%s", DecodeUnicodeConsole(GSM_PhonebookGetEntryName(pbk)));

	/* Print phone type */
	switch (pbk->Entries[pos].EntryType) {
		case PBK_Number_Mobile:
			printf(" (%s)", _("mobile"));
			break;
		case PBK_Number_Fax:
			printf(" (%s)", _("fax"));
			break;
		case PBK_Number_Pager:
			printf(" (%s)", _("pager"));
			break;
		case PBK_Number_General:
			printf(" (%s)", _("general"));
			break;
		default:
			break;
	}
	switch (pbk->Entries[pos].Location) {
		case PBK_Location_Home:
			printf(" [%s]", _("home"));
			break;
		case PBK_Location_Work:
			printf(" [%s]", _("work"));
			break;
		case PBK_Location_Unknown:
			break;
	}
#endif
}

void DisplayMessageHex(GSM_SMSMessage *sms) {
	size_t len;
	char *hexbuf;

	len = strlen(sms->Text);
	hexbuf = malloc(len * 2 + 1);
	printf("%s\n", _("8 bit SMS, cannot be displayed here"));
	if (hexbuf == NULL) {
		return;
	}
	EncodeHexBin(hexbuf, sms->Text, len);
	printf("(hex: %s)\n", hexbuf);
	free(hexbuf);
}

#ifdef GSM_ENABLE_BACKUP
void DisplaySingleSMSInfo(GSM_SMSMessage *sms, gboolean displaytext, gboolean displayudh, const GSM_Backup *Info)
#else
void DisplaySingleSMSInfo(GSM_SMSMessage *sms, gboolean displaytext, gboolean displayudh, const void *Info)
#endif
{
	GSM_SiemensOTASMSInfo 	SiemensOTA;
	int			i;

	switch (sms->PDU) {
	case SMS_Status_Report:
		printf("%s\n", _("SMS status report"));

		printf(LISTFORMAT, _("Status"));
		switch (sms->State) {
			case SMS_Sent	: printf("%s", _("Sent"));	break;
			case SMS_Read	: printf("%s", _("Read"));	break;
			case SMS_UnRead	: printf("%s", _("UnRead"));	break;
			case SMS_UnSent	: printf("%s", _("UnSent"));	break;
		}
		printf("\n");

		printf(LISTFORMAT, _("Remote number"));
		PrintPhoneNumber(sms->Number, Info);
		printf("\n");

		printf(LISTFORMAT "%d\n", _("Reference number"),sms->MessageReference);
		printf(LISTFORMAT "%s\n", _("Sent"),OSDateTime(sms->DateTime,TRUE));
		printf(LISTFORMAT "\"%s\"\n", _("SMSC number"),DecodeUnicodeConsole(sms->SMSC.Number));
		printf(LISTFORMAT "%s\n", _("SMSC response"),OSDateTime(sms->SMSCTime,TRUE));
		printf(LISTFORMAT "%s\n", _("Delivery status"),DecodeUnicodeConsole(sms->Text));
		printf(LISTFORMAT, _("Details"));
		if (sms->DeliveryStatus & 0x40) {
			if (sms->DeliveryStatus & 0x20) {
				printf("%s", _("Temporary error, "));
			} else {
	     			printf("%s", _("Permanent error, "));
			}
	    	} else if (sms->DeliveryStatus & 0x20) {
			printf("%s", _("Temporary error, "));
		}
		switch (sms->DeliveryStatus) {
			case 0x00: printf("%s", _("SM received by the SME"));				break;
			case 0x01: printf("%s", _("SM forwarded by the SC to the SME but the SC is unable to confirm delivery"));break;
			case 0x02: printf("%s", _("SM replaced by the SC"));				break;
			case 0x20: printf("%s", _("Congestion"));					break;
			case 0x21: printf("%s", _("SME busy"));					break;
			case 0x22: printf("%s", _("No response from SME"));				break;
			case 0x23: printf("%s", _("Service rejected"));				break;
			case 0x24: printf("%s", _("Quality of service not available"));		break;
			case 0x25: printf("%s", _("Error in SME"));					break;
		        case 0x40: printf("%s", _("Remote procedure error"));				break;
		        case 0x41: printf("%s", _("Incompatible destination"));			break;
		        case 0x42: printf("%s", _("Connection rejected by SME"));			break;
		        case 0x43: printf("%s", _("Not obtainable"));					break;
		        case 0x44: printf("%s", _("Quality of service not available"));		break;
		        case 0x45: printf("%s", _("No internetworking available"));			break;
		        case 0x46: printf("%s", _("SM Validity Period Expired"));			break;
		        case 0x47: printf("%s", _("SM deleted by originating SME"));			break;
		        case 0x48: printf("%s", _("SM Deleted by SC Administration"));			break;
		        case 0x49: printf("%s", _("SM does not exist"));				break;
		        case 0x60: printf("%s", _("Congestion"));					break;
		        case 0x61: printf("%s", _("SME busy"));					break;
		        case 0x62: printf("%s", _("No response from SME"));				break;
		        case 0x63: printf("%s", _("Service rejected"));				break;
		        case 0x64: printf("%s", _("Quality of service not available"));		break;
		        case 0x65: printf("%s", _("Error in SME"));					break;
		        default  : printf(_("Reserved/Specific to SC: %x"),sms->DeliveryStatus);	break;
		}
		printf("\n");
		break;
	case SMS_Deliver:
		printf("%s\n", _("SMS message"));
		if (sms->State==SMS_UnSent && sms->Memory==MEM_ME) {
			printf(LISTFORMAT "%s\n", _("Saved"), OSDateTime(sms->DateTime,TRUE));
		} else {
			printf(LISTFORMAT "\"%s\"", _("SMSC number"), DecodeUnicodeConsole(sms->SMSC.Number));
			if (sms->ReplyViaSameSMSC) printf("%s", _(" (set for reply)"));
			printf("\n");
			printf(LISTFORMAT "%s\n", _("Sent"), OSDateTime(sms->DateTime,TRUE));
		}
		/* No break. The only difference for SMS_Deliver and SMS_Submit is,
		 * that SMS_Deliver contains additional data. We wrote them and then go
		 * for data shared with SMS_Submit
		 */
	case SMS_Submit:
		if (sms->ReplaceMessage != 0) printf(LISTFORMAT "%i\n", _("SMS replacing ID"),sms->ReplaceMessage);
		/* If we went here from "case SMS_Deliver", we don't write "SMS Message" */
		if (sms->PDU==SMS_Submit) {
			printf("%s\n", _("SMS message"));
			if (sms->State == SMS_Sent) {
				printf(LISTFORMAT "%d\n", _("Reference number"),sms->MessageReference);
			}
			if (CheckDate(&(sms->DateTime)) && CheckTime(&(sms->DateTime))) {
				printf(LISTFORMAT "%s\n", _("Sent"), OSDateTime(sms->DateTime,TRUE));
			}
		}
		if (sms->Name[0] != 0x00 || sms->Name[1] != 0x00) {
			printf(LISTFORMAT "\"%s\"\n", _("Name"),DecodeUnicodeConsole(sms->Name));
		}
		if (sms->Class != -1) {
			printf(LISTFORMAT "%i\n", _("Class"),sms->Class);
		}
		printf(LISTFORMAT, _("Coding"));
		switch (sms->Coding) {
			case SMS_Coding_Unicode_No_Compression 	:
				printf("%s", _("Unicode (no compression)"));
				break;
			case SMS_Coding_Unicode_Compression 	:
				printf("%s", _("Unicode (compression)"));
				break;
			case SMS_Coding_Default_No_Compression 	:
				printf("%s", _("Default GSM alphabet (no compression)"));
				break;
			case SMS_Coding_Default_Compression 	:
				printf("%s", _("Default GSM alphabet (compression)"));
				break;
			case SMS_Coding_8bit			:
				/* l10n: 8-bit message coding */
				printf("%s", _("8-bit"));
				break;
		}
		printf("\n");
		if (sms->State==SMS_UnSent && sms->Memory==MEM_ME) {
		} else {
			printf(LISTFORMAT, ngettext("Remote number", "Remote numbers", sms->OtherNumbersNum + 1));
			PrintPhoneNumber(sms->Number, Info);
			for (i=0;i<sms->OtherNumbersNum;i++) {
				printf(", ");
				PrintPhoneNumber(sms->OtherNumbers[i], Info);
			}
			printf("\n");
		}
		printf(LISTFORMAT, _("Status"));
		switch (sms->State) {
			case SMS_Sent	:	printf("%s", _("Sent"));	break;
			case SMS_Read	:	printf("%s", _("Read"));	break;
			case SMS_UnRead	:	printf("%s", _("UnRead"));	break;
			case SMS_UnSent	:	printf("%s", _("UnSent"));	break;
		}
		printf("\n");
		if (sms->UDH.Type != UDH_NoUDH) {
			printf(LISTFORMAT, _("User Data Header"));
			switch (sms->UDH.Type) {
			case UDH_ConcatenatedMessages	   : printf("%s", _("Concatenated (linked) message")); 	 break;
			case UDH_ConcatenatedMessages16bit : printf("%s", _("Concatenated (linked) message")); 	 break;
			case UDH_DisableVoice		   : printf("%s", _("Disables voice indicator"));	 	 break;
			case UDH_EnableVoice		   : printf("%s", _("Enables voice indicator"));	 	 break;
			case UDH_DisableFax		   : printf("%s", _("Disables fax indicator"));	 	 break;
			case UDH_EnableFax		   : printf("%s", _("Enables fax indicator"));	 		 break;
			case UDH_DisableEmail		   : printf("%s", _("Disables email indicator"));	 	 break;
			case UDH_EnableEmail		   : printf("%s", _("Enables email indicator"));	 	 break;
			case UDH_VoidSMS		   : printf("%s", _("Void SMS"));			 	 break;
			case UDH_NokiaWAP		   : printf("%s", _("Nokia WAP bookmark"));		 	 break;
			case UDH_NokiaOperatorLogoLong	   : printf("%s", _("Nokia operator logo"));	 	 	 break;
			case UDH_NokiaWAPLong		   : printf("%s", _("Nokia WAP bookmark or WAP/MMS settings")); break;
			case UDH_NokiaRingtone		   : printf("%s", _("Nokia ringtone"));		 	 break;
			case UDH_NokiaRingtoneLong	   : printf("%s", _("Nokia ringtone"));		 	 break;
			case UDH_NokiaOperatorLogo	   : printf("%s", _("Nokia GSM operator logo"));	 	 break;
			case UDH_NokiaCallerLogo	   : printf("%s", _("Nokia caller logo"));		 	 break;
			case UDH_NokiaProfileLong	   : printf("%s", _("Nokia profile"));		 		 break;
			case UDH_NokiaCalendarLong	   : printf("%s", _("Nokia calendar note"));	 		 break;
			case UDH_NokiaPhonebookLong	   : printf("%s", _("Nokia phonebook entry"));	 		 break;
			case UDH_UserUDH		   : printf("%s", _("User UDH"));			 	 break;
			case UDH_MMSIndicatorLong	   : printf("%s", _("MMS indicator"));			 	 break;
			case UDH_NoUDH:								 		 break;
			}
			if (sms->UDH.Type != UDH_NoUDH) {
				if (sms->UDH.ID8bit != -1) printf(_(", ID (8 bit) %i"),sms->UDH.ID8bit);
				if (sms->UDH.ID16bit != -1) printf(_(", ID (16 bit) %i"),sms->UDH.ID16bit);
				if (sms->UDH.PartNumber != -1 && sms->UDH.AllParts != -1) {
					if (displayudh) {
						printf(_(", part %i of %i"),sms->UDH.PartNumber,sms->UDH.AllParts);
					} else {
						printf(_(", %i parts"),sms->UDH.AllParts);
					}
				}
			}
			printf("\n");
		}
		if (displaytext) {
			printf("\n");
			if (sms->Coding != SMS_Coding_8bit) {
				printf("%s\n", DecodeUnicodeConsole(sms->Text));
			} else if (GSM_DecodeSiemensOTASMS(GSM_GetGlobalDebug(), &SiemensOTA, sms)) {
				printf("%s\n", _("Siemens file"));
			} else {
				DisplayMessageHex(sms);
			}
		}
		break;
#ifndef CHECK_CASES
	default:
		printf(_("Unknown PDU type: 0x%x\n"), sms->PDU);
		break;
#endif
	}
	fflush(stdout);
}

#ifdef GSM_ENABLE_BACKUP
void DisplayMultiSMSInfo (GSM_MultiSMSMessage *sms, gboolean eachsms, gboolean ems, const GSM_Backup *Info, GSM_StateMachine *sm)
#else
void DisplayMultiSMSInfo (GSM_MultiSMSMessage *sms, gboolean eachsms, gboolean ems, const void *Info, GSM_StateMachine *sm)
#endif
{
	GSM_SiemensOTASMSInfo 	SiemensOTA;
	GSM_MultiPartSMSInfo	SMSInfo;
	gboolean			RetVal,udhinfo=TRUE;
	int			j,i;
	size_t Pos;
	GSM_MemoryEntry		pbk;
	GSM_Error error;

	/* GSM_DecodeMultiPartSMS returns if decoded SMS content correctly */
	RetVal = GSM_DecodeMultiPartSMS(GSM_GetGlobalDebug(), &SMSInfo,sms,ems);

	if (eachsms) {
		if (GSM_DecodeSiemensOTASMS(GSM_GetGlobalDebug(), &SiemensOTA,&sms->SMS[0])) udhinfo = FALSE;
		if (sms->SMS[0].UDH.Type != UDH_NoUDH && sms->SMS[0].UDH.AllParts == sms->Number) udhinfo = FALSE;
		if (RetVal && !udhinfo) {
			DisplaySingleSMSInfo(&(sms->SMS[0]),FALSE,FALSE,Info);
			printf("\n");
		} else {
			for (j=0;j<sms->Number;j++) {
				DisplaySingleSMSInfo(&(sms->SMS[j]),!RetVal,udhinfo,Info);
				printf("\n");
			}
		}
	} else {
		for (j=0;j<sms->Number;j++) {
			DisplaySingleSMSInfo(&(sms->SMS[j]),!RetVal,TRUE,Info);
			printf("\n");
		}
	}
	if (!RetVal) {
		GSM_FreeMultiPartSMSInfo(&SMSInfo);
		return;
	}

	if (SMSInfo.Unknown) printf("%s\n\n", _("Some details were ignored (unknown or not implemented in decoding functions)"));

	for (i=0;i<SMSInfo.EntriesNum;i++) {
		switch (SMSInfo.Entries[i].ID) {
		case SMS_SiemensFile:
			printf("%s", _("Siemens OTA file"));
			if (strstr(DecodeUnicodeString(SMSInfo.Entries[i].File->Name),".vcf")) {
				printf("%s\n", _(" - VCARD"));
				SMSInfo.Entries[i].File->Buffer = realloc(SMSInfo.Entries[i].File->Buffer,1+SMSInfo.Entries[i].File->Used);
				SMSInfo.Entries[i].File->Buffer[SMSInfo.Entries[i].File->Used] = 0;
				SMSInfo.Entries[i].File->Used += 1;
				Pos = 0;
				error = GSM_DecodeVCARD(GSM_GetGlobalDebug(), SMSInfo.Entries[i].File->Buffer, &Pos, &pbk, Nokia_VCard21);
				if (error == ERR_NONE) {
					PrintMemoryEntry(&pbk, sm);
				}
			} else {
				printf("\n");
			}
			break;
		case SMS_NokiaRingtone:
			printf(_("Ringtone \"%s\"\n"),DecodeUnicodeConsole(SMSInfo.Entries[i].Ringtone->Name));
			GSM_SaveRingtoneRttl(stdout,SMSInfo.Entries[i].Ringtone);
			printf("\n");
#if 0
			/* Disabled for now */
			if (answer_yes("%s", _("Do you want to play it?")))
				GSM_PlayRingtone(SMSInfo.Entries[i].Ringtone);
#endif
			break;
		case SMS_NokiaCallerLogo:
			printf("%s\n\n", _("Caller logo"));
			GSM_PrintBitmap(stdout,&SMSInfo.Entries[i].Bitmap->Bitmap[0]);
			break;
		case SMS_NokiaOperatorLogo:
			printf(_("Operator logo for %s network"),
				SMSInfo.Entries[i].Bitmap->Bitmap[0].NetworkCode);
			printf(" (%s",
				DecodeUnicodeConsole(GSM_GetNetworkName(SMSInfo.Entries[i].Bitmap->Bitmap[0].NetworkCode)));
			printf(", %s)",
				DecodeUnicodeConsole(GSM_GetCountryName(SMSInfo.Entries[i].Bitmap->Bitmap[0].NetworkCode)));
			printf("\n\n");
			GSM_PrintBitmap(stdout,&SMSInfo.Entries[i].Bitmap->Bitmap[0]);
			break;
		case SMS_NokiaScreenSaverLong:
			printf("%s\n", _("Screen saver"));
			GSM_PrintBitmap(stdout,&SMSInfo.Entries[i].Bitmap->Bitmap[0]);
			break;
		case SMS_NokiaPictureImageLong:
			printf("%s\n", _("Picture"));
			if (UnicodeLength(SMSInfo.Entries[i].Bitmap->Bitmap[0].Text) != 0)
				printf(LISTFORMAT "\"%s\"\n\n", _("Text"),DecodeUnicodeConsole(SMSInfo.Entries[i].Bitmap->Bitmap[0].Text));
			GSM_PrintBitmap(stdout,&SMSInfo.Entries[i].Bitmap->Bitmap[0]);
			break;
		case SMS_NokiaProfileLong:
			printf("%s\n", _("Profile"));
			GSM_PrintBitmap(stdout,&SMSInfo.Entries[i].Bitmap->Bitmap[0]);
			break;
		case SMS_ConcatenatedTextLong:
		case SMS_ConcatenatedAutoTextLong:
		case SMS_ConcatenatedTextLong16bit:
		case SMS_ConcatenatedAutoTextLong16bit:
		case SMS_NokiaVCARD21Long:
		case SMS_NokiaVCALENDAR10Long:
			if (SMSInfo.Entries[i].Buffer == NULL) printf("\n");
			else printf("%s\n",DecodeUnicodeConsole(SMSInfo.Entries[i].Buffer));
			break;
		case SMS_EMSFixedBitmap:
		case SMS_EMSVariableBitmap:
			GSM_PrintBitmap(stdout,&SMSInfo.Entries[i].Bitmap->Bitmap[0]);
			break;
		case SMS_EMSAnimation:
			/* Can't show animation, we show first frame */
			GSM_PrintBitmap(stdout,&SMSInfo.Entries[i].Bitmap->Bitmap[0]);
			break;
		case SMS_EMSPredefinedSound:
			printf("\n" LISTFORMAT "%i\n", _("EMS sound ID"),SMSInfo.Entries[i].Number);
			break;
		case SMS_EMSPredefinedAnimation:
			printf("\n" LISTFORMAT "%i\n", _("EMS animation ID"),SMSInfo.Entries[i].Number);
			break;
		case SMS_MMSIndicatorLong:
			printf(LISTFORMAT "%s\n", _("Sender"),SMSInfo.Entries[i].MMSIndicator->Sender);
			printf(LISTFORMAT "%s\n", _("Subject"),SMSInfo.Entries[i].MMSIndicator->Title);
			printf(LISTFORMAT "%s\n", _("Address"),SMSInfo.Entries[i].MMSIndicator->Address);
			printf(LISTFORMAT "%li\n", _("Message size"), (long)SMSInfo.Entries[i].MMSIndicator->MessageSize);
			break;
		case SMS_Text:
		case SMS_NokiaRingtoneLong:
		case SMS_NokiaOperatorLogoLong:
		case SMS_NokiaWAPBookmarkLong:
		case SMS_NokiaWAPSettingsLong:
		case SMS_NokiaMMSSettingsLong:
		case SMS_NokiaVCARD10Long:
		case SMS_NokiaVTODOLong:
		case SMS_VCARD10Long:
		case SMS_VCARD21Long:
		case SMS_DisableVoice:
		case SMS_DisableFax:
		case SMS_DisableEmail:
		case SMS_EnableVoice:
		case SMS_EnableFax:
		case SMS_EnableEmail:
		case SMS_VoidSMS:
		case SMS_EMSSound10:
		case SMS_EMSSound12:
		case SMS_EMSSonyEricssonSound:
		case SMS_EMSSound10Long:
		case SMS_EMSSound12Long:
		case SMS_EMSSonyEricssonSoundLong:
		case SMS_EMSVariableBitmapLong:
		case SMS_WAPIndicatorLong:
		case SMS_AlcatelMonoBitmapLong:
		case SMS_AlcatelMonoAnimationLong:
		case SMS_AlcatelSMSTemplateName:
#ifndef CHECK_CASES
		default:
#endif
			printf("%s\n", _("Error"));
			break;
		}
	}
	printf("\n");
	fflush(stdout);
	GSM_FreeMultiPartSMSInfo(&SMSInfo);
}


GSM_Error DisplaySMSFrame(GSM_SMSMessage *SMS, GSM_StateMachine *sm)
{
	GSM_Error 		error;
	int			i, length, current = 0;
	unsigned char		req[1000], buffer[1000], hexreq[1000];
        unsigned char           hexmsg[1000], hexudh[1000];

	error=PHONE_EncodeSMSFrame(sm,SMS,buffer,PHONE_SMSSubmit,&length,TRUE);
	if (error != ERR_NONE) {
		return error;
	}
        length = length - PHONE_SMSSubmit.Text;

        for(i=SMS->UDH.Length;i<length;i++) {
		req[i-SMS->UDH.Length]=buffer[PHONE_SMSSubmit.Text+i];
	}
        EncodeHexBin(hexmsg, req, MAX(0, length-SMS->UDH.Length));

        for(i=0;i<SMS->UDH.Length;i++) {
		req[i]=buffer[PHONE_SMSSubmit.Text+i];
	}
        EncodeHexBin(hexudh, req, SMS->UDH.Length);

        printf(LISTFORMAT "%s\n", _("Data PDU"), hexmsg);
        printf(LISTFORMAT "%d\n", _("Number of bits"),
                (buffer[PHONE_SMSSubmit.TPUDL]-SMS->UDH.Length)*8);
        printf(LISTFORMAT "%s\n", _("UDH"), hexudh);

	for (i=0;i<buffer[PHONE_SMSSubmit.SMSCNumber]+1;i++) {
		req[current++]=buffer[PHONE_SMSSubmit.SMSCNumber+i];
	}
	req[current++]=buffer[PHONE_SMSSubmit.firstbyte];
	req[current++]=buffer[PHONE_SMSSubmit.TPMR];
	for (i=0;i<((buffer[PHONE_SMSSubmit.Number]+1)/2+1)+1;i++) {
		req[current++]=buffer[PHONE_SMSSubmit.Number+i];
	}
	req[current++]=buffer[PHONE_SMSSubmit.TPPID];
	req[current++]=buffer[PHONE_SMSSubmit.TPDCS];
	req[current++]=buffer[PHONE_SMSSubmit.TPVP];
	req[current++]=buffer[PHONE_SMSSubmit.TPUDL];
	for(i=0;i<length;i++) req[current++]=buffer[PHONE_SMSSubmit.Text+i];
	EncodeHexBin(hexreq, req, current);
        printf(LISTFORMAT "%s\n", _("Whole PDU"), hexreq);
	printf("\n");
	fflush(stdout);
	return ERR_NONE;
}

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */

