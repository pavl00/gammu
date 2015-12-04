/* (c) 2002-2005 by Marcin Wiacek, Walek and Michal Cihar */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <gammu-config.h>
#include <gammu-inifile.h>
#include <gammu-backup.h>
#include <gammu-unicode.h>

#include "../../service/gsmcal.h"
#include "../../service/gsmlogo.h"
#include "../../service/gsmmisc.h"
#include "../../misc/coding/coding.h"
#include "../../misc/coding/md5.h"
#include "../../misc/misc.h"
#include "../../debug.h"
#include "backtext.h"

#include "../../../helper/string.h"

#ifdef GSM_ENABLE_BACKUP

/**
 * Helper define to check error code from fwrite.
 */
#define chk_fwrite(data, size, count, file) \
	if (fwrite(data, size, count, file) != count) goto fail;

GSM_Error FindBackupChecksum(const char *FileName, gboolean UseUnicode, char *checksum)
{
	INI_Section		*file_info, *h;
	INI_Entry		*e;
	char			*buffer = NULL,buff[100]={0};
	int			len=0;
	GSM_Error		error;

	error = INI_ReadFile(FileName, UseUnicode, &file_info);
	if (error != ERR_NONE) {
		return error;
	}

	if (UseUnicode) {
	        for (h = file_info; h != NULL; h = h->Next) {
			EncodeUnicode(buff,"Checksum",8);
			if (mywstrncasecmp(buff, h->SectionName, 8)) continue;

			buffer = (unsigned char *)realloc(buffer,len+UnicodeLength(h->SectionName)*2+2);
			CopyUnicodeString(buffer+len,h->SectionName);
			len+=UnicodeLength(h->SectionName)*2;

		        for (e = h->SubEntries; e != NULL; e = e->Next) {
				buffer = (unsigned char *)realloc(buffer,len+UnicodeLength(e->EntryName)*2+2);
				CopyUnicodeString(buffer+len,e->EntryName);
				len+=UnicodeLength(e->EntryName)*2;
				buffer = (unsigned char *)realloc(buffer,len+UnicodeLength(e->EntryValue)*2+2);
				CopyUnicodeString(buffer+len,e->EntryValue);
				len+=UnicodeLength(e->EntryValue)*2;
			}
		}
	} else {
	        for (h = file_info; h != NULL; h = h->Next) {
	            	if (strncasecmp("Checksum", h->SectionName, 8) == 0) continue;

			buffer = (unsigned char *)realloc(buffer,len+strlen(h->SectionName)+1);
			strcpy(buffer+len,h->SectionName);
			len+=strlen(h->SectionName);

		        for (e = h->SubEntries; e != NULL; e = e->Next) {
				buffer = (unsigned char *)realloc(buffer,len+strlen(e->EntryName)+1);
				strcpy(buffer+len,e->EntryName);
				len+=strlen(e->EntryName);
				buffer = (unsigned char *)realloc(buffer,len+strlen(e->EntryValue)+1);
				strcpy(buffer+len,e->EntryValue);
				len+=strlen(e->EntryValue);
			}
		}
	}

	CalculateMD5(buffer, len, checksum);
	free(buffer);
	buffer=NULL;
	INI_Free(file_info);
	return ERR_NONE;
}

static unsigned char *ReadCFGText(INI_Section *cfg, const unsigned char *section, const unsigned char *key, const gboolean Unicode)
{
	unsigned char unicode_key[500],*retval;

	if (Unicode) {
		EncodeUnicode(unicode_key,key,strlen(key));
		retval = INI_GetValue(cfg,section,unicode_key,Unicode);
		if (retval != NULL) return DecodeUnicodeString(retval);
		return NULL;
	} else {
		return INI_GetValue(cfg,section,key,Unicode);
	}
}

static GSM_Error SaveLinkedBackupText(FILE *file, const char *myname, const char *myvalue, const gboolean UseUnicode)
{
	int 		w,current;
	unsigned char 	buffer2[1000],buffer3[1000];

	current = strlen(myvalue); w = 0;
	while (TRUE) {
		if (current > 200) {
			memcpy(buffer2,myvalue+(strlen(myvalue)-current),200);
			buffer2[200] = 0;
			current = current - 200;
		} else {
			memcpy(buffer2,myvalue+(strlen(myvalue)-current),current);
			buffer2[current] = 0;
			current = 0;
		}
		if (UseUnicode) {
			sprintf(buffer3,"%s%02i = %s%c%c",myname,w,buffer2,13,10);
			EncodeUnicode(buffer2,buffer3,strlen(buffer3));
			chk_fwrite(buffer2,1,strlen(buffer3)*2,file);
		} else {
			fprintf(file,"%s%02i = %s%c%c",myname,w,buffer2,13,10);
		}
		if (current == 0) break;
		w++;
	}
	return ERR_NONE;
fail:
	return ERR_WRITING_FILE;
}

char *ReadLinkedBackupText(INI_Section *file_info, const char *section, const char *myname, const gboolean UseUnicode)
{
	char		buffer2[300];
	char			*readvalue;
	int			i;
	char *result = NULL;
	size_t len, cursize = 0, pos = 0;

	i=0;
	while (TRUE) {
		sprintf(buffer2, "%s%02i", myname, i);
		readvalue = ReadCFGText(file_info, section, buffer2, UseUnicode);
		if (readvalue == NULL) {
			break;
		}
		len = strlen(readvalue);
		if (pos + len + 1 >= cursize) {
			cursize += len + 100;
			result = (char *)realloc(result, cursize);
			if (result == NULL) return NULL;
		}

		strcpy(result + pos, readvalue);
		pos += len;

		i++;
	}
	return result;
}

static GSM_Error SaveBackupText(FILE *file, const char *myname, const char *myvalue, const gboolean UseUnicode)
{
	char buffer[10000]={0};
	unsigned char buffer2[10000]={0};

	if (myname[0] == 0x00) {
		if (UseUnicode) {
			EncodeUnicode(buffer,myvalue,strlen(myvalue));
			chk_fwrite(buffer,1,strlen(myvalue)*2,file);
		} else fprintf(file,"%s",myvalue);
	} else {
		if (UseUnicode) {
			sprintf(buffer, "%s = \"", myname);
			EncodeUnicode(buffer2, buffer, strlen(buffer));
			chk_fwrite(buffer2, 1, UnicodeLength(buffer2) * 2, file);

			EncodeUnicodeSpecialChars(buffer2, myvalue);
			chk_fwrite(buffer2, 1, UnicodeLength(buffer2) * 2, file);

			sprintf(buffer,"\"%c%c",13,10);
			EncodeUnicode(buffer2, buffer, strlen(buffer));
			chk_fwrite(buffer2, 1, UnicodeLength(buffer2) * 2, file);

		} else {
			EncodeSpecialChars(buffer, DecodeUnicodeString(myvalue));
			fprintf(file, "%s = \"%s\"%c%c", myname, buffer, 13, 10);
			EncodeHexBin(buffer, myvalue, UnicodeLength(myvalue) * 2);
			fprintf(file, "%sUnicode = %s%c%c", myname, buffer, 13, 10);
		}
	}
	return ERR_NONE;
fail:
	return ERR_WRITING_FILE;
}

static GSM_Error SaveBackupBase64(FILE *file, char *myname, unsigned char *data, size_t length, gboolean UseUnicode)
{
	char *buffer=NULL;
	unsigned char *unicode_buffer=NULL;
	GSM_Error error;

	/*
	 * Need to be big enough to store base64 (what is *4/3, but *2 is safer
	 * and we don't have to care about rounding and padding).
	 */
	buffer = (char *)malloc(length * 2);
	if (buffer == NULL) {
		return ERR_MOREMEMORY;
	}
	unicode_buffer = (unsigned char *)malloc(length * 4);
	if (unicode_buffer == NULL) {
		free(buffer);
		buffer=NULL;
		return ERR_MOREMEMORY;
	}

	EncodeBASE64(data, buffer, length);

	error = SaveLinkedBackupText(file, myname, buffer, UseUnicode);

	free(buffer);
	buffer=NULL;
	free(unicode_buffer);
	unicode_buffer=NULL;
	return error;
}

#define ReadBackupText(file_info, section, myname, myvalue, UseUnicode) ReadBackupTextLen(file_info, section, myname, myvalue, sizeof(myvalue), UseUnicode)

static gboolean ReadBackupTextLen(INI_Section *file_info, const char *section, const char *myname, char *myvalue, const size_t maxlen, const gboolean UseUnicode)
{
	unsigned char paramname[10000],*readvalue, decodedvalue[10000];
	gboolean ret = TRUE;

	if (UseUnicode) {
		EncodeUnicode(paramname,myname,strlen(myname));
		readvalue = INI_GetValue(file_info, section, paramname, UseUnicode);
		if (readvalue!=NULL) {
			DecodeUnicodeSpecialChars(decodedvalue, readvalue+2);
			if ((UnicodeLength(decodedvalue) + 1) * 2 >= maxlen) {
				decodedvalue[maxlen - 1] = 0;
				decodedvalue[maxlen - 2] = 0;
				dbgprintf(NULL, "String too long!\n");
				ret = FALSE;
			}
			CopyUnicodeString(myvalue, decodedvalue);
			myvalue[UnicodeLength(myvalue)*2-2]=0;
			myvalue[UnicodeLength(myvalue)*2-1]=0;

			dbgprintf(NULL, "Cfg read: %s\n",DecodeUnicodeString(readvalue));
		} else {
			myvalue[0]=0;
			myvalue[1]=0;
			ret = FALSE;
		}
	} else {
		strcpy(paramname,myname);
		strcat(paramname,"Unicode");
		readvalue = ReadCFGText(file_info, section, paramname, UseUnicode);
		if (readvalue!=NULL) {
			dbgprintf(NULL, "Cfg read: %s %ld\n",readvalue,(long)strlen(readvalue));
			if (strlen(readvalue) >= maxlen - 1) {
				ret = FALSE;
				dbgprintf(NULL, "String too long!\n");
			}
			DecodeHexBin (myvalue, readvalue, MIN(strlen(readvalue), maxlen - 1));
			myvalue[strlen(readvalue)/2]=0;
			myvalue[strlen(readvalue)/2+1]=0;
			dbgprintf(NULL, "Cfg decoded: %s\n",DecodeUnicodeString(myvalue));
		} else {
			strcpy(paramname,myname);
			readvalue = ReadCFGText(file_info, section, paramname, UseUnicode);
			if (readvalue!=NULL) {
				DecodeSpecialChars(decodedvalue, readvalue + 1);
				EncodeUnicode(myvalue, decodedvalue, strlen(decodedvalue) - 1);
			} else {
				myvalue[0]=0;
				myvalue[1]=0;
				ret = FALSE;
			}
		}
	}
	return ret;
}

static GSM_Error SaveVCalDateTime(FILE *file, GSM_DateTime *dt, gboolean UseUnicode)
{
	unsigned char 	buffer[100];
	size_t		Length = 3;
	GSM_Error error;

	sprintf(buffer, " = ");
	error = VC_StoreDateTime(buffer, sizeof(buffer), &Length, dt, NULL);
	if (error != ERR_NONE) return error;
	return SaveBackupText(file, "", buffer, UseUnicode);
}

static GSM_Error SaveVCalDate(FILE *file, GSM_DateTime *dt, gboolean UseUnicode)
{
	unsigned char buffer[100];

	sprintf(buffer, " = %04d%02d%02d%c%c", dt->Year, dt->Month, dt->Day,13,10);
	return SaveBackupText(file, "", buffer, UseUnicode);
}

/* ---------------------- backup files ------------------------------------- */

static GSM_Error SavePbkEntry(FILE *file, GSM_MemoryEntry *Pbk, gboolean UseUnicode)
{
	gboolean	text;
	char	buffer[1000]={0};
	int	j, i;
	GSM_Error error;

	sprintf(buffer,"Location = %03i%c%c",Pbk->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	for (j=0;j<Pbk->EntriesNum;j++) {
		text = TRUE;
		switch (Pbk->Entries[j].Location) {
			case PBK_Location_Home:
				sprintf(buffer,"Entry%02iLocation = Home%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Location_Work:
				sprintf(buffer,"Entry%02iLocation = Work%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Location_Unknown:
				break;
		}
		switch (Pbk->Entries[j].EntryType) {
			case PBK_Number_General:
				sprintf(buffer,"Entry%02iType = NumberGeneral%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Number_Video:
				sprintf(buffer,"Entry%02iType = NumberVideo%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Number_Mobile:
				sprintf(buffer,"Entry%02iType = NumberMobile%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Number_Fax:
				sprintf(buffer,"Entry%02iType = NumberFax%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Number_Pager:
				sprintf(buffer,"Entry%02iType = NumberPager%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Number_Other:
				sprintf(buffer,"Entry%02iType = NumberOther%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Number_Messaging:
				sprintf(buffer,"Entry%02iType = NumberMessaging%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Note:
				sprintf(buffer,"Entry%02iType = Note%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Postal:
				sprintf(buffer,"Entry%02iType = Postal%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Email:
				sprintf(buffer,"Entry%02iType = Email%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Email2:
				sprintf(buffer,"Entry%02iType = Email2%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_URL:
				sprintf(buffer,"Entry%02iType = URL%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Name:
				sprintf(buffer,"Entry%02iType = Name%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Caller_Group:
				sprintf(buffer,"Entry%02iType = CallerGroup%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"Entry%02iNumber = %i%c%c",j,Pbk->Entries[j].Number,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				text = FALSE;
				break;
			case PBK_RingtoneID:
				sprintf(buffer,"Entry%02iType = RingtoneID%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"Entry%02iNumber = %i%c%c",j,Pbk->Entries[j].Number,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				text = FALSE;
				break;
			case PBK_PictureID:
				sprintf(buffer,"Entry%02iType = PictureID%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"Entry%02iNumber = %i%c%c",j,Pbk->Entries[j].Number,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				text = FALSE;
				break;
			case PBK_Text_PictureName:
				sprintf(buffer,"Entry%02iType = PictureName%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_UserID:
				sprintf(buffer,"Entry%02iType = UserID%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Category:
				sprintf(buffer,"Entry%02iType = Category%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				if (Pbk->Entries[j].Number != -1) {
					sprintf(buffer,"Entry%02iNumber = %i%c%c",j,Pbk->Entries[j].Number,13,10);
					error = SaveBackupText(file, "", buffer, UseUnicode);
					if (error != ERR_NONE) return error;
					text = FALSE;
				}
				break;
			case PBK_Private:
				sprintf(buffer,"Entry%02iType = Private%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"Entry%02iNumber = %i%c%c",j,Pbk->Entries[j].Number,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				text = FALSE;
				break;
			case PBK_Text_LastName:
				sprintf(buffer,"Entry%02iType = LastName%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_FirstName:
				sprintf(buffer,"Entry%02iType = FirstName%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_SecondName:
				sprintf(buffer,"Entry%02iType = SecondName%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_NickName:
				sprintf(buffer,"Entry%02iType = NickName%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_FormalName:
				sprintf(buffer,"Entry%02iType = FormalName%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_NamePrefix:
				sprintf(buffer,"Entry%02iType = NamePrefix%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_NameSuffix:
				sprintf(buffer,"Entry%02iType = NameSuffix%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Company:
				sprintf(buffer,"Entry%02iType = Company%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_JobTitle:
				sprintf(buffer,"Entry%02iType = JobTitle%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_StreetAddress:
				sprintf(buffer,"Entry%02iType = Address%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_City:
				sprintf(buffer,"Entry%02iType = City%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_State:
				sprintf(buffer,"Entry%02iType = State%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Zip:
				sprintf(buffer,"Entry%02iType = Zip%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Country:
				sprintf(buffer,"Entry%02iType = Country%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Custom1:
				sprintf(buffer,"Entry%02iType = Custom1%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Custom2:
				sprintf(buffer,"Entry%02iType = Custom2%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Custom3:
				sprintf(buffer,"Entry%02iType = Custom3%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_Custom4:
				sprintf(buffer,"Entry%02iType = Custom4%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_LUID:
				sprintf(buffer,"Entry%02iType = LUID%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_VOIP:
				sprintf(buffer,"Entry%02iType = VOIP%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_WVID:
				sprintf(buffer,"Entry%02iType = WVID%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_SWIS:
				sprintf(buffer,"Entry%02iType = SWIS%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_SIP:
				sprintf(buffer,"Entry%02iType = SIP%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Text_DTMF:
				sprintf(buffer,"Entry%02iType = DTMF%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Date:
				sprintf(buffer,"Entry%02iType = Date%c%cEntry%02iText",j,13,10, j);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				error = SaveVCalDate(file, &Pbk->Entries[j].Date, UseUnicode);
				if (error != ERR_NONE) return error;
				text = FALSE;
				break;
			case PBK_LastModified:
				sprintf(buffer,"Entry%02iType = LastModified%c%cEntry%02iText",j,13,10, j);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				error = SaveVCalDateTime(file, &Pbk->Entries[j].Date, UseUnicode);
				if (error != ERR_NONE) return error;
				text = FALSE;
				break;
			case PBK_CallLength:
				text = FALSE;
				break;
			case PBK_PushToTalkID:
				sprintf(buffer,"Entry%02iType = PushToTalkID%c%c",j,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case PBK_Photo:
				switch (Pbk->Entries[j].Picture.Type) {
					case PICTURE_BMP:
						sprintf(buffer,"Entry%02iType = BMPPhoto%c%c",j,13,10);
						break;
					case PICTURE_GIF:
						sprintf(buffer,"Entry%02iType = GIFPhoto%c%c",j,13,10);
						break;
					case PICTURE_JPG:
						sprintf(buffer,"Entry%02iType = JPEGPhoto%c%c",j,13,10);
						break;
					case PICTURE_ICN:
						sprintf(buffer,"Entry%02iType = ICOPhoto%c%c",j,13,10);
						break;
					case PICTURE_PNG:
						sprintf(buffer,"Entry%02iType = PNGPhoto%c%c",j,13,10);
						break;
					default:
						dbgprintf(NULL, "Unknown picture format: %d\n", Pbk->Entries[j].Picture.Type);
						sprintf(buffer,"Entry%02iType = Photo%c%c",j,13,10);
						break;
				}
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer, "Entry%02iData", j);
				error = SaveBackupBase64(file, buffer, Pbk->Entries[j].Picture.Buffer, Pbk->Entries[j].Picture.Length, UseUnicode);
				if (error != ERR_NONE) return error;
				text = FALSE;
				break;
        	}
		if (text) {
			sprintf(buffer,"Entry%02iText",j);
			error = SaveBackupText(file,buffer,Pbk->Entries[j].Text, UseUnicode);
			if (error != ERR_NONE) return error;
		}
		switch (Pbk->Entries[j].EntryType) {
			case PBK_Number_General:
			case PBK_Number_Video:
			case PBK_Number_Mobile:
			case PBK_Number_Fax:
			case PBK_Number_Other:
			case PBK_Number_Pager:
				if (Pbk->Entries[j].VoiceTag!=0) {
					sprintf(buffer,"Entry%02iVoiceTag = %i%c%c",j,Pbk->Entries[j].VoiceTag,13,10);
					error = SaveBackupText(file, "", buffer, UseUnicode);
					if (error != ERR_NONE) return error;
				}
				i = 0;
				while (Pbk->Entries[j].SMSList[i]!=0) {
					sprintf(buffer,"Entry%02iSMSList%02i = %i%c%c",j,i,Pbk->Entries[j].SMSList[i],13,10);
					error = SaveBackupText(file, "", buffer, UseUnicode);
					if (error != ERR_NONE) return error;
					i++;
				}
				break;
			default:
				break;
		}
	}
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveNoteEntry(FILE *file, GSM_NoteEntry *Note, gboolean UseUnicode)
{
	char buffer[1000]={0};
	GSM_Error error;

	sprintf(buffer,"Location = %d%c%c", Note->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveBackupText(file, "Text", Note->Text, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer, "%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveCalendarType(FILE *file, GSM_CalendarNoteType Type, gboolean UseUnicode)
{
	char	buffer[1000]={0};
	GSM_Error error;

	error = SaveBackupText(file, "", "Type = ", UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"0%c%c",13,10);
	switch (Type) {
		case GSM_CAL_REMINDER 	: sprintf(buffer,"Reminder%c%c", 		13,10); break;
		case GSM_CAL_CALL     	: sprintf(buffer,"Call%c%c", 			13,10); break;
		case GSM_CAL_MEETING  	: sprintf(buffer,"Meeting%c%c", 		13,10); break;
		case GSM_CAL_BIRTHDAY 	: sprintf(buffer,"Birthday%c%c", 		13,10); break;
		case GSM_CAL_TRAVEL  	: sprintf(buffer,"Travel%c%c", 			13,10); break;
		case GSM_CAL_VACATION 	: sprintf(buffer,"Vacation%c%c", 		13,10); break;
		case GSM_CAL_MEMO	: sprintf(buffer,"Memo%c%c", 			13,10); break;
		case GSM_CAL_SHOPPING	: sprintf(buffer,"Shopping%c%c",		13,10); break;
		case GSM_CAL_ALARM    	: sprintf(buffer,"Alarm%c%c", 			13,10); break;
		case GSM_CAL_DAILY_ALARM: sprintf(buffer,"DailyAlarm%c%c", 		13,10); break;
		case GSM_CAL_T_ATHL   	: sprintf(buffer,"Training/Athletism%c%c", 	13,10); break;
       		case GSM_CAL_T_BALL   	: sprintf(buffer,"Training/BallGames%c%c", 	13,10); break;
                case GSM_CAL_T_CYCL   	: sprintf(buffer,"Training/Cycling%c%c", 	13,10); break;
                case GSM_CAL_T_BUDO   	: sprintf(buffer,"Training/Budo%c%c", 		13,10); break;
                case GSM_CAL_T_DANC   	: sprintf(buffer,"Training/Dance%c%c", 		13,10); break;
                case GSM_CAL_T_EXTR   	: sprintf(buffer,"Training/ExtremeSports%c%c", 	13,10); break;
                case GSM_CAL_T_FOOT   	: sprintf(buffer,"Training/Football%c%c", 	13,10); break;
                case GSM_CAL_T_GOLF   	: sprintf(buffer,"Training/Golf%c%c", 		13,10); break;
                case GSM_CAL_T_GYM    	: sprintf(buffer,"Training/Gym%c%c", 		13,10); break;
                case GSM_CAL_T_HORS   	: sprintf(buffer,"Training/HorseRaces%c%c", 	13,10); break;
                case GSM_CAL_T_HOCK   	: sprintf(buffer,"Training/Hockey%c%c", 	13,10); break;
                case GSM_CAL_T_RACE   	: sprintf(buffer,"Training/Races%c%c", 		13,10); break;
                case GSM_CAL_T_RUGB   	: sprintf(buffer,"Training/Rugby%c%c", 		13,10); break;
                case GSM_CAL_T_SAIL   	: sprintf(buffer,"Training/Sailing%c%c", 	13,10); break;
                case GSM_CAL_T_STRE   	: sprintf(buffer,"Training/StreetGames%c%c",	13,10); break;
                case GSM_CAL_T_SWIM   	: sprintf(buffer,"Training/Swimming%c%c", 	13,10); break;
                case GSM_CAL_T_TENN   	: sprintf(buffer,"Training/Tennis%c%c", 	13,10); break;
                case GSM_CAL_T_TRAV   	: sprintf(buffer,"Training/Travels%c%c", 	13,10); break;
                case GSM_CAL_T_WINT   	: sprintf(buffer,"Training/WinterGames%c%c", 	13,10); break;
	}
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveCalendarEntry(FILE *file, GSM_CalendarEntry *Note, gboolean UseUnicode)
{
	int 	i;
	char	buffer[1000]={0};
	GSM_Error error;

	sprintf(buffer,"Location = %d%c%c", Note->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveCalendarType(file, Note->Type, UseUnicode);
	if (error != ERR_NONE) return error;
	for (i=0;i<Note->EntriesNum;i++) {
		switch (Note->Entries[i].EntryType) {
		case CAL_START_DATETIME:
			error = SaveBackupText(file, "", "StartTime", UseUnicode);
			if (error != ERR_NONE) return error;
			error = SaveVCalDateTime(file, &Note->Entries[i].Date, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_END_DATETIME:
			error = SaveBackupText(file, "", "StopTime", UseUnicode);
			if (error != ERR_NONE) return error;
			error = SaveVCalDateTime(file, &Note->Entries[i].Date, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_TONE_ALARM_DATETIME:
			error = SaveBackupText(file, "", "ToneAlarm", UseUnicode);
			if (error != ERR_NONE) return error;
			error = SaveVCalDateTime(file, &Note->Entries[i].Date, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_SILENT_ALARM_DATETIME:
			error = SaveBackupText(file, "", "SilentAlarm", UseUnicode);
			if (error != ERR_NONE) return error;
			error = SaveVCalDateTime(file, &Note->Entries[i].Date, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_LAST_MODIFIED:
			error = SaveBackupText(file, "", "LastModified", UseUnicode);
			if (error != ERR_NONE) return error;
			error = SaveVCalDateTime(file, &Note->Entries[i].Date, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_PRIVATE:
			sprintf(buffer, "Private = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_LOCATION:
			error = SaveBackupText(file, "EventLocation", Note->Entries[i].Text, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_CONTACTID:
			sprintf(buffer, "ContactID = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_TEXT:
			error = SaveBackupText(file, "Text", Note->Entries[i].Text, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_DESCRIPTION:
			error = SaveBackupText(file, "Description", Note->Entries[i].Text, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_LUID:
			error = SaveBackupText(file, "LUID", Note->Entries[i].Text, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_PHONE:
			error = SaveBackupText(file, "Phone", Note->Entries[i].Text, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_STOPDATE:
			error = SaveBackupText(file, "", "RepeatStopDate", UseUnicode);
			if (error != ERR_NONE) return error;
			error = SaveVCalDate(file, &Note->Entries[i].Date, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_STARTDATE:
			error = SaveBackupText(file, "", "RepeatStartDate", UseUnicode);
			if (error != ERR_NONE) return error;
			error = SaveVCalDate(file, &Note->Entries[i].Date, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_DAYOFWEEK:
			sprintf(buffer, "RepeatDayOfWeek = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_DAY:
			sprintf(buffer, "RepeatDay = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_WEEKOFMONTH:
			sprintf(buffer, "RepeatWeekOfMonth = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_MONTH:
			sprintf(buffer, "RepeatMonth = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_FREQUENCY:
			sprintf(buffer, "RepeatFrequency = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_COUNT:
			sprintf(buffer, "RepeatCount = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case CAL_REPEAT_DAYOFYEAR:
			sprintf(buffer, "RepeatDayOfYear = %d%c%c",Note->Entries[i].Number,13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		}
	}
	sprintf(buffer, "%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveWAPSettingsEntry(FILE *file, GSM_MultiWAPSettings *settings, gboolean UseUnicode)
{
	int 	i;
	char 	buffer[10000]={0};
	GSM_Error error;

	if (settings->Active) {
		sprintf(buffer,"Active = Yes%c%c",13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	switch (settings->ActiveBearer) {
		case WAPSETTINGS_BEARER_SMS : sprintf(buffer,"Bearer = SMS%c%c",13,10);  break;
		case WAPSETTINGS_BEARER_GPRS: sprintf(buffer,"Bearer = GPRS%c%c",13,10); break;
		case WAPSETTINGS_BEARER_DATA: sprintf(buffer,"Bearer = Data%c%c",13,10); break;
		case WAPSETTINGS_BEARER_USSD: sprintf(buffer,"Bearer = USSD%c%c",13,10);
	}
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	if (settings->ReadOnly) {
		sprintf(buffer,"ReadOnly = Yes%c%c",13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	sprintf(buffer,"Proxy");
	error = SaveBackupText(file, buffer, settings->Proxy, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"ProxyPort = %i%c%c",settings->ProxyPort,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"Proxy2");
	error = SaveBackupText(file, buffer, settings->Proxy2, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"Proxy2Port = %i%c%c",settings->Proxy2Port,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	for (i=0;i<settings->Number;i++) {
		sprintf(buffer,"Title%02i",i);
		error = SaveBackupText(file, buffer, settings->Settings[i].Title, UseUnicode);
		if (error != ERR_NONE) return error;
		sprintf(buffer,"HomePage%02i",i);
		error = SaveBackupText(file, buffer, settings->Settings[i].HomePage, UseUnicode);
		if (error != ERR_NONE) return error;
		if (settings->Settings[i].IsContinuous) {
			sprintf(buffer,"Type%02i = Continuous%c%c",i,13,10);
		} else {
			sprintf(buffer,"Type%02i = Temporary%c%c",i,13,10);
		}
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
		if (settings->Settings[i].IsSecurity) {
			sprintf(buffer,"Security%02i = On%c%c",i,13,10);
		} else {
			sprintf(buffer,"Security%02i = Off%c%c",i,13,10);
		}
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
		switch (settings->Settings[i].Bearer) {
			case WAPSETTINGS_BEARER_SMS:
				sprintf(buffer,"Bearer%02i = SMS%c%c",i,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"Server%02i",i);
				error = SaveBackupText(file, buffer, settings->Settings[i].Server, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"Service%02i",i);
				error = SaveBackupText(file, buffer, settings->Settings[i].Service, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case WAPSETTINGS_BEARER_GPRS:
				sprintf(buffer,"Bearer%02i = GPRS%c%c",i,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"IP%02i",i);
				error = SaveBackupText(file, buffer, settings->Settings[i].IPAddress, UseUnicode);
				if (error != ERR_NONE) return error;
			case WAPSETTINGS_BEARER_DATA:
				if (settings->Settings[i].Bearer == WAPSETTINGS_BEARER_DATA) {
					sprintf(buffer,"Bearer%02i = Data%c%c",i,13,10);
					error = SaveBackupText(file, "", buffer, UseUnicode);
					if (error != ERR_NONE) return error;
					if (settings->Settings[i].IsISDNCall) {
						sprintf(buffer,"CallType%02i = ISDN%c%c",i,13,10);
					} else {
						sprintf(buffer,"CallType%02i = Analogue%c%c",i,13,10);
					}
					error = SaveBackupText(file, "", buffer, UseUnicode);
					if (error != ERR_NONE) return error;
					sprintf(buffer,"IP%02i",i);
					error = SaveBackupText(file, buffer, settings->Settings[i].IPAddress, UseUnicode);
					if (error != ERR_NONE) return error;
				}
				sprintf(buffer,"Number%02i",i);
				error = SaveBackupText(file, buffer, settings->Settings[i].DialUp, UseUnicode);
				if (error != ERR_NONE) return error;
				if (settings->Settings[i].ManualLogin) {
					sprintf(buffer,"Login%02i = Manual%c%c",i,13,10);
				} else {
					sprintf(buffer,"Login%02i = Automatic%c%c",i,13,10);
				}
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				if (settings->Settings[i].IsNormalAuthentication) {
					sprintf(buffer,"Authentication%02i = Normal%c%c",i,13,10);
				} else {
					sprintf(buffer,"Authentication%02i = Secure%c%c",i,13,10);
				}
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				switch (settings->Settings[i].Speed) {
					case WAPSETTINGS_SPEED_9600 : sprintf(buffer,"CallSpeed%02i = 9600%c%c" ,i,13,10); break;
					case WAPSETTINGS_SPEED_14400: sprintf(buffer,"CallSpeed%02i = 14400%c%c",i,13,10); break;
					case WAPSETTINGS_SPEED_AUTO : sprintf(buffer,"CallSpeed%02i = auto%c%c" ,i,13,10); break;
				}
				switch (settings->Settings[i].Speed) {
					case WAPSETTINGS_SPEED_9600 :
					case WAPSETTINGS_SPEED_14400:
					case WAPSETTINGS_SPEED_AUTO :
						error = SaveBackupText(file, "", buffer, UseUnicode);
						if (error != ERR_NONE) return error;
					default:
						break;
				}
				sprintf(buffer,"User%02i",i);
				error = SaveBackupText(file, buffer, settings->Settings[i].User, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"Password%02i",i);
				error = SaveBackupText(file, buffer, settings->Settings[i].Password, UseUnicode);
				if (error != ERR_NONE) return error;
				break;
			case WAPSETTINGS_BEARER_USSD:
				sprintf(buffer,"Bearer%02i = USSD%c%c",i,13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
				sprintf(buffer,"ServiceCode%02i",i);
				error = SaveBackupText(file, buffer, settings->Settings[i].Code, UseUnicode);
				if (error != ERR_NONE) return error;
				if (settings->Settings[i].IsIP) {
					sprintf(buffer,"IP%02i",i);
				} else {
					sprintf(buffer,"Number%02i",i);
				}
				error = SaveBackupText(file, buffer, settings->Settings[i].Service, UseUnicode);
				if (error != ERR_NONE) return error;
		}
		sprintf(buffer,"%c%c",13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	return ERR_NONE;
}

static GSM_Error SaveChatSettingsEntry(FILE *file, GSM_ChatSettings *settings, gboolean UseUnicode)
{
	char buffer[10000]={0};
	GSM_Error error;

	sprintf(buffer,"HomePage");
	error = SaveBackupText(file, buffer, settings->HomePage, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"User");
	error = SaveBackupText(file, buffer, settings->User, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"Password");
	error = SaveBackupText(file, buffer, settings->Password, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveWAPSettingsEntry(file, &settings->Connection, UseUnicode);
	if (error != ERR_NONE) return error;
	return ERR_NONE;
}

static GSM_Error SaveSyncMLSettingsEntry(FILE *file, GSM_SyncMLSettings *settings, gboolean UseUnicode)
{
	char buffer[10000]={0};
	GSM_Error error;

	sprintf(buffer,"User");
	error = SaveBackupText(file, buffer, settings->User, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"Password");
	error = SaveBackupText(file, buffer, settings->Password, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"PhonebookDB");
	error = SaveBackupText(file, buffer, settings->PhonebookDataBase, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"CalendarDB");
	error = SaveBackupText(file, buffer, settings->CalendarDataBase, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"Server");
	error = SaveBackupText(file, buffer, settings->Server, UseUnicode);
	if (error != ERR_NONE) return error;
	if (settings->SyncPhonebook) {
		sprintf(buffer,"SyncPhonebook = True%c%c",13,10);
	} else {
		sprintf(buffer,"SyncPhonebook = False%c%c",13,10);
	}
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	if (settings->SyncCalendar) {
		sprintf(buffer,"SyncCalendar = True%c%c",13,10);
	} else {
		sprintf(buffer,"SyncCalendar = False%c%c",13,10);
	}
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveWAPSettingsEntry(file, &settings->Connection, UseUnicode);
	if (error != ERR_NONE) return error;
	return ERR_NONE;
}

static GSM_Error SaveBitmapEntry(FILE *file, GSM_Bitmap *bitmap, gboolean UseUnicode)
{
	unsigned char 	buffer[10000]={0},buffer2[10000]={0};
	size_t		x,y;
	GSM_Error error;

	sprintf(buffer,"Width = %ld%c%c", (long)bitmap->BitmapWidth,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"Height = %ld%c%c", (long)bitmap->BitmapHeight,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	for (y=0;y<bitmap->BitmapHeight;y++) {
		for (x=0;x<bitmap->BitmapWidth;x++) {
			buffer[x] = ' ';
			if (GSM_IsPointBitmap(bitmap,x,y)) buffer[x]='#';
		}
		buffer[bitmap->BitmapWidth] = 0;
		sprintf(buffer2,"Bitmap%02i = \"%s\"%c%c",(int)y,buffer,13,10);
		error = SaveBackupText(file, "", buffer2, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	return ERR_NONE;
}

static GSM_Error SaveCallerEntry(FILE *file, GSM_Bitmap *bitmap, gboolean UseUnicode)
{
	unsigned char buffer[1000]={0};
	GSM_Error error;

	sprintf(buffer,"Location = %03i%c%c",bitmap->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	if (!bitmap->DefaultName) {
		error = SaveBackupText(file, "Name", bitmap->Text, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	if (!bitmap->DefaultRingtone) 	{
		if (bitmap->FileSystemRingtone) {
			sprintf(buffer,"FileRingtone = %02x%c%c",bitmap->RingtoneID,13,10);
		} else {
			sprintf(buffer,"Ringtone = %02x%c%c",bitmap->RingtoneID,13,10);
		}
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	if (bitmap->BitmapEnabled) {
		sprintf(buffer,"Enabled = True%c%c",13,10);
	} else {
		sprintf(buffer,"Enabled = False%c%c",13,10);
	}
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	if (!bitmap->DefaultBitmap) {
		error = SaveBitmapEntry(file, bitmap, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveWAPBookmarkEntry(FILE *file, GSM_WAPBookmark *bookmark, gboolean UseUnicode)
{
	unsigned char buffer[1000]={0};
	GSM_Error error;

	error = SaveBackupText(file, "URL", bookmark->Address, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveBackupText(file, "Title", bookmark->Title, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveStartupEntry(FILE *file, GSM_Bitmap *bitmap, gboolean UseUnicode)
{
	unsigned char buffer[1000]={0};
	GSM_Error error;

	sprintf(buffer,"[Startup]%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	if (bitmap->Type == GSM_WelcomeNote_Text) {
		error = SaveBackupText(file, "Text", bitmap->Text, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	if (bitmap->Type == GSM_StartupLogo) {
		error = SaveBitmapEntry(file, bitmap, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveSMSCEntry(FILE *file, GSM_SMSC *SMSC, gboolean UseUnicode)
{
	unsigned char buffer[1000]={0};
	GSM_Error error;

	sprintf(buffer,"Location = %03i%c%c",SMSC->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveBackupText(file, "Name", SMSC->Name, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveBackupText(file, "Number", SMSC->Number, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveBackupText(file, "DefaultNumber", SMSC->DefaultNumber, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveBackupText(file, "", "Format = ", UseUnicode);
	if (error != ERR_NONE) return error;
	switch (SMSC->Format) {
		case SMS_FORMAT_Text	: sprintf(buffer,"Text");  break;
		case SMS_FORMAT_Fax	: sprintf(buffer,"Fax");   break;
		case SMS_FORMAT_Email	: sprintf(buffer,"Email"); break;
		case SMS_FORMAT_Pager	: sprintf(buffer,"Pager"); break;
	}
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"%c%cValidity = ",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	switch (SMSC->Validity.Relative) {
		case SMS_VALID_1_Hour	: sprintf(buffer, "1hour"	); break;
		case SMS_VALID_6_Hours 	: sprintf(buffer, "6hours"	); break;
		case SMS_VALID_1_Day	: sprintf(buffer, "24hours"	); break;
		case SMS_VALID_3_Days	: sprintf(buffer, "72hours"	); break;
		case SMS_VALID_1_Week  	: sprintf(buffer, "1week"	); break;
		case SMS_VALID_Max_Time	:
		default			: sprintf(buffer,"MaximumTime"	); break;
	}
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"%c%c%c%c",13,10,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveRingtoneEntry(FILE *file, GSM_Ringtone *ringtone, gboolean UseUnicode)
{
	unsigned char *buffer=NULL;
	GSM_Error error;

	buffer = (unsigned char *)malloc(MAX(32, 2 * ringtone->NokiaBinary.Length) + 1);
	if (buffer == NULL)
		return ERR_MOREMEMORY;

	sprintf(buffer,"Location = %i%c%c",ringtone->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) {
		free(buffer);
		buffer=NULL;
		return error;
	}
	error = SaveBackupText(file, "Name", ringtone->Name, UseUnicode);
	if (error != ERR_NONE) {
		free(buffer);
		buffer=NULL;
		return error;
	}
	switch (ringtone->Format) {
	case RING_NOKIABINARY:
		EncodeHexBin(buffer,ringtone->NokiaBinary.Frame,ringtone->NokiaBinary.Length);
		SaveLinkedBackupText(file, "NokiaBinary", buffer, UseUnicode);
		break;
	case RING_MIDI:
		EncodeHexBin(buffer,ringtone->NokiaBinary.Frame,ringtone->NokiaBinary.Length);
		SaveLinkedBackupText(file, "Pure Midi", buffer, UseUnicode);
		break;
	case RING_MMF:
		EncodeHexBin(buffer,ringtone->NokiaBinary.Frame,ringtone->NokiaBinary.Length);
		SaveLinkedBackupText(file, "SMAF", buffer, UseUnicode);
		break;
	case RING_NOTETONE:
		break;
	}
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	free(buffer);
	buffer=NULL;

	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveOperatorEntry(FILE *file, GSM_Bitmap *bitmap, gboolean UseUnicode)
{
	unsigned char buffer[1000]={0};
	GSM_Error error;

	sprintf(buffer,"[Operator]%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"Network = \"%s\"%c%c", bitmap->NetworkCode,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveBitmapEntry(file, bitmap, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveToDoEntry(FILE *file, GSM_ToDoEntry *ToDo, gboolean UseUnicode)
{
	unsigned char 	buffer[1000]={0};
    	int 		j;
	GSM_Error error;

	sprintf(buffer,"Location = %i%c%c",ToDo->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveCalendarType(file, ToDo->Type, UseUnicode);
	if (error != ERR_NONE) return error;
	switch (ToDo->Priority) {
	case GSM_Priority_High:
		sprintf(buffer,"Priority = High%c%c",13,10);
		break;
	case GSM_Priority_Medium:
		sprintf(buffer,"Priority = Medium%c%c",13,10);
		break;
	case GSM_Priority_Low:
		sprintf(buffer,"Priority = Low%c%c",13,10);
		break;
	default:
		break;
	}
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	for (j=0;j<ToDo->EntriesNum;j++) {
        switch (ToDo->Entries[j].EntryType) {
	    case TODO_END_DATETIME:
		error = SaveBackupText(file, "", "DueTime", UseUnicode);
		if (error != ERR_NONE) return error;
                error = SaveVCalDateTime(file, &ToDo->Entries[j].Date, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
	    case TODO_START_DATETIME:
		error = SaveBackupText(file, "", "StartTime", UseUnicode);
		if (error != ERR_NONE) return error;
                error = SaveVCalDateTime(file, &ToDo->Entries[j].Date, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
	    case TODO_COMPLETED_DATETIME:
		error = SaveBackupText(file, "", "CompletedTime", UseUnicode);
		if (error != ERR_NONE) return error;
                error = SaveVCalDateTime(file, &ToDo->Entries[j].Date, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_COMPLETED:
	        sprintf(buffer,"Completed = %s%c%c",ToDo->Entries[j].Number == 1 ? "yes" : "no" ,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_ALARM_DATETIME:
		error = SaveBackupText(file, "", "Alarm", UseUnicode);
		if (error != ERR_NONE) return error;
                error = SaveVCalDateTime(file, &ToDo->Entries[j].Date, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_SILENT_ALARM_DATETIME:
		error = SaveBackupText(file, "", "SilentAlarm", UseUnicode);
		if (error != ERR_NONE) return error;
                error = SaveVCalDateTime(file, &ToDo->Entries[j].Date, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
	    case TODO_LAST_MODIFIED:
		error = SaveBackupText(file, "", "LastModified", UseUnicode);
		if (error != ERR_NONE) return error;
		error = SaveVCalDateTime(file, &ToDo->Entries[j].Date, UseUnicode);
		if (error != ERR_NONE) return error;
		break;
            case TODO_TEXT:
	        error = SaveBackupText(file, "Text", ToDo->Entries[j].Text, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_PRIVATE:
	        sprintf(buffer,"Private = %i%c%c",ToDo->Entries[j].Number,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_CATEGORY:
	        sprintf(buffer,"Category = %i%c%c",ToDo->Entries[j].Number,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_CONTACTID:
	        sprintf(buffer,"ContactID = %i%c%c",ToDo->Entries[j].Number,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_PHONE:
	        error = SaveBackupText(file, "Phone", ToDo->Entries[j].Text, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_DESCRIPTION:
	        error = SaveBackupText(file, "Description", ToDo->Entries[j].Text, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
            case TODO_LOCATION:
	        error = SaveBackupText(file, "EventLocation", ToDo->Entries[j].Text, UseUnicode);
		if (error != ERR_NONE) return error;
                break;
	    case TODO_LUID:
		error = SaveBackupText(file, "LUID", ToDo->Entries[j].Text, UseUnicode);
		if (error != ERR_NONE) return error;
		break;
        }
    }
    sprintf(buffer,"%c%c",13,10);
    error = SaveBackupText(file, "", buffer, UseUnicode);
    if (error != ERR_NONE) return error;

    return ERR_NONE;
}

static GSM_Error SaveProfileEntry(FILE *file, GSM_Profile *Profile, gboolean UseUnicode)
{
	int			j=0,k=0;
	gboolean		special=FALSE;
	unsigned char 		buffer[1000]={0};
	GSM_Error error;

	sprintf(buffer,"Location = %i%c%c",Profile->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	error = SaveBackupText(file, "Name",Profile->Name, UseUnicode);
	if (error != ERR_NONE) return error;

	if (Profile->DefaultName) {
		sprintf(buffer,"DefaultName = TRUE%c%c",13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	if (Profile->HeadSetProfile) {
		sprintf(buffer,"HeadSetProfile = TRUE%c%c",13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	if (Profile->CarKitProfile) {
		sprintf(buffer,"CarKitProfile = TRUE%c%c",13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
	}

	for (j=0;j<Profile->FeaturesNumber;j++) {
		sprintf(buffer,"Feature%02i = ",j);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
		special = FALSE;
		switch (Profile->FeatureID[j]) {
		case Profile_MessageToneID:
		case Profile_RingtoneID:
			special = TRUE;
			if (Profile->FeatureID[j] == Profile_RingtoneID) {
				sprintf(buffer,"RingtoneID%c%c",13,10);
			} else {
				sprintf(buffer,"MessageToneID%c%c",13,10);
			}
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			sprintf(buffer,"Value%02i = %i%c%c",j,Profile->FeatureValue[j],13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
				break;
			case Profile_CallerGroups:
				special = TRUE;
				sprintf(buffer,"CallerGroups%c%c",13,10);
				error = SaveBackupText(file, "", buffer, UseUnicode);
				if (error != ERR_NONE) return error;
			sprintf(buffer,"Value%02i = ",j);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			for (k=0;k<5;k++) {
				if (Profile->CallerGroups[k]) {
					sprintf(buffer,"%i",k);
					error = SaveBackupText(file, "", buffer, UseUnicode);
					if (error != ERR_NONE) return error;
				}
			}
			sprintf(buffer,"%c%c",13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case Profile_ScreenSaverNumber:
			special = TRUE;
			sprintf(buffer,"ScreenSaverNumber%c%c",13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			sprintf(buffer,"Value%02i = %i%c%c",j,Profile->FeatureValue[j],13,10);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			break;
		case Profile_CallAlert  	: sprintf(buffer,"IncomingCallAlert%c%c",13,10); 		break;
		case Profile_RingtoneVolume 	: sprintf(buffer,"RingtoneVolume%c%c",13,10); 			break;
		case Profile_Vibration		: sprintf(buffer,"Vibrating%c%c",13,10); 			break;
		case Profile_MessageTone	: sprintf(buffer,"MessageTone%c%c",13,10); 			break;
		case Profile_KeypadTone		: sprintf(buffer,"KeypadTones%c%c",13,10); 			break;
		case Profile_WarningTone	: sprintf(buffer,"WarningTones%c%c",13,10); 			break;
		case Profile_ScreenSaver	: sprintf(buffer,"ScreenSaver%c%c",13,10); 			break;
		case Profile_ScreenSaverTime	: sprintf(buffer,"ScreenSaverTimeout%c%c",13,10); 		break;
		case Profile_AutoAnswer		: sprintf(buffer,"AutomaticAnswer%c%c",13,10); 			break;
		case Profile_Lights		: sprintf(buffer,"Lights%c%c",13,10); 				break;
		default				: special = TRUE;
		}
		if (!special) {
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			sprintf(buffer,"Value%02i = ",j);
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
			switch (Profile->FeatureValue[j]) {
			case PROFILE_VOLUME_LEVEL1 		:
			case PROFILE_KEYPAD_LEVEL1 		: sprintf(buffer,"Level1%c%c",13,10); 		break;
			case PROFILE_VOLUME_LEVEL2 		:
			case PROFILE_KEYPAD_LEVEL2 		: sprintf(buffer,"Level2%c%c",13,10);		break;
			case PROFILE_VOLUME_LEVEL3 		:
			case PROFILE_KEYPAD_LEVEL3 		: sprintf(buffer,"Level3%c%c",13,10); 		break;
			case PROFILE_VOLUME_LEVEL4 		: sprintf(buffer,"Level4%c%c",13,10); 		break;
			case PROFILE_VOLUME_LEVEL5 		: sprintf(buffer,"Level5%c%c",13,10); 		break;
			case PROFILE_MESSAGE_NOTONE 		:
			case PROFILE_AUTOANSWER_OFF		:
			case PROFILE_LIGHTS_OFF  		:
			case PROFILE_SAVER_OFF			:
			case PROFILE_WARNING_OFF		:
			case PROFILE_CALLALERT_OFF	 	:
			case PROFILE_VIBRATION_OFF 		:
			case PROFILE_KEYPAD_OFF	   		: sprintf(buffer,"Off%c%c",13,10);	  	break;
			case PROFILE_CALLALERT_RINGING   	: sprintf(buffer,"Ringing%c%c",13,10);		break;
			case PROFILE_CALLALERT_RINGONCE  	: sprintf(buffer,"RingOnce%c%c",13,10);		break;
			case PROFILE_CALLALERT_ASCENDING 	: sprintf(buffer,"Ascending%c%c",13,10);        break;
			case PROFILE_CALLALERT_CALLERGROUPS	: sprintf(buffer,"CallerGroups%c%c",13,10);	break;
			case PROFILE_MESSAGE_STANDARD 		: sprintf(buffer,"Standard%c%c",13,10);  	break;
			case PROFILE_MESSAGE_SPECIAL 		: sprintf(buffer,"Special%c%c",13,10);	 	break;
			case PROFILE_MESSAGE_BEEPONCE		:
			case PROFILE_CALLALERT_BEEPONCE  	: sprintf(buffer,"BeepOnce%c%c",13,10);		break;
			case PROFILE_MESSAGE_ASCENDING		: sprintf(buffer,"Ascending%c%c",13,10); 	break;
			case PROFILE_MESSAGE_PERSONAL		: sprintf(buffer,"Personal%c%c",13,10);		break;
			case PROFILE_AUTOANSWER_ON		:
			case PROFILE_WARNING_ON			:
			case PROFILE_SAVER_ON			:
			case PROFILE_VIBRATION_ON 		: sprintf(buffer,"On%c%c",13,10);  		break;
			case PROFILE_VIBRATION_FIRST 		: sprintf(buffer,"VibrateFirst%c%c",13,10);	break;
			case PROFILE_LIGHTS_AUTO 		: sprintf(buffer,"Auto%c%c",13,10); 		break;
			case PROFILE_SAVER_TIMEOUT_5SEC	 	: sprintf(buffer,"5Seconds%c%c",13,10); 	break;
			case PROFILE_SAVER_TIMEOUT_20SEC 	: sprintf(buffer,"20Seconds%c%c",13,10); 	break;
			case PROFILE_SAVER_TIMEOUT_1MIN	 	: sprintf(buffer,"1Minute%c%c",13,10);		break;
			case PROFILE_SAVER_TIMEOUT_2MIN	 	: sprintf(buffer,"2Minutes%c%c",13,10);		break;
			case PROFILE_SAVER_TIMEOUT_5MIN	 	: sprintf(buffer,"5Minutes%c%c",13,10);		break;
			case PROFILE_SAVER_TIMEOUT_10MIN 	: sprintf(buffer,"10Minutes%c%c",13,10);	break;
			default					: sprintf(buffer,"UNKNOWN%c%c",13,10);
			}
			error = SaveBackupText(file, "", buffer, UseUnicode);
			if (error != ERR_NONE) return error;
		}
	}
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveFMStationEntry(FILE *file, GSM_FMStation *FMStation, gboolean UseUnicode)
{
	unsigned char buffer[1000]={0};
	GSM_Error error;

 	sprintf(buffer,"Location = %i%c%c",FMStation->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
        error = SaveBackupText(file, "StationName", FMStation->StationName, UseUnicode);
	if (error != ERR_NONE) return error;
        sprintf(buffer,"Frequency = %f%c%c",FMStation->Frequency,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

static GSM_Error SaveGPRSPointEntry(FILE *file, GSM_GPRSAccessPoint *GPRSPoint, gboolean UseUnicode)
{
	unsigned char buffer[1000]={0};
	GSM_Error error;

 	sprintf(buffer,"Location = %i%c%c",GPRSPoint->Location,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;
        error = SaveBackupText(file, "Name", GPRSPoint->Name, UseUnicode);
	if (error != ERR_NONE) return error;
        error = SaveBackupText(file, "URL", GPRSPoint->URL, UseUnicode);
	if (error != ERR_NONE) return error;
	if (GPRSPoint->Active) {
		sprintf(buffer,"Active = Yes%c%c",13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) return error;
	}
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) return error;

	return ERR_NONE;
}

GSM_Error SaveBackup(const char *FileName, GSM_Backup *backup, gboolean UseUnicode)
{
	int 		i=0;
	unsigned char 	buffer[1000]={0},checksum[200]={0};
	FILE 		*file;
	GSM_Error error;

	file = fopen(FileName, "wb");
	if (file == NULL) return ERR_CANTOPENFILE;

	if (UseUnicode) {
		sprintf(buffer,"%c%c", 0xFE, 0xFF);
		error = SaveBackupText(file, "", buffer, FALSE);
		if (error != ERR_NONE) goto done;
	}

	sprintf(buffer, BACKUP_MAIN_HEADER "%c%c", 13, 10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;
	sprintf(buffer, BACKUP_INFO_HEADER "%c%c", 13, 10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;
	sprintf(buffer,"[Backup]%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;
	sprintf(buffer,"IMEI = \"%s\"%c%c",backup->IMEI,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;
	sprintf(buffer,"Phone = \"%s\"%c%c",backup->Model,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;
	if (backup->Creator[0] != 0) {
		sprintf(buffer,"Creator = \"%s\"%c%c",backup->Creator,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
	}
	if (backup->DateTimeAvailable) {
		error = SaveBackupText(file, "", "DateTime", UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveVCalDateTime(file, &backup->DateTime, UseUnicode);
		if (error != ERR_NONE) goto done;
	}
	sprintf(buffer,"Format = 1.05%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;
	sprintf(buffer,"%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;

	i=0;
	while (backup->PhonePhonebook[i]!=NULL) {
		sprintf(buffer,"[PhonePBK%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SavePbkEntry(file, backup->PhonePhonebook[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->SIMPhonebook[i]!=NULL) {
		sprintf(buffer,"[SIMPBK%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SavePbkEntry(file, backup->SIMPhonebook[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->Calendar[i]!=NULL) {
		sprintf(buffer,"[Calendar%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveCalendarEntry(file, backup->Calendar[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->Note[i]!=NULL) {
		sprintf(buffer,"[Note%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveNoteEntry(file, backup->Note[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->CallerLogos[i]!=NULL) {
		sprintf(buffer,"[Caller%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveCallerEntry(file, backup->CallerLogos[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->SMSC[i]!=NULL) {
		sprintf(buffer,"[SMSC%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveSMSCEntry(file, backup->SMSC[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->WAPBookmark[i]!=NULL) {
		sprintf(buffer,"[WAPBookmark%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveWAPBookmarkEntry(file, backup->WAPBookmark[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->WAPSettings[i]!=NULL) {
		sprintf(buffer,"[WAPSettings%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveWAPSettingsEntry(file, backup->WAPSettings[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->MMSSettings[i]!=NULL) {
		sprintf(buffer,"[MMSSettings%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveWAPSettingsEntry(file, backup->MMSSettings[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->SyncMLSettings[i]!=NULL) {
		sprintf(buffer,"[SyncMLSettings%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error =  SaveSyncMLSettingsEntry(file, backup->SyncMLSettings[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->ChatSettings[i]!=NULL) {
		sprintf(buffer,"[ChatSettings%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveChatSettingsEntry(file, backup->ChatSettings[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->Ringtone[i]!=NULL) {
		sprintf(buffer,"[Ringtone%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveRingtoneEntry(file, backup->Ringtone[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->ToDo[i]!=NULL) {
		sprintf(buffer,"[TODO%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveToDoEntry(file, backup->ToDo[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
	i=0;
	while (backup->Profiles[i]!=NULL) {
		sprintf(buffer,"[Profile%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
		error = SaveProfileEntry(file, backup->Profiles[i], UseUnicode);
		if (error != ERR_NONE) goto done;
		i++;
	}
 	i=0;
 	while (backup->FMStation[i]!=NULL) {
 		sprintf(buffer,"[FMStation%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
 		error = SaveFMStationEntry(file, backup->FMStation[i], UseUnicode);
		if (error != ERR_NONE) goto done;
 		i++;
 	}
 	i=0;
 	while (backup->GPRSPoint[i]!=NULL) {
 		sprintf(buffer,"[GPRSPoint%03i]%c%c",i+1,13,10);
		error = SaveBackupText(file, "", buffer, UseUnicode);
		if (error != ERR_NONE) goto done;
 		error = SaveGPRSPointEntry(file, backup->GPRSPoint[i], UseUnicode);
		if (error != ERR_NONE) goto done;
 		i++;
 	}

	if (backup->StartupLogo!=NULL) {
		error = SaveStartupEntry(file, backup->StartupLogo, UseUnicode);
		if (error != ERR_NONE) goto done;
	}
	if (backup->OperatorLogo!=NULL) {
		error = SaveOperatorEntry(file, backup->OperatorLogo, UseUnicode);
		if (error != ERR_NONE) goto done;
	}

	fclose(file);

	FindBackupChecksum(FileName, UseUnicode, checksum);

	file = fopen(FileName, "ab");
	if (file == NULL) return ERR_CANTOPENFILE;
	sprintf(buffer,"[Checksum]%c%c",13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;
	sprintf(buffer,"MD5=%s%c%c",checksum,13,10);
	error = SaveBackupText(file, "", buffer, UseUnicode);
	if (error != ERR_NONE) goto done;

	error = ERR_NONE;

done:
	fclose(file);

	return error;
}

static void ReadPbkEntry(INI_Section *file_info, char *section, GSM_MemoryEntry *Pbk, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0};
	char			*readvalue=NULL;
	int			num=0,i=0;
	INI_Entry		*e;

	Pbk->EntriesNum = 0;
	e = INI_FindLastSectionEntry(file_info, section, UseUnicode);

	while (e != NULL) {
		num = -1;
		if (UseUnicode) {
			sprintf(buffer,"%s",DecodeUnicodeString(e->EntryName));
		} else {
			sprintf(buffer,"%s",e->EntryName);
		}
		if (strlen(buffer) == 11) {
			if (strncasecmp("Entry", buffer,   5) == 0 &&
			    strncasecmp("Type",  buffer+7, 4) == 0) {
				num = atoi(buffer+5);
			}
		}
		e = e->Prev;
		if (num != -1) {
			Pbk->Entries[Pbk->EntriesNum].AddError = ERR_NONE;
			sprintf(buffer,"Entry%02iLocation",num);
			readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
			if (readvalue == NULL) {
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Unknown;
			} else if (strcasecmp(readvalue, "Home") == 0) {
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Home;
			} else if (strcasecmp(readvalue, "Work") == 0) {
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else {
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Unknown;
			}
			sprintf(buffer,"Entry%02iType",num);
			readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
			if (strcasecmp(readvalue,"NumberGeneral") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_General;
			} else if (strcasecmp(readvalue,"NumberVideo") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_Video;
			} else if (strcasecmp(readvalue,"NumberMobileWork") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_Mobile;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else if (strcasecmp(readvalue,"NumberMobileHome") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_Mobile;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Home;
			} else if (strcasecmp(readvalue,"NumberMobile") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_Mobile;
			} else if (strcasecmp(readvalue,"NumberWork") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_General;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else if (strcasecmp(readvalue,"NumberFax") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_Fax;
			} else if (strcasecmp(readvalue,"NumberHome") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_General;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Home;
			} else if (strcasecmp(readvalue,"NumberOther") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_Other;
			} else if (strcasecmp(readvalue,"NumberMessaging") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_Messaging;
			} else if (strcasecmp(readvalue,"NumberPager") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Number_Pager;
			} else if (strcasecmp(readvalue,"Note") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Note;
			} else if (strcasecmp(readvalue,"Postal") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Postal;
			} else if (strcasecmp(readvalue,"WorkPostal") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Postal;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else if (strcasecmp(readvalue,"Email") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Email;
			} else if (strcasecmp(readvalue,"Email2") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Email2;
			} else if (strcasecmp(readvalue,"URL") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_URL;
			} else if (strcasecmp(readvalue,"FirstName") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_FirstName;
			} else if (strcasecmp(readvalue,"SecondName") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_SecondName;
			} else if (strcasecmp(readvalue,"NickName") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_NickName;
			} else if (strcasecmp(readvalue,"FormalName") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_FormalName;
			} else if (strcasecmp(readvalue,"NamePrefix") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_NamePrefix;
			} else if (strcasecmp(readvalue,"NameSuffix") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_NameSuffix;
			} else if (strcasecmp(readvalue,"LastName") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_LastName;
			} else if (strcasecmp(readvalue,"Company") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Company;
			} else if (strcasecmp(readvalue,"JobTitle") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_JobTitle;
			} else if (strcasecmp(readvalue,"Address") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_StreetAddress;
			} else if (strcasecmp(readvalue,"City") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_City;
			} else if (strcasecmp(readvalue,"State") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_State;
			} else if (strcasecmp(readvalue,"Zip") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Zip;
			} else if (strcasecmp(readvalue,"Country") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Country;
			} else if (strcasecmp(readvalue,"WorkAddress") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_StreetAddress;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else if (strcasecmp(readvalue,"WorkCity") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_City;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else if (strcasecmp(readvalue,"WorkState") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_State;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else if (strcasecmp(readvalue,"WorkZip") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Zip;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else if (strcasecmp(readvalue,"WorkCountry") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Country;
				Pbk->Entries[Pbk->EntriesNum].Location = PBK_Location_Work;
			} else if (strcasecmp(readvalue,"Custom1") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Custom1;
			} else if (strcasecmp(readvalue,"Custom2") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Custom2;
			} else if (strcasecmp(readvalue,"Custom3") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Custom3;
			} else if (strcasecmp(readvalue,"Custom4") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Custom4;
			} else if (strcasecmp(readvalue,"LUID") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_LUID;
			} else if (strcasecmp(readvalue,"VOIP") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_VOIP;
			} else if (strcasecmp(readvalue,"SWIS") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_SWIS;
			} else if (strcasecmp(readvalue,"WVID") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_WVID;
			} else if (strcasecmp(readvalue,"SIP") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_SIP;
			} else if (strcasecmp(readvalue,"DTMF") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_DTMF;
			} else if (strcasecmp(readvalue,"Name") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_Name;
			} else if (strcasecmp(readvalue,"Category") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Category;
				Pbk->Entries[Pbk->EntriesNum].Number = -1;
				sprintf(buffer,"Entry%02iNumber",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (readvalue!=NULL) {
					Pbk->Entries[Pbk->EntriesNum].Number = atoi(readvalue);
					Pbk->EntriesNum ++;
					continue;
				}
			} else if (strcasecmp(readvalue,"Private") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Private;
				Pbk->Entries[Pbk->EntriesNum].Number = 0;
				sprintf(buffer,"Entry%02iNumber",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (readvalue!=NULL) {
					Pbk->Entries[Pbk->EntriesNum].Number = atoi(readvalue);
				}
				Pbk->EntriesNum ++;
				continue;
			} else if (strcasecmp(readvalue,"CallerGroup") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Caller_Group;
				Pbk->Entries[Pbk->EntriesNum].Number = 0;
				sprintf(buffer,"Entry%02iNumber",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (readvalue!=NULL) {
					Pbk->Entries[Pbk->EntriesNum].Number = atoi(readvalue);
				}
				Pbk->EntriesNum ++;
				continue;
			} else if (strcasecmp(readvalue,"RingtoneID") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_RingtoneID;
				Pbk->Entries[Pbk->EntriesNum].Number = 0;
				sprintf(buffer,"Entry%02iNumber",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (readvalue!=NULL) {
					Pbk->Entries[Pbk->EntriesNum].Number = atoi(readvalue);
				}
				Pbk->EntriesNum ++;
				continue;
			} else if (strcasecmp(readvalue,"PictureID") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_PictureID;
				Pbk->Entries[Pbk->EntriesNum].Number = 0;
				sprintf(buffer,"Entry%02iNumber",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (readvalue!=NULL) {
					Pbk->Entries[Pbk->EntriesNum].Number = atoi(readvalue);
				}
				Pbk->EntriesNum ++;
				continue;
			} else if (strcasecmp(readvalue,"Date") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Date;
				sprintf(buffer,"Entry%02iText",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (readvalue != NULL) {
					ReadVCALDateTime(readvalue, &Pbk->Entries[Pbk->EntriesNum].Date);
				}
				Pbk->EntriesNum++;
				continue;
			} else if (strcasecmp(readvalue,"LastModified") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_LastModified;
				sprintf(buffer,"Entry%02iText",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (readvalue != NULL) {
					ReadVCALDateTime(readvalue, &Pbk->Entries[Pbk->EntriesNum].Date);
				}
				Pbk->EntriesNum++;
				continue;
			} else if (strcasecmp(readvalue,"BMPPhoto") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Photo;
				Pbk->Entries[Pbk->EntriesNum].Picture.Type = PICTURE_BMP;
				Pbk->Entries[Pbk->EntriesNum].Picture.Length = 0;
				Pbk->Entries[Pbk->EntriesNum].Picture.Buffer = NULL;
				goto loadpicture;
			} else if (strcasecmp(readvalue,"GIFPhoto") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Photo;
				Pbk->Entries[Pbk->EntriesNum].Picture.Type = PICTURE_GIF;
				Pbk->Entries[Pbk->EntriesNum].Picture.Length = 0;
				Pbk->Entries[Pbk->EntriesNum].Picture.Buffer = NULL;
				goto loadpicture;
			} else if (strcasecmp(readvalue,"JPEGPhoto") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Photo;
				Pbk->Entries[Pbk->EntriesNum].Picture.Type = PICTURE_JPG;
				Pbk->Entries[Pbk->EntriesNum].Picture.Length = 0;
				Pbk->Entries[Pbk->EntriesNum].Picture.Buffer = NULL;
				goto loadpicture;
			} else if (strcasecmp(readvalue,"ICOPhoto") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Photo;
				Pbk->Entries[Pbk->EntriesNum].Picture.Type = PICTURE_ICN;
				Pbk->Entries[Pbk->EntriesNum].Picture.Length = 0;
				Pbk->Entries[Pbk->EntriesNum].Picture.Buffer = NULL;
				goto loadpicture;
			} else if (strcasecmp(readvalue,"PNGPhoto") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Photo;
				Pbk->Entries[Pbk->EntriesNum].Picture.Type = PICTURE_PNG;
				Pbk->Entries[Pbk->EntriesNum].Picture.Length = 0;
				Pbk->Entries[Pbk->EntriesNum].Picture.Buffer = NULL;
				goto loadpicture;
			} else if (strcasecmp(readvalue,"Photo") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Photo;
				Pbk->Entries[Pbk->EntriesNum].Picture.Type = 0;
				Pbk->Entries[Pbk->EntriesNum].Picture.Length = 0;
				Pbk->Entries[Pbk->EntriesNum].Picture.Buffer = NULL;
				goto loadpicture;
			} else if (strcasecmp(readvalue,"PushToTalkID") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_PushToTalkID;
			} else if (strcasecmp(readvalue,"UserID") == 0) {
				Pbk->Entries[Pbk->EntriesNum].EntryType = PBK_Text_UserID;
			}
			goto loadtext;
loadpicture:
			sprintf(buffer,"Entry%02iData",num);
			readvalue = ReadLinkedBackupText(file_info, section, buffer, UseUnicode);
			if (readvalue != NULL) {
				/* We allocate here more memory than is actually required */
				Pbk->Entries[Pbk->EntriesNum].Picture.Buffer = (char *)malloc(strlen(readvalue));
				if (Pbk->Entries[Pbk->EntriesNum].Picture.Buffer == NULL)
					break;

				Pbk->Entries[Pbk->EntriesNum].Picture.Length =
					DecodeBASE64(readvalue, Pbk->Entries[Pbk->EntriesNum].Picture.Buffer, strlen(readvalue));

				free(readvalue);
				readvalue=NULL;
			}

			goto loaddone;
loadtext:
			sprintf(buffer,"Entry%02iText",num);
			ReadBackupText(file_info, section, buffer, Pbk->Entries[Pbk->EntriesNum].Text,UseUnicode);
			dbgprintf(NULL, "text \"%s\", type %i\n",DecodeUnicodeString(Pbk->Entries[Pbk->EntriesNum].Text),Pbk->Entries[Pbk->EntriesNum].EntryType);
			Pbk->Entries[Pbk->EntriesNum].VoiceTag = 0;
			sprintf(buffer,"Entry%02iVoiceTag",num);
			readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
			if (readvalue!=NULL) {
				Pbk->Entries[Pbk->EntriesNum].VoiceTag = atoi(readvalue);
			}
			i = 0;
			while (1) {
				Pbk->Entries[Pbk->EntriesNum].SMSList[i] = 0;
				sprintf(buffer,"Entry%02iSMSList%02i",num,i);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (readvalue==NULL) break;
				Pbk->Entries[Pbk->EntriesNum].SMSList[i] = atoi(readvalue);
				i++;
			}
loaddone:
			Pbk->EntriesNum ++;
			if (Pbk->EntriesNum >= GSM_PHONEBOOK_ENTRIES) {
				Pbk->EntriesNum--;
				return;
			}
		}
	}
}

static void ReadCalendarType(INI_Section *file_info, char *section, GSM_CalendarNoteType *type, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0};
	char			*readvalue=NULL;

	sprintf(buffer,"Type");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	*type = GSM_CAL_REMINDER;
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"Call") == 0) {
			*type = GSM_CAL_CALL;
		} else if (strcasecmp(readvalue,"Meeting") == 0) {
			*type = GSM_CAL_MEETING;
		} else if (strcasecmp(readvalue,"Birthday") == 0) {
			*type = GSM_CAL_BIRTHDAY;
		} else if (strcasecmp(readvalue,"Memo") == 0) {
			*type = GSM_CAL_MEMO;
		} else if (strcasecmp(readvalue,"Travel") == 0) {
			*type = GSM_CAL_TRAVEL;
		} else if (strcasecmp(readvalue,"Vacation") == 0) {
			*type = GSM_CAL_VACATION;
		} else if (strcasecmp(readvalue,"DailyAlarm") == 0) {
			*type = GSM_CAL_DAILY_ALARM;
		} else if (strcasecmp(readvalue,"Alarm") == 0) {
			*type = GSM_CAL_ALARM;
		} else if (strcasecmp(readvalue,"Shopping") == 0) {
			*type = GSM_CAL_SHOPPING;
		} else if (strcasecmp(readvalue,"Training/Athletism") == 0) {
			*type = GSM_CAL_T_ATHL;
		} else if (strcasecmp(readvalue,"Training/BallGames") == 0) {
			*type = GSM_CAL_T_BALL;
		} else if (strcasecmp(readvalue,"Training/Cycling") == 0) {
			*type = GSM_CAL_T_CYCL;
		} else if (strcasecmp(readvalue,"Training/Budo") == 0) {
			*type = GSM_CAL_T_BUDO;
		} else if (strcasecmp(readvalue,"Training/Dance") == 0) {
			*type = GSM_CAL_T_DANC;
		} else if (strcasecmp(readvalue,"Training/ExtremeSports") == 0) {
			*type = GSM_CAL_T_EXTR;
		} else if (strcasecmp(readvalue,"Training/Football") == 0) {
			*type = GSM_CAL_T_FOOT;
		} else if (strcasecmp(readvalue,"Training/Golf") == 0) {
			*type = GSM_CAL_T_GOLF;
		} else if (strcasecmp(readvalue,"Training/Gym") == 0) {
			*type = GSM_CAL_T_GYM;
		} else if (strcasecmp(readvalue,"Training/HorseRaces") == 0) {
			*type = GSM_CAL_T_HORS;
		} else if (strcasecmp(readvalue,"Training/Hockey") == 0) {
			*type = GSM_CAL_T_HOCK;
		} else if (strcasecmp(readvalue,"Training/Races") == 0) {
			*type = GSM_CAL_T_RACE;
		} else if (strcasecmp(readvalue,"Training/Rugby") == 0) {
			*type = GSM_CAL_T_RUGB;
		} else if (strcasecmp(readvalue,"Training/Sailing") == 0) {
			*type = GSM_CAL_T_SAIL;
		} else if (strcasecmp(readvalue,"Training/StreetGames") == 0) {
			*type = GSM_CAL_T_STRE;
		} else if (strcasecmp(readvalue,"Training/Swimming") == 0) {
			*type = GSM_CAL_T_SWIM;
		} else if (strcasecmp(readvalue,"Training/Tennis") == 0) {
			*type = GSM_CAL_T_TENN;
		} else if (strcasecmp(readvalue,"Training/Travels") == 0) {
			*type = GSM_CAL_T_TRAV;
		} else if (strcasecmp(readvalue,"Training/WinterGames") == 0) {
			*type = GSM_CAL_T_WINT;
		} else if (strcasecmp(readvalue,"0") == 0) {
			*type = 0;
		}
	}
}
static GSM_Error ReadCalendarEntry(INI_Section *file_info, char *section, GSM_CalendarEntry *note, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0},buf[20]={0};
	char			*readvalue=NULL;
	int			rec=0,rec2=0;

	sprintf(buffer,"Location");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) note->Location = atoi(readvalue);

	ReadCalendarType(file_info,section, &(note->Type), UseUnicode);

	note->EntriesNum = 0;
	sprintf(buffer,"Text");
	if (ReadBackupText(file_info, section, buffer, note->Entries[note->EntriesNum].Text,UseUnicode)) {
		note->Entries[note->EntriesNum].EntryType = CAL_TEXT;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"Description");
	if (ReadBackupText(file_info, section, buffer, note->Entries[note->EntriesNum].Text,UseUnicode)) {
		note->Entries[note->EntriesNum].EntryType = CAL_DESCRIPTION;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"LUID");
	if (ReadBackupText(file_info, section, buffer, note->Entries[note->EntriesNum].Text,UseUnicode)) {
		note->Entries[note->EntriesNum].EntryType = CAL_LUID;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"Phone");
	if (ReadBackupText(file_info, section, buffer, note->Entries[note->EntriesNum].Text,UseUnicode)) {
		note->Entries[note->EntriesNum].EntryType = CAL_PHONE;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"Private");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_PRIVATE;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"EventLocation");
	if (ReadBackupText(file_info, section, buffer, note->Entries[note->EntriesNum].Text,UseUnicode)) {
		note->Entries[note->EntriesNum].EntryType = CAL_LOCATION;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"ContactID");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_CONTACTID;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	/* StartTime must be before Recurrance */
	sprintf(buffer,"StartTime");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &note->Entries[note->EntriesNum].Date)) {
		note->Entries[note->EntriesNum].EntryType = CAL_START_DATETIME;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"Recurrance");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		rec2 	= -1;
		rec 	= atoi(readvalue);
		switch (rec) {
		case 1:
			rec2 = 1*24;
			break;
		case 7:
			rec2 = 7*24;
			break;
		case 14:
			rec2 = 14*24;
			break;
		case 30:
		case ((0xffff-1)/24):
			rec2 = 0xffff-1;
			break;
		case 365:
			rec2 = 0xffff;
		}
		if (rec2 != -1) {
			buf[0] = rec2 / 256;
			buf[1] = rec2 % 256;
			dbgprintf(NULL, "Setting recurrance %i\n",rec2);
			GSM_GetCalendarRecurranceRepeat(NULL, buf, NULL, note);
		}
	}
	sprintf(buffer,"StopTime");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &note->Entries[note->EntriesNum].Date)) {
		note->Entries[note->EntriesNum].EntryType = CAL_END_DATETIME;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	/* This is for compatibility with older backup formats */
	sprintf(buffer,"Alarm");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &note->Entries[note->EntriesNum].Date)) {
		note->Entries[note->EntriesNum].EntryType = CAL_TONE_ALARM_DATETIME;
		sprintf(buffer,"AlarmType");
		readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
		if (readvalue!=NULL)
		{
			if (strcasecmp(readvalue,"Silent") == 0) {
				note->Entries[note->EntriesNum].EntryType = CAL_SILENT_ALARM_DATETIME;
			}
		}
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"ToneAlarm");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &note->Entries[note->EntriesNum].Date)) {
		note->Entries[note->EntriesNum].EntryType = CAL_TONE_ALARM_DATETIME;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"SilentAlarm");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &note->Entries[note->EntriesNum].Date)) {
		note->Entries[note->EntriesNum].EntryType = CAL_SILENT_ALARM_DATETIME;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"LastModified");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL  && ReadVCALDateTime(readvalue, &note->Entries[note->EntriesNum].Date)) {
        	note->Entries[note->EntriesNum].EntryType = CAL_LAST_MODIFIED;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
        	note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
   	}

	sprintf(buffer,"RepeatFrequency");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_FREQUENCY;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"RepeatDayOfWeek");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_DAYOFWEEK;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"RepeatDay");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_DAY;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"RepeatWeekOfMonth");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_WEEKOFMONTH;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"RepeatMonth");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_MONTH;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"RepeatCount");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_COUNT;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"RepeatDayOfYear");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		note->Entries[note->EntriesNum].Number 	  = atoi(readvalue);
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_DAYOFYEAR;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"RepeatStartDate");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &note->Entries[note->EntriesNum].Date)) {
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_STARTDATE;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}
	sprintf(buffer,"RepeatStopDate");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &note->Entries[note->EntriesNum].Date)) {
		note->Entries[note->EntriesNum].EntryType = CAL_REPEAT_STOPDATE;
		note->Entries[note->EntriesNum].AddError = ERR_NONE;
		note->EntriesNum++;
		if (note->EntriesNum >= GSM_CALENDAR_ENTRIES) return ERR_MOREMEMORY;
	}

	return ERR_NONE;
}

static GSM_Error ReadToDoEntry(INI_Section *file_info, char *section, GSM_ToDoEntry *ToDo, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0};
	char			*readvalue=NULL;

    	ToDo->EntriesNum = 0;

	sprintf(buffer,"Location");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) ToDo->Location = atoi(readvalue);

	ReadCalendarType(file_info,section, &(ToDo->Type), UseUnicode);

	ToDo->Priority = GSM_Priority_High;
	sprintf(buffer,"Priority");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		if (!strcmp(readvalue,"3") || !strcmp(readvalue,"Low")) {
			ToDo->Priority = GSM_Priority_Low;
		}
		if (!strcmp(readvalue,"2") || !strcmp(readvalue,"Medium")) {
			ToDo->Priority = GSM_Priority_Medium;
		}
	}

	sprintf(buffer,"StartTime");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL  && ReadVCALDateTime(readvalue, &ToDo->Entries[ToDo->EntriesNum].Date)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_START_DATETIME;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
   	}

	sprintf(buffer,"CompletedTime");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL  && ReadVCALDateTime(readvalue, &ToDo->Entries[ToDo->EntriesNum].Date)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_COMPLETED_DATETIME;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
   	}

	sprintf(buffer,"Text");
	if (ReadBackupText(file_info, section, buffer, ToDo->Entries[ToDo->EntriesNum].Text,UseUnicode)) {
  	      	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_TEXT;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"Description");
	if (ReadBackupText(file_info, section, buffer, ToDo->Entries[ToDo->EntriesNum].Text,UseUnicode)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_DESCRIPTION;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"EventLocation");
	if (ReadBackupText(file_info, section, buffer, ToDo->Entries[ToDo->EntriesNum].Text,UseUnicode)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_LOCATION;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"LUID");
	if (ReadBackupText(file_info, section, buffer, ToDo->Entries[ToDo->EntriesNum].Text,UseUnicode)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_LUID;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"Phone");
	if (ReadBackupText(file_info, section, buffer, ToDo->Entries[ToDo->EntriesNum].Text,UseUnicode)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_PHONE;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"Private");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
        	ToDo->Entries[ToDo->EntriesNum].Number 		= atoi(readvalue);
        	ToDo->Entries[ToDo->EntriesNum].EntryType 	= TODO_PRIVATE;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"Completed");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		if (strncmp(readvalue, "yes", 3) == 0) {
			ToDo->Entries[ToDo->EntriesNum].Number 	= 1;
		} else {
			ToDo->Entries[ToDo->EntriesNum].Number 	= 0;
		}
        	ToDo->Entries[ToDo->EntriesNum].EntryType 	= TODO_COMPLETED;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"Category");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
        	ToDo->Entries[ToDo->EntriesNum].Number		= atoi(readvalue);
        	ToDo->Entries[ToDo->EntriesNum].EntryType 	= TODO_CATEGORY;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"ContactID");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
       	 	ToDo->Entries[ToDo->EntriesNum].Number 		= atoi(readvalue);
        	ToDo->Entries[ToDo->EntriesNum].EntryType 	= TODO_CONTACTID;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"DueTime");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL  && ReadVCALDateTime(readvalue, &ToDo->Entries[ToDo->EntriesNum].Date)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_END_DATETIME;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
   	}

	sprintf(buffer,"LastModified");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL  && ReadVCALDateTime(readvalue, &ToDo->Entries[ToDo->EntriesNum].Date)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_LAST_MODIFIED;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
   	}

	sprintf(buffer,"Alarm");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &ToDo->Entries[ToDo->EntriesNum].Date)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_ALARM_DATETIME;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}

	sprintf(buffer,"SilentAlarm");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &ToDo->Entries[ToDo->EntriesNum].Date)) {
        	ToDo->Entries[ToDo->EntriesNum].EntryType = TODO_SILENT_ALARM_DATETIME;
        	ToDo->EntriesNum++;
		if (ToDo->EntriesNum >= GSM_TODO_ENTRIES) return ERR_MOREMEMORY;
    	}
	return ERR_NONE;
}

static gboolean ReadBitmapEntry(INI_Section *file_info, char *section, GSM_Bitmap *bitmap, gboolean UseUnicode)
{
	char		*readvalue=NULL;
	unsigned char	buffer[10000]={0};
	size_t 		Width=0, Height=0;
	size_t		x=0, y=0;

	GSM_GetMaxBitmapWidthHeight(bitmap->Type, &Width, &Height);
	sprintf(buffer,"Width");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue==NULL) bitmap->BitmapWidth = Width; else bitmap->BitmapWidth = atoi(readvalue);
	sprintf(buffer,"Height");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue==NULL) bitmap->BitmapHeight = Height; else bitmap->BitmapHeight = atoi(readvalue);
	GSM_ClearBitmap(bitmap);
	for (y=0;y<bitmap->BitmapHeight;y++) {
		sprintf(buffer,"Bitmap%02i",(int)y);
		readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
		if (readvalue!=NULL) {
			for (x=0;x<bitmap->BitmapWidth;x++) {
				if (readvalue[x+1]=='#') GSM_SetPointBitmap(bitmap,x,y);
			}
		} else return FALSE;
	}
	return TRUE;
}

static void ReadCallerEntry(INI_Section *file_info, char *section, GSM_Bitmap *bitmap, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0};
	char			*readvalue=NULL;

	bitmap->Type 		= GSM_CallerGroupLogo;
	bitmap->DefaultBitmap 	= !ReadBitmapEntry(file_info, section, bitmap, UseUnicode);
	if (bitmap->DefaultBitmap) {
		bitmap->BitmapWidth  = 72;
		bitmap->BitmapHeight = 14;
		GSM_ClearBitmap(bitmap);
	}
	sprintf(buffer,"Name");
	ReadBackupText(file_info, section, buffer, bitmap->Text,UseUnicode);
	if (bitmap->Text[0] == 0x00 && bitmap->Text[1] == 0x00) {
		bitmap->DefaultName = TRUE;
	} else {
		bitmap->DefaultName = FALSE;
	}
	sprintf(buffer,"Ringtone");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue==NULL) {
		sprintf(buffer,"FileRingtone");
		readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
		if (readvalue==NULL) {
			bitmap->DefaultRingtone = TRUE;
		} else {
			DecodeHexBin (&bitmap->RingtoneID, readvalue, 2);
			bitmap->DefaultRingtone 	= FALSE;
			bitmap->FileSystemRingtone 	= TRUE;
		}
	} else {
		DecodeHexBin (&bitmap->RingtoneID, readvalue, 2);
		bitmap->DefaultRingtone 	= FALSE;
		bitmap->FileSystemRingtone 	= FALSE;
	}
	sprintf(buffer,"Enabled");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
        bitmap->BitmapEnabled = TRUE;
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"False") == 0) bitmap->BitmapEnabled = FALSE;
	}
	bitmap->FileSystemPicture = FALSE;
	/* FIXME */
}

static void ReadStartupEntry(INI_Section *file_info, char *section, GSM_Bitmap *bitmap, gboolean UseUnicode)
{
	unsigned char buffer[10000]={0};

	sprintf(buffer,"Text");
	ReadBackupText(file_info, section, buffer, bitmap->Text,UseUnicode);
	if (bitmap->Text[0]!=0 || bitmap->Text[1]!=0) {
		bitmap->Type = GSM_WelcomeNote_Text;
	} else {
		bitmap->Type 	 = GSM_StartupLogo;
		bitmap->Location = 1;
		ReadBitmapEntry(file_info, section, bitmap, UseUnicode);
#ifdef DEBUG
		if (GSM_global_debug.dl == DL_TEXTALL || GSM_global_debug.dl == DL_TEXTALLDATE)
			GSM_PrintBitmap(GSM_global_debug.df,bitmap);
#endif
	}
}

static void ReadWAPBookmarkEntry(INI_Section *file_info, char *section, GSM_WAPBookmark *bookmark, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0};

	sprintf(buffer,"URL");
	ReadBackupText(file_info, section, buffer, bookmark->Address,UseUnicode);
	sprintf(buffer,"Title");
	ReadBackupText(file_info, section, buffer, bookmark->Title,UseUnicode);
}

static void ReadOperatorEntry(INI_Section *file_info, char *section, GSM_Bitmap *bitmap, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0};
	char			*readvalue=NULL;

	sprintf(buffer,"Network");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	memcpy(bitmap->NetworkCode, readvalue + 1, 6);
	bitmap->NetworkCode[6] = 0;
	bitmap->Type = GSM_OperatorLogo;
	ReadBitmapEntry(file_info, section, bitmap, UseUnicode);
}

static void ReadSMSCEntry(INI_Section *file_info, char *section, GSM_SMSC *SMSC, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0};
	char			*readvalue=NULL;

	sprintf(buffer,"Name");
	ReadBackupText(file_info, section, buffer, SMSC->Name,UseUnicode);
	sprintf(buffer,"Number");
	ReadBackupText(file_info, section, buffer, SMSC->Number,UseUnicode);
	sprintf(buffer,"DefaultNumber");
	ReadBackupText(file_info, section, buffer, SMSC->DefaultNumber,UseUnicode);
	sprintf(buffer,"Format");
	SMSC->Format = SMS_FORMAT_Text;
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"Fax") == 0) {
			SMSC->Format = SMS_FORMAT_Fax;
		} else if (strcasecmp(readvalue,"Email") == 0) {
			SMSC->Format = SMS_FORMAT_Email;
		} else if (strcasecmp(readvalue,"Pager") == 0) {
			SMSC->Format = SMS_FORMAT_Pager;
		}
	}
	sprintf(buffer,"Validity");
	SMSC->Validity.Relative = SMS_VALID_Max_Time;
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"1hour") == 0) {
			SMSC->Validity.Relative = SMS_VALID_1_Hour;
		} else if (strcasecmp(readvalue,"6hours") == 0) {
			SMSC->Validity.Relative = SMS_VALID_6_Hours;
		} else if (strcasecmp(readvalue,"24hours") == 0) {
			SMSC->Validity.Relative = SMS_VALID_1_Day;
		} else if (strcasecmp(readvalue,"72hours") == 0) {
			SMSC->Validity.Relative = SMS_VALID_3_Days;
		} else if (strcasecmp(readvalue,"1week") == 0) {
			SMSC->Validity.Relative = SMS_VALID_1_Week;
		}
	}
}

static void ReadWAPSettingsEntry(INI_Section *file_info, char *section, GSM_MultiWAPSettings *settings, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0}, *readvalue=NULL;
	int			num=0;
	INI_Entry		*e;

	settings->ActiveBearer = WAPSETTINGS_BEARER_DATA;
	sprintf(buffer,"Bearer");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"SMS") == 0) {
			settings->ActiveBearer = WAPSETTINGS_BEARER_SMS;
		} else if (strcasecmp(readvalue,"GPRS") == 0) {
			settings->ActiveBearer = WAPSETTINGS_BEARER_GPRS;
		} else if (strcasecmp(readvalue,"USSD") == 0) {
			settings->ActiveBearer = WAPSETTINGS_BEARER_USSD;
		}
	}

	settings->Active = FALSE;
	sprintf(buffer,"Active");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"Yes") == 0) settings->Active = TRUE;
	}

	settings->ReadOnly = FALSE;
	sprintf(buffer,"ReadOnly");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"Yes") == 0) settings->ReadOnly = TRUE;
	}

	sprintf(buffer,"Proxy");
	ReadBackupText(file_info, section, buffer, settings->Proxy,UseUnicode);
	sprintf(buffer,"ProxyPort");
	settings->ProxyPort = 8080;
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) settings->ProxyPort = atoi(readvalue);
	sprintf(buffer,"Proxy2");
	ReadBackupText(file_info, section, buffer, settings->Proxy2,UseUnicode);
	sprintf(buffer,"Proxy2Port");
	settings->Proxy2Port = 8080;
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) settings->Proxy2Port = atoi(readvalue);

	settings->Number = 0;
	e = INI_FindLastSectionEntry(file_info, section, UseUnicode);
	while (e != NULL) {
		num = -1;
		if (UseUnicode) {
			sprintf(buffer,"%s",DecodeUnicodeString(e->EntryName));
		} else {
			sprintf(buffer,"%s",e->EntryName);
		}
		if (strlen(buffer) == 7) {
			if (strncasecmp("Title", buffer,5) == 0) num = atoi(buffer+5);
		}
		e = e->Prev;
		if (num != -1) {
			sprintf(buffer,"Title%02i",num);
			ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].Title,UseUnicode);
			sprintf(buffer,"HomePage%02i",num);
			ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].HomePage,UseUnicode);
			sprintf(buffer,"Type%02i",num);
			settings->Settings[settings->Number].IsContinuous = TRUE;
			readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
			if (readvalue!=NULL) {
				if (strcasecmp(readvalue,"Temporary") == 0) settings->Settings[settings->Number].IsContinuous = FALSE;
			}
			sprintf(buffer,"Security%02i",num);
			settings->Settings[settings->Number].IsSecurity = TRUE;
			readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
			if (readvalue!=NULL)
			{
				if (strcasecmp(readvalue,"Off") == 0) settings->Settings[settings->Number].IsSecurity = FALSE;
			}
			sprintf(buffer,"Bearer%02i",num);
			readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
			if (readvalue!=NULL)
			{
				if (strcasecmp(readvalue,"SMS") == 0) {
					settings->Settings[settings->Number].Bearer = WAPSETTINGS_BEARER_SMS;
					sprintf(buffer,"Server%02i",num);
					ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].Server,UseUnicode);
					sprintf(buffer,"Service%02i",num);
					ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].Service,UseUnicode);
				} else if ((strcasecmp(readvalue,"Data") == 0 || strcasecmp(readvalue,"GPRS") == 0)) {
					settings->Settings[settings->Number].Bearer = WAPSETTINGS_BEARER_DATA;
					if (strcasecmp(readvalue,"GPRS") == 0) settings->Settings[settings->Number].Bearer = WAPSETTINGS_BEARER_GPRS;
					sprintf(buffer,"Number%02i",num);
					ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].DialUp,UseUnicode);
					sprintf(buffer,"IP%02i",num);
					ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].IPAddress,UseUnicode);
					sprintf(buffer,"User%02i",num);
					ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].User,UseUnicode);
					sprintf(buffer,"Password%02i",num);
					ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].Password,UseUnicode);
					sprintf(buffer,"Authentication%02i",num);
					settings->Settings[settings->Number].IsNormalAuthentication = TRUE;
					readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
					if (readvalue!=NULL)
					{
						if (strcasecmp(readvalue,"Secure") == 0) settings->Settings[settings->Number].IsNormalAuthentication = FALSE;
					}
					sprintf(buffer,"CallSpeed%02i",num);
					settings->Settings[settings->Number].Speed = WAPSETTINGS_SPEED_14400;
					readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
					if (readvalue!=NULL)
					{
						if (strcasecmp(readvalue,"9600") == 0) settings->Settings[settings->Number].Speed = WAPSETTINGS_SPEED_9600;
						if (strcasecmp(readvalue,"auto") == 0) settings->Settings[settings->Number].Speed = WAPSETTINGS_SPEED_AUTO;
					}
					sprintf(buffer,"Login%02i",num);
					settings->Settings[settings->Number].ManualLogin = FALSE;
					readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
					if (readvalue!=NULL)
					{
						if (strcasecmp(readvalue,"Manual") == 0) settings->Settings[settings->Number].ManualLogin = TRUE;
					}
					sprintf(buffer,"CallType%02i",num);
					settings->Settings[settings->Number].IsISDNCall = TRUE;
					readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
					if (readvalue!=NULL)
					{
						if (strcasecmp(readvalue,"Analogue") == 0) settings->Settings[settings->Number].IsISDNCall = FALSE;
					}
				} else if (strcasecmp(readvalue,"USSD") == 0) {
					settings->Settings[settings->Number].Bearer = WAPSETTINGS_BEARER_USSD;
					sprintf(buffer,"ServiceCode%02i",num);
					ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].Code,UseUnicode);
					sprintf(buffer,"IP%02i",num);
					readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
					if (readvalue!=NULL) {
						settings->Settings[settings->Number].IsIP = TRUE;
						sprintf(buffer,"IP%02i",num);
					} else {
						settings->Settings[settings->Number].IsIP = FALSE;
						sprintf(buffer,"Number%02i",num);
					}
					ReadBackupText(file_info, section, buffer, settings->Settings[settings->Number].Service,UseUnicode);
				}
			}
			settings->Number++;
		}
	}
}

static void ReadRingtoneEntry(INI_Section *file_info, char *section, GSM_Ringtone *ringtone, gboolean UseUnicode)
{
	unsigned char buffer[10000]={0}, *readvalue=NULL;
	char *buffer2=NULL;

	sprintf(buffer,"Name");
	ReadBackupText(file_info, section, buffer, ringtone->Name,UseUnicode);
	ringtone->Location = 0;
	sprintf(buffer,"Location");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) ringtone->Location = atoi(readvalue);
	sprintf(buffer,"NokiaBinary00");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		ringtone->Format = RING_NOKIABINARY;
		buffer2 = ReadLinkedBackupText(file_info, section, "NokiaBinary", UseUnicode);
		DecodeHexBin (ringtone->NokiaBinary.Frame, buffer2, strlen(buffer2));
		ringtone->NokiaBinary.Length = strlen(buffer2)/2;
		free(buffer2);
		buffer2=NULL;
	}
	sprintf(buffer,"Pure Midi00");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		ringtone->Format = RING_MIDI;
		buffer2 = ReadLinkedBackupText(file_info, section, "Pure Midi", UseUnicode);
		DecodeHexBin (ringtone->NokiaBinary.Frame, buffer2, strlen(buffer2));
		ringtone->NokiaBinary.Length = strlen(buffer2)/2;
		free(buffer2);
		buffer2=NULL;
	}

}

static void ReadProfileEntry(INI_Section *file_info, char *section, GSM_Profile *Profile, gboolean UseUnicode)
{
	unsigned char		buffer[10000]={0};
	char			*readvalue=NULL;
	gboolean		unknown;
	int			num=0,j=0;
	INI_Entry		*e;

	sprintf(buffer,"Name");
	ReadBackupText(file_info, section, buffer, Profile->Name,UseUnicode);

	sprintf(buffer,"Location");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	Profile->Location = atoi(readvalue);

	Profile->DefaultName = FALSE;
	sprintf(buffer,"DefaultName");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL && strcasecmp(buffer,"TRUE") == 0) Profile->DefaultName = TRUE;

	Profile->HeadSetProfile = FALSE;
	sprintf(buffer,"HeadSetProfile");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL && strcasecmp(buffer,"TRUE") == 0) Profile->HeadSetProfile = TRUE;

	Profile->CarKitProfile = FALSE;
	sprintf(buffer,"CarKitProfile");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL && strcasecmp(buffer,"TRUE") == 0) Profile->CarKitProfile = TRUE;

	Profile->FeaturesNumber = 0;
	e = INI_FindLastSectionEntry(file_info, section, UseUnicode);
	while (e != NULL) {
		num = -1;
		if (UseUnicode) {
			sprintf(buffer,"%s",DecodeUnicodeString(e->EntryName));
		} else {
			sprintf(buffer,"%s",e->EntryName);
		}
		if (strlen(buffer) == 9) {
			if (strncasecmp("Feature", buffer, 7) == 0) num = atoi(buffer+7);
		}
		e = e->Prev;
		if (num != -1) {
			sprintf(buffer,"Feature%02i",num);
			readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
			if (readvalue==NULL) break;
			unknown = TRUE;
			if (strcasecmp(readvalue,"RingtoneID") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_RingtoneID;
				sprintf(buffer,"Value%02i",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				Profile->FeatureValue[Profile->FeaturesNumber]=atoi(readvalue);
				Profile->FeaturesNumber++;
			} else if (strcasecmp(readvalue,"MessageToneID") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_MessageToneID;
				sprintf(buffer,"Value%02i",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				Profile->FeatureValue[Profile->FeaturesNumber]=atoi(readvalue);
				Profile->FeaturesNumber++;
			} else if (strcasecmp(readvalue,"ScreenSaverNumber") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_ScreenSaverNumber;
				sprintf(buffer,"Value%02i",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				Profile->FeatureValue[Profile->FeaturesNumber]=atoi(readvalue);
				Profile->FeaturesNumber++;
			} else if (strcasecmp(readvalue,"CallerGroups") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_CallerGroups;
				sprintf(buffer,"Value%02i",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				for (j = 0; j < 5; j++) {
					Profile->CallerGroups[j] = FALSE;
					if (strchr(readvalue, '1' + j) != NULL) {
						Profile->CallerGroups[j] = TRUE;
					}
				}
				Profile->FeaturesNumber++;
			} else if (strcasecmp(readvalue,"IncomingCallAlert") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_CallAlert;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"RingtoneVolume") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_RingtoneVolume;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"Vibrating") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_Vibration;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"MessageTone") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_MessageTone;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"KeypadTones") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_KeypadTone;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"WarningTones") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_WarningTone;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"ScreenSaver") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_ScreenSaver;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"ScreenSaverTimeout") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_ScreenSaverTime;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"AutomaticAnswer") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_AutoAnswer;
				unknown = FALSE;
			} else if (strcasecmp(readvalue,"Lights") == 0) {
				Profile->FeatureID[Profile->FeaturesNumber]=Profile_Lights;
				unknown = FALSE;
			}
			if (!unknown) {
				sprintf(buffer,"Value%02i",num);
				readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
				if (strcasecmp(readvalue,"Level1") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_VOLUME_LEVEL1;
					if (Profile->FeatureID[Profile->FeaturesNumber]==Profile_KeypadTone) {
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_KEYPAD_LEVEL1;
					}
				} else if (strcasecmp(readvalue,"Level2") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_VOLUME_LEVEL2;
					if (Profile->FeatureID[Profile->FeaturesNumber]==Profile_KeypadTone) {
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_KEYPAD_LEVEL2;
					}
				} else if (strcasecmp(readvalue,"Level3") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_VOLUME_LEVEL3;
					if (Profile->FeatureID[Profile->FeaturesNumber]==Profile_KeypadTone) {
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_KEYPAD_LEVEL3;
					}
				} else if (strcasecmp(readvalue,"Level4") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_VOLUME_LEVEL4;
				} else if (strcasecmp(readvalue,"Level5") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_VOLUME_LEVEL5;
				} else if (strcasecmp(readvalue,"Off") == 0) {
					switch (Profile->FeatureID[Profile->FeaturesNumber]) {
					case Profile_MessageTone:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_MESSAGE_NOTONE;
						break;
					case Profile_AutoAnswer:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_AUTOANSWER_OFF;
						break;
					case Profile_Lights:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_LIGHTS_OFF;
						break;
					case Profile_ScreenSaver:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_SAVER_OFF;
						break;
					case Profile_WarningTone:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_WARNING_OFF;
						break;
					case Profile_CallAlert:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_CALLALERT_OFF;
						break;
					case Profile_Vibration:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_VIBRATION_OFF;
						break;
					default:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_KEYPAD_OFF;
						break;
					}
				} else if (strcasecmp(readvalue,"Ringing") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_CALLALERT_RINGING;
				} else if (strcasecmp(readvalue,"BeepOnce") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_CALLALERT_BEEPONCE;
					if (Profile->FeatureID[Profile->FeaturesNumber]==Profile_MessageTone) {
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_MESSAGE_BEEPONCE;
					}
				} else if (strcasecmp(readvalue,"RingOnce") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_CALLALERT_RINGONCE;
				} else if (strcasecmp(readvalue,"Ascending") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_CALLALERT_ASCENDING;
				} else if (strcasecmp(readvalue,"CallerGroups") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_CALLALERT_CALLERGROUPS;
				} else if (strcasecmp(readvalue,"Standard") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_MESSAGE_STANDARD;
				} else if (strcasecmp(readvalue,"Special") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_MESSAGE_SPECIAL;
				} else if (strcasecmp(readvalue,"Ascending") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_MESSAGE_ASCENDING;
				} else if (strcasecmp(readvalue,"Personal") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_MESSAGE_PERSONAL;
				} else if (strcasecmp(readvalue,"VibrateFirst") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_VIBRATION_FIRST;
				} else if (strcasecmp(readvalue,"Auto") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_LIGHTS_AUTO;
				} else if (strcasecmp(readvalue,"5Seconds") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_SAVER_TIMEOUT_5SEC;
				} else if (strcasecmp(readvalue,"20Seconds") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_SAVER_TIMEOUT_20SEC;
				} else if (strcasecmp(readvalue,"1Minute") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_SAVER_TIMEOUT_1MIN;
				} else if (strcasecmp(readvalue,"2Minutes") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_SAVER_TIMEOUT_2MIN;
				} else if (strcasecmp(readvalue,"5Minutes") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_SAVER_TIMEOUT_5MIN;
				} else if (strcasecmp(readvalue,"10Minutes") == 0) {
					Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_SAVER_TIMEOUT_10MIN;
				} else if (strcasecmp(readvalue,"On") == 0) {
					switch (Profile->FeatureID[Profile->FeaturesNumber]) {
					case Profile_AutoAnswer:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_AUTOANSWER_ON;
						break;
					case Profile_WarningTone:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_WARNING_ON;
						break;
					case Profile_ScreenSaver:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_SAVER_ON;
						break;
					default:
						Profile->FeatureValue[Profile->FeaturesNumber]=PROFILE_VIBRATION_ON;
						break;
					}
				} else unknown = TRUE;
			}
			if (!unknown) Profile->FeaturesNumber++;
		}
	}
}

static GSM_Error ReadFMStationEntry(INI_Section *file_info, char *section, GSM_FMStation *FMStation, gboolean UseUnicode)
{
	unsigned char buffer[10000]={0}, *readvalue=NULL;
	char *endptr;

	FMStation->Location  = 0;
	FMStation->Frequency = 0;

	sprintf(buffer,"Location");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) FMStation->Location = atoi(readvalue);

	sprintf(buffer,"StationName");
	ReadBackupText(file_info, section, buffer, FMStation->StationName,UseUnicode);

	sprintf(buffer,"Frequency");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue != NULL) {
		FMStation->Frequency = strtod(readvalue, &endptr);
		if (*endptr != 0) {
			return ERR_FILENOTSUPPORTED;
		}
	}
	return ERR_NONE;
}

static void ReadGPRSPointEntry(INI_Section *file_info, char *section, GSM_GPRSAccessPoint *GPRSPoint, gboolean UseUnicode)
{
	unsigned char buffer[10000]={0}, *readvalue=NULL;

	GPRSPoint->Name[0]  = 0;
	GPRSPoint->Name[1]  = 0;
	GPRSPoint->URL[0]   = 0;
	GPRSPoint->URL[1]   = 0;
	GPRSPoint->Location = 0;

	GPRSPoint->Active = FALSE;
	sprintf(buffer,"Active");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"Yes") == 0) GPRSPoint->Active = TRUE;
	}

	sprintf(buffer,"Location");
	readvalue = ReadCFGText(file_info, section, buffer, UseUnicode);
	if (readvalue!=NULL) GPRSPoint->Location = atoi(readvalue);

	sprintf(buffer,"Name");
	ReadBackupText(file_info, section, buffer, GPRSPoint->Name,UseUnicode);

	sprintf(buffer,"URL");
	ReadBackupText(file_info, section, buffer, GPRSPoint->URL,UseUnicode);
}

static void ReadNoteEntry(INI_Section *file_info, char *section, GSM_NoteEntry *Note, gboolean UseUnicode)
{
	unsigned char buffer[100]={0};

	sprintf(buffer,"Text");
	ReadBackupText(file_info, section, buffer, Note->Text,UseUnicode);
}

GSM_Error LoadBackup(const char *FileName, GSM_Backup *backup)
{
	INI_Section		*file_info, *h;
	char			buffer[100]={0}, *readvalue=NULL;
	int			num=0;
	gboolean		found;
	GSM_Error		error;
	gboolean		UseUnicode = FALSE;
	FILE			*file;
	unsigned char		guessbuffer[10]={0};
	size_t			readbytes=0;

	file = fopen(FileName, "rb");
	if (file == NULL) return ERR_CANTOPENFILE;
	readbytes = fread(guessbuffer, 1, 9, file); /* Read the header of the file. */
	fclose(file);
	if (readbytes >= 2 && ((guessbuffer[0] == 0xFE && guessbuffer[1] == 0xFF) ||
			(guessbuffer[0] == 0xFF && guessbuffer[1] == 0xFE))) {
		UseUnicode = TRUE;
	}


	error = INI_ReadFile(FileName, UseUnicode, &file_info);
	if (error != ERR_NONE) {
		return error;
	}

	sprintf(buffer,"Backup");
	if (UseUnicode) EncodeUnicode(buffer,"Backup",6);

	readvalue = ReadCFGText(file_info, buffer, "Format", UseUnicode);
	/* Did we read anything? */
	if (readvalue == NULL) {
		error = ERR_FILENOTSUPPORTED;
		goto fail_error;
	}
	/* Is this format version supported ? */
	if (strcmp(readvalue,"1.01")!=0 && strcmp(readvalue,"1.02")!=0 &&
            strcmp(readvalue,"1.05")!=0 &&
            strcmp(readvalue,"1.03")!=0 && strcmp(readvalue,"1.04")!=0) {
		error = ERR_FILENOTSUPPORTED;
		goto fail_error;
	}

	readvalue = ReadCFGText(file_info, buffer, "IMEI", UseUnicode);
	if (readvalue!=NULL) strcpy(backup->IMEI,readvalue);
	readvalue = ReadCFGText(file_info, buffer, "Phone", UseUnicode);
	if (readvalue!=NULL) strcpy(backup->Model,readvalue);
	readvalue = ReadCFGText(file_info, buffer, "Creator", UseUnicode);
	if (readvalue!=NULL) {
		strncpy(backup->Creator,readvalue, sizeof(backup->Creator) - 1);
		backup->Creator[sizeof(backup->Creator) - 1] = 0;
	}
	readvalue = ReadCFGText(file_info, buffer, "DateTime", UseUnicode);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &backup->DateTime)) {
		backup->DateTimeAvailable = TRUE;
	}

	sprintf(buffer,"Checksum");
	if (UseUnicode) EncodeUnicode(buffer,"Checksum",8);
	readvalue = ReadCFGText(file_info, buffer, "MD5", UseUnicode);
	if (readvalue!=NULL) strcpy(backup->MD5Original,readvalue);

	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"Profile",7);
			if (mywstrncasecmp(buffer, h->SectionName, 7)) found = TRUE;
		} else {
	                if (strncasecmp("Profile", h->SectionName, 7) == 0) found = TRUE;
		}
		if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_PROFILES) {
				backup->Profiles[num] = (GSM_Profile *)malloc(sizeof(GSM_Profile));
			        if (backup->Profiles[num] == NULL) {
					goto fail_memory;
				}
				backup->Profiles[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_PROFILES\n");
				goto fail_memory;
			}
			ReadProfileEntry(file_info, h->SectionName, backup->Profiles[num], UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"PhonePBK",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("PhonePBK", h->SectionName, 8) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_PHONEPHONEBOOK) {
				backup->PhonePhonebook[num] = (GSM_MemoryEntry *)malloc(sizeof(GSM_MemoryEntry));
			        if (backup->PhonePhonebook[num] == NULL) {
					goto fail_memory;
				}
				backup->PhonePhonebook[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_PHONEPHONEBOOK\n");
				goto fail_memory;
			}
			backup->PhonePhonebook[num]->Location	= atoi (readvalue);
			backup->PhonePhonebook[num]->MemoryType	= MEM_ME;
			ReadPbkEntry(file_info, h->SectionName, backup->PhonePhonebook[num],UseUnicode);
			dbgprintf(NULL, "number of entries = %i\n",backup->PhonePhonebook[num]->EntriesNum);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"SIMPBK",6);
			if (mywstrncasecmp(buffer, h->SectionName, 6)) found = TRUE;
		} else {
	                if (strncasecmp("SIMPBK", h->SectionName, 6) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_SIMPHONEBOOK) {
				backup->SIMPhonebook[num] = (GSM_MemoryEntry *)malloc(sizeof(GSM_MemoryEntry));
			        if (backup->SIMPhonebook[num] == NULL) {
					goto fail_memory;
				}
				backup->SIMPhonebook[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_SIMPHONEBOOK\n");
				goto fail_memory;
			}
			backup->SIMPhonebook[num]->Location	= atoi (readvalue);
			backup->SIMPhonebook[num]->MemoryType	= MEM_SM;
			ReadPbkEntry(file_info, h->SectionName, backup->SIMPhonebook[num],UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"Calendar",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("Calendar", h->SectionName, 8) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Type", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_MAXCALENDARTODONOTES) {
				backup->Calendar[num] = (GSM_CalendarEntry *)malloc(sizeof(GSM_CalendarEntry));
			        if (backup->Calendar[num] == NULL) {
					goto fail_memory;
				}
				backup->Calendar[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_MAXCALENDARTODONOTES\n");
				goto fail_memory;
			}
			backup->Calendar[num]->Location = num + 1;
			error = ReadCalendarEntry(file_info, h->SectionName, backup->Calendar[num],UseUnicode);
			if (error != ERR_NONE) {
				goto fail_error;
			}
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"Caller",6);
			if (mywstrncasecmp(buffer, h->SectionName, 6)) found = TRUE;
		} else {
	                if (strncasecmp("Caller", h->SectionName, 6) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_CALLER) {
				backup->CallerLogos[num] = (GSM_Bitmap *)malloc(sizeof(GSM_Bitmap));
			        if (backup->CallerLogos[num] == NULL) {
					goto fail_memory;
				}
				backup->CallerLogos[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_CALLER\n");
				goto fail_memory;
			}
			backup->CallerLogos[num]->Location = atoi (readvalue);
			ReadCallerEntry(file_info, h->SectionName, backup->CallerLogos[num],UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"SMSC",4);
			if (mywstrncasecmp(buffer, h->SectionName, 4)) found = TRUE;
		} else {
	                if (strncasecmp("SMSC", h->SectionName, 4) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_SMSC) {
				backup->SMSC[num] = (GSM_SMSC *)malloc(sizeof(GSM_SMSC));
			        if (backup->SMSC[num] == NULL) {
					goto fail_memory;
				}
				backup->SMSC[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_SMSC\n");
				goto fail_memory;
			}
			backup->SMSC[num]->Location = atoi (readvalue);
			ReadSMSCEntry(file_info, h->SectionName, backup->SMSC[num],UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"WAPBookmark",11);
			if (mywstrncasecmp(buffer, h->SectionName, 11)) found = TRUE;
			if (!found) {
				EncodeUnicode(buffer,"Bookmark",8);
				if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
			}
		} else {
	                if (strncasecmp("WAPBookmark", h->SectionName, 11) == 0) found = TRUE;
			if (!found) {
				if (strncasecmp("Bookmark", h->SectionName, 8) == 0) found = TRUE;
			}
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "URL", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_WAPBOOKMARK) {
				backup->WAPBookmark[num] = (GSM_WAPBookmark *)malloc(sizeof(GSM_WAPBookmark));
			        if (backup->WAPBookmark[num] == NULL) {
					goto fail_memory;
				}
				backup->WAPBookmark[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_WAPBOOKMARK\n");
				goto fail_memory;
			}
			backup->WAPBookmark[num]->Location = num + 1;
			ReadWAPBookmarkEntry(file_info, h->SectionName, backup->WAPBookmark[num],UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"WAPSettings",11);
			if (mywstrncasecmp(buffer, h->SectionName, 11)) found = TRUE;
			if (!found) {
				EncodeUnicode(buffer,"Settings",8);
				if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
			}
		} else {
	                if (strncasecmp("WAPSettings", h->SectionName, 11) == 0) found = TRUE;
			if (!found) {
		                if (strncasecmp("Settings", h->SectionName, 8) == 0) found = TRUE;
			}
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Title00", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_WAPSETTINGS) {
				backup->WAPSettings[num] = (GSM_MultiWAPSettings *)malloc(sizeof(GSM_MultiWAPSettings));
			        if (backup->WAPSettings[num] == NULL) {
					goto fail_memory;
				}
				backup->WAPSettings[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_WAPSETTINGS\n");
				goto fail_memory;
			}
			backup->WAPSettings[num]->Location = num + 1;
			dbgprintf(NULL, "reading wap settings\n");
			ReadWAPSettingsEntry(file_info, h->SectionName, backup->WAPSettings[num],UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"MMSSettings",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("MMSSettings", h->SectionName, 8) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Title00", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_MMSSETTINGS) {
				backup->MMSSettings[num] = (GSM_MultiWAPSettings *)malloc(sizeof(GSM_MultiWAPSettings));
			        if (backup->MMSSettings[num] == NULL) {
					goto fail_memory;
				}
				backup->MMSSettings[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_MMSSETTINGS\n");
				goto fail_memory;
			}
			backup->MMSSettings[num]->Location = num + 1;
			dbgprintf(NULL, "reading mms settings\n");
			ReadWAPSettingsEntry(file_info, h->SectionName, backup->MMSSettings[num],UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"Ringtone",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("Ringtone", h->SectionName, 8) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_RINGTONES) {
				backup->Ringtone[num] = (GSM_Ringtone *)malloc(sizeof(GSM_Ringtone));
			        if (backup->Ringtone[num] == NULL) {
					goto fail_memory;
				}
				backup->Ringtone[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_RINGTONES\n");
				goto fail_memory;
			}
			ReadRingtoneEntry(file_info, h->SectionName, backup->Ringtone[num],UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"TODO",4);
			if (mywstrncasecmp(buffer, h->SectionName, 4)) found = TRUE;
		} else {
	                if (strncasecmp("TODO", h->SectionName, 4) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_MAXCALENDARTODONOTES) {
				backup->ToDo[num] = (GSM_ToDoEntry *)malloc(sizeof(GSM_ToDoEntry));
			        if (backup->ToDo[num] == NULL) {
					goto fail_memory;
				}
				backup->ToDo[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_MAXCALENDARTODONOTES\n");
				goto fail_memory;
			}
			backup->ToDo[num]->Location = num + 1;
			error = ReadToDoEntry(file_info, h->SectionName, backup->ToDo[num],UseUnicode);
			if (error != ERR_NONE) {
				goto fail_error;
			}
			num++;
                }
        }
	sprintf(buffer,"Startup");
	readvalue = ReadCFGText(file_info, buffer, "Text", UseUnicode);
	if (readvalue==NULL) {
		readvalue = ReadCFGText(file_info, buffer, "Width", UseUnicode);
	}
	if (readvalue!=NULL) {
		backup->StartupLogo = (GSM_Bitmap *)malloc(sizeof(GSM_Bitmap));
	        if (backup->StartupLogo == NULL) {
			goto fail_memory;
		}
		ReadStartupEntry(file_info, buffer, backup->StartupLogo,UseUnicode);
	}
	sprintf(buffer,"Operator");
	readvalue = ReadCFGText(file_info, buffer, "Network", UseUnicode);
	if (readvalue!=NULL) {
		backup->OperatorLogo = (GSM_Bitmap *)malloc(sizeof(GSM_Bitmap));
	        if (backup->OperatorLogo == NULL) {
			goto fail_memory;
		}
		ReadOperatorEntry(file_info, buffer, backup->OperatorLogo,UseUnicode);
	}
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"FMStation",9);
			if (mywstrncasecmp(buffer, h->SectionName, 9)) found = TRUE;
		} else {
	                if (strncasecmp("FMStation", h->SectionName, 9) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_FMSTATIONS) {
				backup->FMStation[num] = (GSM_FMStation *)malloc(sizeof(GSM_FMStation));
			        if (backup->FMStation[num] == NULL) {
					goto fail_memory;
				}
				backup->FMStation[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_FMSTATIONS\n");
				goto fail_memory;
			}
			backup->FMStation[num]->Location = num + 1;
			error = ReadFMStationEntry(file_info, h->SectionName, backup->FMStation[num],UseUnicode);
			if (error != ERR_NONE) {
				goto fail_error;
			}
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"GPRSPoint",9);
			if (mywstrncasecmp(buffer, h->SectionName, 9)) found = TRUE;
		} else {
	                if (strncasecmp("GPRSPoint", h->SectionName, 9) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Location", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_GPRSPOINT) {
				backup->GPRSPoint[num] = (GSM_GPRSAccessPoint *)malloc(sizeof(GSM_GPRSAccessPoint));
			        if (backup->GPRSPoint[num] == NULL) {
					goto fail_memory;
				}
				backup->GPRSPoint[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_GPRSPOINT\n");
				goto fail_memory;
			}
			backup->GPRSPoint[num]->Location = num + 1;
			ReadGPRSPointEntry(file_info, h->SectionName, backup->GPRSPoint[num],UseUnicode);
			num++;
                }
        }
	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"Note",4);
			if (mywstrncasecmp(buffer, h->SectionName, 4)) found = TRUE;
		} else {
	                if (strncasecmp("Note", h->SectionName, 4) == 0) found = TRUE;
		}
                if (found) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Text", UseUnicode);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_NOTE) {
				backup->Note[num] = (GSM_NoteEntry *)malloc(sizeof(GSM_NoteEntry));
			        if (backup->Note[num] == NULL) {
					goto fail_memory;
				}
				backup->Note[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_NOTE\n");
				goto fail_memory;
			}
			ReadNoteEntry(file_info, h->SectionName, backup->Note[num],UseUnicode);
			num++;
                }
        }
	if (backup->MD5Original[0]!=0) {
		FindBackupChecksum(FileName, UseUnicode, backup->MD5Calculated);
	}
        for (h = file_info; h != NULL; h = h->Next) {
		found = FALSE;
		if (UseUnicode) {
			EncodeUnicode(buffer,"Backup",6);
			if (mywstrncasecmp(buffer, h->SectionName, 6)) found = TRUE;
		} else {
	                if (strncasecmp("Backup", h->SectionName, 6) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"Checksum",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("Checksum", h->SectionName, 8) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"Profile",7);
			if (mywstrncasecmp(buffer, h->SectionName, 7)) found = TRUE;
		} else {
	                if (strncasecmp("Profile", h->SectionName, 7) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"PhonePBK",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("PhonePBK", h->SectionName, 8) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"SIMPBK",6);
			if (mywstrncasecmp(buffer, h->SectionName, 6)) found = TRUE;
		} else {
	                if (strncasecmp("SIMPBK", h->SectionName, 6) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"Calendar",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("Calendar", h->SectionName, 8) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"Caller",6);
			if (mywstrncasecmp(buffer, h->SectionName, 6)) found = TRUE;
		} else {
	                if (strncasecmp("Caller", h->SectionName, 6) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"SMSC",4);
			if (mywstrncasecmp(buffer, h->SectionName, 4)) found = TRUE;
		} else {
	                if (strncasecmp("SMSC", h->SectionName, 4) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"WAPBookmark",11);
			if (mywstrncasecmp(buffer, h->SectionName, 11)) found = TRUE;
			if (!found) {
				EncodeUnicode(buffer,"Bookmark",8);
				if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
			}
		} else {
	                if (strncasecmp("WAPBookmark", h->SectionName, 11) == 0) found = TRUE;
			if (!found) {
				if (strncasecmp("Bookmark", h->SectionName, 8) == 0) found = TRUE;
			}
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"WAPSettings",11);
			if (mywstrncasecmp(buffer, h->SectionName, 11)) found = TRUE;
			if (!found) {
				EncodeUnicode(buffer,"Settings",8);
				if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
			}
		} else {
	                if (strncasecmp("WAPSettings", h->SectionName, 11) == 0) found = TRUE;
			if (!found) {
		                if (strncasecmp("Settings", h->SectionName, 8) == 0) found = TRUE;
			}
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"MMSSettings",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("MMSSettings", h->SectionName, 8) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"Ringtone",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("Ringtone", h->SectionName, 8) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"TODO",4);
			if (mywstrncasecmp(buffer, h->SectionName, 4)) found = TRUE;
		} else {
	                if (strncasecmp("TODO", h->SectionName, 4) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"Startup",7);
			if (mywstrncasecmp(buffer, h->SectionName, 7)) found = TRUE;
		} else {
	                if (strncasecmp("Startup", h->SectionName, 7) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"Operator",8);
			if (mywstrncasecmp(buffer, h->SectionName, 8)) found = TRUE;
		} else {
	                if (strncasecmp("Operator", h->SectionName, 8) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"FMStation",9);
			if (mywstrncasecmp(buffer, h->SectionName, 9)) found = TRUE;
		} else {
	                if (strncasecmp("FMStation", h->SectionName, 9) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"GPRSPoint",9);
			if (mywstrncasecmp(buffer, h->SectionName, 9)) found = TRUE;
		} else {
	                if (strncasecmp("GPRSPoint", h->SectionName, 9) == 0) found = TRUE;
		}
		if (UseUnicode) {
			EncodeUnicode(buffer,"Note",4);
			if (mywstrncasecmp(buffer, h->SectionName, 4)) found = TRUE;
		} else {
	                if (strncasecmp("Note", h->SectionName, 4) == 0) found = TRUE;
		}
		if (!found) {
			goto fail_memory;
		}
        }
	INI_Free(file_info);
	return ERR_NONE;
fail_memory:
	error = ERR_MOREMEMORY;
fail_error:
	INI_Free(file_info);
	return error;
}

/* ---------------------- backup files for SMS ----------------------------- */

static GSM_Error ReadSMSBackupEntry(INI_Section *file_info, char *section, GSM_SMSMessage *SMS)
{
	unsigned char *readvalue=NULL, *readbuffer=NULL;

	GSM_SetDefaultSMSData(SMS);

	SMS->PDU = SMS_Submit;
	SMS->SMSC.Location = 0;
	ReadBackupText(file_info, section, "SMSC", SMS->SMSC.Number, FALSE);
	SMS->ReplyViaSameSMSC = INI_GetBool(file_info, section, "ReplySMSC", FALSE);
	SMS->Class = INI_GetInt(file_info, section, "Class", -1);
	readvalue = ReadCFGText(file_info, section, "Sent", FALSE);
	if (readvalue != NULL && ReadVCALDateTime(readvalue, &SMS->DateTime)) {
		SMS->PDU = SMS_Deliver;
	}
	readvalue = ReadCFGText(file_info, section, "PDU", FALSE);
	if (readvalue != NULL) {
		if (strcmp(readvalue, "Deliver") == 0) {
			SMS->PDU = SMS_Deliver;
		} else if (strcmp(readvalue, "Submit" ) == 0) {
			SMS->PDU = SMS_Submit;
		} else if (strcmp(readvalue, "Status_Report" ) == 0) {
			SMS->PDU = SMS_Status_Report;
		}
	}
	readvalue = ReadCFGText(file_info, section, "DateTime", FALSE);
	if (readvalue != NULL) {
		ReadVCALDateTime(readvalue, &SMS->DateTime);
	}
	SMS->RejectDuplicates = INI_GetBool(file_info, section, "RejectDuplicates", FALSE);
	SMS->ReplaceMessage = INI_GetInt(file_info, section, "ReplaceMessage", 0);
	SMS->MessageReference = INI_GetInt(file_info, section, "MessageReference", 0);
	SMS->State = SMS_UnRead;
	readvalue = ReadCFGText(file_info, section, "State", FALSE);
	if (readvalue!=NULL) {
		if (strcasecmp(readvalue,"Read") == 0)		SMS->State = SMS_Read;
		else if (strcasecmp(readvalue,"Sent") == 0)	SMS->State = SMS_Sent;
		else if (strcasecmp(readvalue,"UnSent") == 0)	SMS->State = SMS_UnSent;
	}
	ReadBackupText(file_info, section, "Number", SMS->Number, FALSE);
	ReadBackupText(file_info, section, "Name", SMS->Name, FALSE);
	SMS->Length = INI_GetInt(file_info, section, "Length", 0);
	SMS->Coding  = SMS_Coding_8bit;
	readvalue = ReadCFGText(file_info, section, "Coding", FALSE);
	if (readvalue!=NULL) {
		SMS->Coding = GSM_StringToSMSCoding(readvalue);
		if (SMS->Coding == 0) {
			SMS->Coding  = SMS_Coding_8bit;
		}
	}
	readbuffer = ReadLinkedBackupText(file_info, section, "Text", FALSE);
	if (readbuffer == NULL) {
		dbgprintf(NULL, "No text found, assuming empty!\n");
		SMS->Length = 0;
		SMS->Text[0] = 0;
		SMS->Text[1] = 0;
	} else {
		dbgprintf(NULL, "Linked text: %s\n", readbuffer);
		/* This is hex encoded unicode, need to multiply by 4 */
		if (strlen(readbuffer) > 4 * GSM_MAX_SMS_CHARS_LENGTH) {
			dbgprintf(NULL, "Message text too long, truncating!\n");
			readbuffer[4 * GSM_MAX_SMS_CHARS_LENGTH] = 0;
		}
		if (!DecodeHexBin (SMS->Text, readbuffer, strlen(readbuffer))) {
			dbgprintf(NULL, "Failed decoding binary field!\n");
		}
		/*
		 * For 8-bit messages we store number of bytes,
		 * otherwise length of text which should be nul terminated.
		 */
		if (SMS->Coding == SMS_Coding_8bit) {
			SMS->Length = strlen(readbuffer)/2;
		} else {
			SMS->Length = strlen(readbuffer)/4;
			SMS->Text[(SMS->Length * 2)]	= 0;
			SMS->Text[(SMS->Length * 2) + 1] 	= 0;
		}
	}
	free(readbuffer);
	readbuffer=NULL;
	SMS->Folder = INI_GetInt(file_info, section, "Folder", SMS->Folder);
	SMS->UDH.Type		= UDH_NoUDH;
	SMS->UDH.Length 	= 0;
	SMS->UDH.ID8bit	  	= -1;
	SMS->UDH.ID16bit	= -1;
	SMS->UDH.PartNumber	= -1;
	SMS->UDH.AllParts	= -1;
	readvalue = ReadCFGText(file_info, section, "UDH", FALSE);
	if (readvalue!=NULL) {
		DecodeHexBin (SMS->UDH.Text, readvalue, strlen(readvalue));
		SMS->UDH.Length = strlen(readvalue)/2;
		GSM_DecodeUDHHeader(NULL, &SMS->UDH);
	}
	return ERR_NONE;
}

static GSM_Error GSM_ReadSMSBackupTextFile(const char *FileName, GSM_SMS_Backup *backup)
{
	INI_Section	*file_info, *h;
	char		*readvalue=NULL;
	int		num=0;
	GSM_Error	error;

	backup->SMS[0] = NULL;

	error = INI_ReadFile(FileName, FALSE, &file_info);
	if (error != ERR_NONE) {
		return error;
	}

	num = 0;
        for (h = file_info; h != NULL; h = h->Next) {
                if (strncasecmp("SMSBackup", h->SectionName, 9) == 0) {
			readvalue = ReadCFGText(file_info, h->SectionName, "Number", FALSE);
			if (readvalue==NULL) break;
			if (num < GSM_BACKUP_MAX_SMS) {
				backup->SMS[num] = (GSM_SMSMessage *)malloc(sizeof(GSM_SMSMessage));
			        if (backup->SMS[num] == NULL) return ERR_MOREMEMORY;
				backup->SMS[num + 1] = NULL;
			} else {
				dbgprintf(NULL, "Increase GSM_BACKUP_MAX_SMS\n");
				return ERR_MOREMEMORY;
			}
			backup->SMS[num]->Location = num + 1;
			error = ReadSMSBackupEntry(file_info, h->SectionName, backup->SMS[num]);
			if (error != ERR_NONE) {
				goto done;
			}
			num++;
		}
        }
	error = ERR_NONE;
done:
	INI_Free(file_info);
	return error;
}

GSM_Error GSM_ReadSMSBackupFile(const char *FileName, GSM_SMS_Backup *backup)
{
	FILE *file;

	GSM_ClearSMSBackup(backup);

	file = fopen(FileName, "rb");
	if (file ==  NULL) return(ERR_CANTOPENFILE);

	fclose(file);

	return GSM_ReadSMSBackupTextFile(FileName, backup);

}

/**
 * Saves text as a comment split to text lines (max 78 chars long).
 */
GSM_Error SaveTextComment(FILE *file, unsigned char *comment)
{
	char buffer[10000]={0};
	size_t i=0, len=0, pos = 0;

	sprintf(buffer, "%s", DecodeUnicodeString(comment));

	fprintf(file, "; ");

	len = strlen(buffer);

	for (i = 0; i < len; i++) {
		if (buffer[i] == 10 || buffer[i] == 13) {
			fprintf(file,"\n; ");
			pos = 0;
		} else {
			if (pos > 75) {
				fprintf(file,"\n; ");
				pos = 0;
			}
			fprintf(file, "%c", buffer[i]);
			pos++;
		}
	}
	fprintf(file,"\n");
	return ERR_NONE;
}

static GSM_Error SaveSMSBackupTextFile(FILE *file, GSM_SMS_Backup *backup)
{
	int 		i=0;
	unsigned char 	buffer[10000]={0};
	const char *s;
	GSM_DateTime	DT;
	GSM_Error error;

	fprintf(file, BACKUP_MAIN_HEADER "\n");
	fprintf(file, BACKUP_INFO_HEADER "\n");
	GSM_GetCurrentDateTime (&DT);
	fprintf(file,"; Saved ");
	fprintf(file, "%04d%02d%02dT%02d%02d%02d",
			DT.Year, DT.Month, DT.Day,
			DT.Hour, DT.Minute, DT.Second);
	fprintf(file," (%s)\n\n",OSDateTime(DT,FALSE));

	i=0;
	while (backup->SMS[i]!=NULL) {
		fprintf(file,"[SMSBackup%03i]\n",i);
		switch (backup->SMS[i]->Coding) {
			case SMS_Coding_Unicode_No_Compression:
			case SMS_Coding_Default_No_Compression:
				error = SaveTextComment(file, backup->SMS[i]->Text);
				if (error != ERR_NONE) return error;
				break;
			default:
				break;
		}
		if (backup->SMS[i]->PDU == SMS_Deliver) {
			error = SaveBackupText(file, "SMSC", backup->SMS[i]->SMSC.Number, FALSE);
			if (error != ERR_NONE) return error;
			if (backup->SMS[i]->ReplyViaSameSMSC) {
				fprintf(file,"SMSCReply = TRUE\n");
			}
			fprintf(file,"PDU = Deliver\n");
		} else if (backup->SMS[i]->PDU == SMS_Submit) {
			fprintf(file,"PDU = Submit\n");
		} else if (backup->SMS[i]->PDU == SMS_Status_Report) {
			fprintf(file,"PDU = Status_Report\n");
		}
		if (backup->SMS[i]->DateTime.Year != 0) {
			fprintf(file,"DateTime");
			error = SaveVCalDateTime(file,&backup->SMS[i]->DateTime, FALSE);
			if (error != ERR_NONE) return error;
		}
		fprintf(file,"State = ");
		switch (backup->SMS[i]->State) {
			case SMS_UnRead	: fprintf(file,"UnRead\n");	break;
			case SMS_Read	: fprintf(file,"Read\n");	break;
			case SMS_Sent	: fprintf(file,"Sent\n");	break;
			case SMS_UnSent	: fprintf(file,"UnSent\n");	break;
		}
		error = SaveBackupText(file, "Number", backup->SMS[i]->Number, FALSE);
		if (error != ERR_NONE) return error;
		error = SaveBackupText(file, "Name", backup->SMS[i]->Name, FALSE);
		if (error != ERR_NONE) return error;
		if (backup->SMS[i]->UDH.Type != UDH_NoUDH) {
			EncodeHexBin(buffer,backup->SMS[i]->UDH.Text,backup->SMS[i]->UDH.Length);
			fprintf(file,"UDH = %s\n",buffer);
		}
		switch (backup->SMS[i]->Coding) {
			case SMS_Coding_Unicode_No_Compression:
			case SMS_Coding_Default_No_Compression:
				EncodeHexBin(buffer,backup->SMS[i]->Text,backup->SMS[i]->Length*2);
				break;
			default:
				EncodeHexBin(buffer,backup->SMS[i]->Text,backup->SMS[i]->Length);
				break;
		}
		SaveLinkedBackupText(file, "Text", buffer, FALSE);
		s = GSM_SMSCodingToString(backup->SMS[i]->Coding);
		fprintf(file, "Coding = %s\n", s);
		fprintf(file,"Folder = %i\n",backup->SMS[i]->Folder);
		fprintf(file,"Length = %i\n",backup->SMS[i]->Length);
		fprintf(file,"Class = %i\n",backup->SMS[i]->Class);
		fprintf(file,"ReplySMSC = ");
		if (backup->SMS[i]->ReplyViaSameSMSC) fprintf(file,"True\n"); else fprintf(file,"False\n");
		fprintf(file,"RejectDuplicates = ");
		if (backup->SMS[i]->RejectDuplicates) fprintf(file,"True\n"); else fprintf(file,"False\n");
		fprintf(file,"ReplaceMessage = %i\n",backup->SMS[i]->ReplaceMessage);
		fprintf(file,"MessageReference = %i\n",backup->SMS[i]->MessageReference);
		fprintf(file,"\n");
		i++;
	}
	return ERR_NONE;
}

GSM_Error GSM_AddSMSBackupFile(const char *FileName, GSM_SMS_Backup *backup)
{
	FILE *file;

	file = fopen(FileName, "ab");

	if (file == NULL) return(ERR_CANTOPENFILE);

	SaveSMSBackupTextFile(file,backup);

	fclose(file);

	return ERR_NONE;
}

void GSM_ClearSMSBackup(GSM_SMS_Backup *backup)
{
	int i=0;

	for (i = 0; i <= GSM_BACKUP_MAX_SMS; i++) {
		backup->SMS[i] = NULL;
	}
}

void GSM_FreeSMSBackup(GSM_SMS_Backup *backup)
{
	int i=0;

	for (i = 0; i <= GSM_BACKUP_MAX_SMS; i++) {
		if (backup->SMS[i] == NULL) break;
		free(backup->SMS[i]);
		backup->SMS[i] = NULL;
	}
}
#endif

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
