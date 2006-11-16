/* (c) 2002-2005 by Marcin Wiacek and Michal Cihar */

#include "../../gsmstate.h"

#ifdef GSM_ENABLE_ATGEN

#include <string.h>
#include <time.h>
#include <ctype.h>

#include "../../gsmcomon.h"
#include "../../misc/coding/coding.h"
#include "../../service/sms/gsmsms.h"
#include "../pfunc.h"

#include "atgen.h"
#include "atgen-functions.h"

#include "samsung.h"
#include "siemens.h"

#ifdef GSM_ENABLE_ALCATEL
GSM_Error ALCATEL_ProtocolVersionReply (GSM_Protocol_Message, GSM_StateMachine *);
#endif

#ifdef GSM_ENABLE_SONYERICSSON
#include "../sonyericsson/sonyericsson-functions.h"
#endif


typedef struct {
	GSM_AT_Charset	charset;
	char		*text;
	bool		unicode;
} GSM_AT_Charset_Info;

/**
 * List of charsets and text identifying them in phone responses, order
 * defines their preferences, so if first is found it is used.
 */
static GSM_AT_Charset_Info AT_Charsets[] = {
	{AT_CHARSET_HEX,	"HEX",		false},
	{AT_CHARSET_GSM,	"GSM",		false},
	{AT_CHARSET_PCCP437,	"PCCP437",	false},
	{AT_CHARSET_UTF8,	"UTF-8",	true},
	{AT_CHARSET_UCS2,	"UCS2",		true},
	{AT_CHARSET_IRA,	"IRA",		false},
	{0,			NULL}
};

typedef struct {
	int     Number;
	char    Text[60];
} ATErrorCode;

static ATErrorCode CMSErrorCodes[] = {
	/*
	 * Error codes not specified here were either undefined or reserved in my
	 * copy of specifications, if you have newer one, please fill in the gaps.
	 */
	/* 0...127 from GSM 04.11 Annex E-2 */
	{1,    "Unassigned (unallocated) number"},
	{8,    "Operator determined barring"},
	{10,   "Call barred"},
	{21,   "Short message transfer rejected"},
	{27,   "Destination out of service"},
	{28,   "Unidentified subscriber"},
	{29,   "Facility rejected"},
	{30,   "Unknown subscriber"},
	{38,   "Network out of order"},
	{41,   "Temporary failure"},
	{42,   "Congestion"},
	{47,   "Resources unavailable, unspecified"},
	{50,   "Requested facility not subscribed"},
	{69,   "Requested facility not implemented"},
	{81,   "Invalid short message transfer reference value"},
	{95,   "Invalid message, unspecified"},
	{96,   "Invalid mandatory information"},
	{97,   "Message type non-existent or not implemented"},
	{98,   "Message not compatible with short message protocol state"},
	{99,   "Information element non-existent or not implemented"},
	{111,  "Protocol error, unspecified"},
	{127,  "Interworking, unspecified"},
	/* 128...255 from GSM 03.40 subclause 9.2.3.22 */
	{0x80, "Telematic interworking not supported"},
	{0x81, "Short message Type 0 not supported"},
	{0x82, "Cannot replace short message"},
	{0x8F, "Unspecified TP-PID error"},
	{0x90, "Data coding scheme (alphabet) not supported"},
	{0x91, "Message class not supported"},
	{0x9F, "Unspecified TP-DCS error"},
	{0xA0, "Command cannot be actioned"},
	{0xA1, "Command unsupported"},
	{0xAF, "Unspecified TP-Command error"},
	{0xB0, "TPDU not supported"},
	{0xC0, "SC busy"},
	{0xC1, "No SC subscription"},
	{0xC2, "SC system failure"},
	{0xC3, "Invalid SME address"},
	{0xC4, "Destination SME barred"},
	{0xC5, "SM Rejected-Duplicate SM"},
	{0xC6, "TP-VPF not supported"},
	{0xC7, "TP-VP not supported"},
	{0xD0, "SIM SMS storage full"},
	{0xD1, "No SMS storage capability in SIM"},
	{0xD2, "Error in MS"},
	{0xD3, "Memory Capacity Exceede"},
	{0xD4, "SIM Application Toolkit Busy"},
	{0xFF, "Unspecified error cause"},
	/* 300...511 from GSM 07.05 subclause 3.2.5 */
	{300,  "ME failure"},
	{301,  "SMS service of ME reserved"},
	{302,  "operation not allowed"},
	{303,  "operation not supported"},
	{304,  "invalid PDU mode parameter"},
	{305,  "invalid text mode parameter"},
	{310,  "SIM not inserted"},
	{311,  "SIM PIN required"},
	{312,  "PH-SIM PIN required"},
	{313,  "SIM failure"},
	{314,  "SIM busy"},
	{315,  "SIM wrong"},
	{316,  "SIM PUK required"},
	{317,  "SIM PIN2 required"},
	{318,  "SIM PUK2 required"},
	{320,  "memory failure"},
	{321,  "invalid memory index"},
	{322,  "memory full"},
	{330,  "SMSC address unknown"},
	{331,  "no network service"},
	{332,  "network timeout"},
	{340,  "no CNMA acknowledgement expected"},
	{500,  "unknown error"},
	/* > 512 are manufacturer specific according to GSM 07.05 subclause 3.2.5 */
	{-1,   ""}
};

static ATErrorCode CMEErrorCodes[] = {
	/* CME Error codes from GSM 07.07 section 9.2 */
	{0,   "phone failure"},
	{1,   "no connection to phone"},
	{2,   "phone-adaptor link reserved"},
	{3,   "operation not allowed"},
	{4,   "operation not supported"},
	{5,   "PH-SIM PIN required"},
	{10,  "SIM not inserted"},
	{11,  "SIM PIN required"},
	{12,  "SIM PUK required"},
	{13,  "SIM failure"},
	{14,  "SIM busy"},
	{15,  "SIM wrong"},
	{16,  "incorrect password"},
	{17,  "SIM PIN2 required"},
	{18,  "SIM PUK2 required"},
	{20,  "memory full"},
	{21,  "invalid index"},
	{22,  "not found"},
	{23,  "memory failure"},
	{24,  "text string too long"},
	{25,  "invalid characters in text string"},
	{26,  "dial string too long"},
	{27,  "invalid characters in dial string"},
	{30,  "no network service"},
	{31,  "network timeout"},
	{100, "unknown"},
};

static char samsung_location_error[] = "[Samsung] Empty location";


GSM_Error ATGEN_HandleCMEError(GSM_StateMachine *s)
{
	GSM_Phone_ATGENData *Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->ErrorCode == 0) {
		smprintf(s, "CME Error occured, but it's type not detected\n");
	} else if (Priv->ErrorText == NULL) {
		smprintf(s, "CME Error %i, no description available\n", Priv->ErrorCode);
	} else {
		smprintf(s, "CME Error %i: \"%s\"\n", Priv->ErrorCode, Priv->ErrorText);
	}
	/* For error codes descriptions see table a bit above */
	switch (Priv->ErrorCode) {
		case -1:
			return ERR_EMPTY;
		case 3:
			return ERR_PERMISSION;
		case 4:
			return ERR_NOTSUPPORTED;
		case 5:
		case 11:
		case 12:
		case 16:
		case 17:
		case 18:
			return ERR_SECURITYERROR;
		case 10:
		case 13:
		case 14:
		case 15:
			return ERR_NOSIM;
		case 20:
			return ERR_FULL;
		case 21:
			return ERR_INVALIDLOCATION;
		case 22:
			return ERR_EMPTY;
		case 23:
			return ERR_MEMORY;
		case 24:
		case 25:
		case 26:
		case 27:
			return ERR_INVALIDDATA;
		default:
			return ERR_UNKNOWN;
	}
}

GSM_Error ATGEN_HandleCMSError(GSM_StateMachine *s)
{
	GSM_Phone_ATGENData *Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->ErrorCode == 0) {
		smprintf(s, "CMS Error occured, but it's type not detected\n");
	} else if (Priv->ErrorText == NULL) {
		smprintf(s, "CMS Error %i, no description available\n", Priv->ErrorCode);
	} else {
		smprintf(s, "CMS Error %i: \"%s\"\n", Priv->ErrorCode, Priv->ErrorText);
	}
	/* For error codes descriptions see table a bit above */
	switch (Priv->ErrorCode) {
	case 304:
            	return ERR_NOTSUPPORTED;
        case 305:
            	return ERR_BUG;
        case 311:
        case 312:
        case 316:
        case 317:
        case 318:
            	return ERR_SECURITYERROR;
	case 313:
	case 314:
	case 315:
		return ERR_NOSIM;
        case 322:
            	return ERR_FULL;
        case 321:
            	return ERR_INVALIDLOCATION;
        default:
		return ERR_UNKNOWN;
	}
}

int ATGEN_ExtractOneParameter(unsigned char *input, unsigned char *output)
{
	int	position=0;
	bool	inside_quotes = false;

	while ((*input!=',' || inside_quotes) && *input!=0x0d && *input!=0x00) {
		if (*input == '"') inside_quotes = ! inside_quotes;
		*output=*input;
		input	++;
		output	++;
		position++;
	}
	*output=0;
	position++;
	return position;
}

void ATGEN_TweakInternationalNumber(unsigned char *Number, unsigned char *numType)
{/*	Author: Peter Ondraska
	License: Whatever the current maintainer of gammulib chooses, as long as there
	is an easy way to obtain the source under GPL, otherwise the author's parts
	of this function are GPL 2.0. */

	/* Checks if International number needs to be corrected */
	char* pos; /* current position in the buffer */
	char buf[500]; /* Taken from DecodeUnicodeString(). How to get length of the encoded string? There may be embedded 0s. */

	if (atoi(numType)==NUMBER_INTERNATIONAL_NUMBERING_PLAN_ISDN) {
		sprintf(buf+1,"%s",DecodeUnicodeString(Number)); /* leave 1 free char before the number, we'll need it */
		/* International number may be without + (e.g. (Sony)Ericsson)
			we can add it, but must handle numbers in the form:
			         NNNNNN         N - any digit (char)
			   *code#NNNNNN         any number of Ns
			*[*]code*NNNNNN[...]
			other combinations (like *code1*code2*number#)
			will have to be added if found in real life
			Or does somebody know the exact allowed syntax
			from some standard?
		*/
		pos=buf+1;
		if (*pos=='*') { /* Code? Skip it. */
			/* probably with code */
			while (*pos=='*') { /* skip leading asterisks */
				*(pos-1)=*pos; /* shift the chars by one */
				pos++;
			}
			while ((*pos!='*')&&(*pos!='#')) { /* skip code - anything except * or # */
				*(pos-1)=*pos;
				pos++;
			}
			*(pos-1)=*pos; /* shift the last delimiter */
			pos++;
	        }
		/* check the guessed location, if + is correctly there */
		if (*pos=='+') {
			/* yes, just shift the rest of the string */
			while (*pos) {
				*(pos-1) = *pos;
				pos++;
			}
			*(pos-1)=0; /* kill the last char, which now got doubled */
		} else {
			/* no, insert + and exit, no more shifting */
			*(pos-1)='+';
		}
		EncodeUnicode(Number,buf,strlen(buf));
	}
}

GSM_Error ATGEN_DecodeDateTime(GSM_StateMachine *s, GSM_DateTime *dt, unsigned char *input)
{
       	/* This function parses datetime strings in the format:
	[YY[YY]/MM/DD,]hh:mm[:ss[+TZ]] , [] enclosed parts are optional */
	/* (or the same hex/unicode encoded) */
	GSM_Phone_ATGENData 	*Priv 	= &s->Phone.Data.Priv.ATGEN;
	unsigned char		buffer[100];
	unsigned char		*pos;
	unsigned char		buffer2[100];
	int			len;

	pos = input;

	/* Strip possible quotes */
	if (*pos == '"') pos++;
	if (buffer[strlen(pos) - 1] == '"') buffer[strlen(pos) - 1] = 0;

	len = strlen(pos);

	if (Priv->Charset == AT_CHARSET_HEX && (len > 10) && (len % 2 == 0) && (strchr(pos, '/') == NULL)) {
		/* This is probably hex encoded number */
		DecodeHexBin(buffer, input, len);
	} else if (Priv->Charset == AT_CHARSET_UCS2 && (len > 20) && (len % 4 == 0) && (strchr(pos, '/') == NULL)) {
		/* This is probably unicode encoded number */
		DecodeHexUnicode(buffer2, pos, len);
		DecodeUnicode(buffer2, buffer);
	} else  {
		strcpy(buffer, pos);
	}

	pos = buffer;

	/* Strip possible quotes again */
	if (*pos == '"') pos++;
	if (buffer[strlen(pos) - 1] == '"') buffer[strlen(pos) - 1] = 0;
	/* some phones report only time (HH:MM) in the alarm */
	if (strchr(pos, '/')) {
		/* date present */
		/* Samsung phones report year as %d instead of %02d */
		dt->Year=atoi(pos);
		if(dt->Year>80 && dt->Year<1000) {
			dt->Year+=1900;
		} else {
			dt->Year+=2000;
		}
		pos = strchr(pos, '/');
		pos++;
		dt->Month = atoi(pos);
		pos = strchr(pos, '/');
		if (pos == NULL) return ERR_UNKNOWN;
		pos++;
		dt->Day = atoi(pos);
		pos = strchr(pos, ',');
		if (pos == NULL) return ERR_UNKNOWN;
		pos++;
	} else {
		/* if date was not found, it is still necessary to initialize
		   the variables, maybe Today() would be better in some replies */
		dt->Year=0;
		dt->Month=0;
		dt->Day=0;
	}

	/* time present */
	dt->Hour = atoi(pos);
	pos = strchr(pos, ':');
	if (pos == NULL) return ERR_UNKNOWN;
	pos++;
	dt->Minute = atoi(pos);
	pos = strchr(pos, ':');
	if (pos!=NULL) {
	        /* seconds present */
		pos++;
		dt->Second = atoi(pos);
	} else {
		dt->Second=0;
	}

	if ((pos != NULL) && (*pos == '+' || *pos == '-')) {
		/* timezone present */
		dt->Timezone = (*pos == '+' ? 1 : -1);
		dt->Timezone = dt->Timezone*atoi(pos);
	} else {
		dt->Timezone = 0;
	}
	return ERR_NONE;
}

GSM_Error ATGEN_DispatchMessage(GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv 	= &s->Phone.Data.Priv.ATGEN;
	GSM_Protocol_Message	*msg	= s->Phone.Data.RequestMsg;
	int 			i	= 0, j, k;
	char                    *err, *line;
	ATErrorCode		*ErrorCodes = NULL;

	SplitLines(msg->Buffer, msg->Length, &Priv->Lines, "\x0D\x0A", 2, true);

	/* Find number of lines */
	while (Priv->Lines.numbers[i*2+1] != 0) {
		/* FIXME: handle special chars correctly */
		smprintf(s, "%i \"%s\"\n",i+1,GetLineString(msg->Buffer,Priv->Lines,i+1));
		i++;
	}

	Priv->ReplyState 	= AT_Reply_Unknown;
	Priv->ErrorText     	= NULL;
	Priv->ErrorCode     	= 0;

	line = GetLineString(msg->Buffer,Priv->Lines,i);
	if (!strcmp(line,"OK"))		Priv->ReplyState = AT_Reply_OK;
	if (!strcmp(line,"> "))		Priv->ReplyState = AT_Reply_SMSEdit;
	if (!strcmp(line,"CONNECT"))	Priv->ReplyState = AT_Reply_Connect;
	if (!strcmp(line,"ERROR"  ))	Priv->ReplyState = AT_Reply_Error;
	if (!strncmp(line,"+CME ERROR:",11)) {
		Priv->ReplyState = AT_Reply_CMEError;
		ErrorCodes = CMEErrorCodes;
	}
	if (!strncmp(line,"+CMS ERROR:",11)) {
		Priv->ReplyState = AT_Reply_CMSError;
		ErrorCodes = CMSErrorCodes;
	}

	/* FIXME: Samsung phones can answer +CME ERROR:-1 meaning empty location */
	if (Priv->ReplyState == AT_Reply_CMEError && Priv->Manufacturer == AT_Samsung) {
		err = line + 11;
		Priv->ErrorCode = atoi(err);

		if (Priv->ErrorCode == -1) {
			Priv->ErrorText = samsung_location_error;
			return GSM_DispatchMessage(s);
		}
	}

	if (Priv->ReplyState == AT_Reply_CMEError || Priv->ReplyState == AT_Reply_CMSError) {
	        j = 0;
		/* One char behind +CM[SE] ERROR */
		err = line + 12;
		while (err[j] && !isalnum(err[j])) j++;
		if (isdigit(err[j])) {
			Priv->ErrorCode = atoi(&(err[j]));
			k = 0;
			while (ErrorCodes[k].Number != -1) {
				if (ErrorCodes[k].Number == Priv->ErrorCode) {
					Priv->ErrorText = ErrorCodes[k].Text;
					break;
				}
				k++;
			}
		} else if (isalpha(err[j])) {
			k = 0;
			while (ErrorCodes[k].Number != -1) {
				if (!strncmp(err + j, ErrorCodes[k].Text, strlen(ErrorCodes[k].Text))) {
					Priv->ErrorCode = ErrorCodes[k].Number;
					Priv->ErrorText = ErrorCodes[k].Text;
					break;
				}
				k++;
			}
		}
	}
	return GSM_DispatchMessage(s);
}

GSM_Error ATGEN_GenericReply(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
		case AT_Reply_OK:
		case AT_Reply_Connect:
			return ERR_NONE;
		case AT_Reply_Error:
			return ERR_UNKNOWN;
		case AT_Reply_CMSError:
			return ATGEN_HandleCMSError(s);
		case AT_Reply_CMEError:
			return ATGEN_HandleCMEError(s);
		default:
			break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_ReplyGetUSSD(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	unsigned char 	buffer[2000],buffer2[4000];
	int 		i = 10;

	/* Ugly hack */
	while (msg.Buffer[i]!=13) i++;
	i = i - 6;
	memcpy(buffer,msg.Buffer+10,i-11);
	buffer[i-11] = 0x00;

	smprintf(s, "USSD reply: \"%s\"\n",buffer);

	if (s->Phone.Data.EnableIncomingUSSD && s->User.IncomingUSSD!=NULL) {
		EncodeUnicode(buffer2,buffer,strlen(buffer));
		s->User.IncomingUSSD(s->CurrentConfig->Device, buffer2);
	}

	return ERR_NONE;
}

GSM_Error ATGEN_SetIncomingUSSD(GSM_StateMachine *s, bool enable)
{
	GSM_Error error;

	if (enable) {
		smprintf(s, "Enabling incoming USSD\n");
		error=GSM_WaitFor (s, "AT+CUSD=1\r", 10, 0x00, 3, ID_SetUSSD);
	} else {
		smprintf(s, "Disabling incoming USSD\n");
		error=GSM_WaitFor (s, "AT+CUSD=0\r", 10, 0x00, 3, ID_SetUSSD);
	}
	if (error==ERR_NONE) s->Phone.Data.EnableIncomingUSSD = enable;
	if (error==ERR_UNKNOWN) return ERR_NOTSUPPORTED;
	return error;
}

GSM_Error ATGEN_ReplyGetModel(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Phone_Data		*Data = &s->Phone.Data;

	if (s->Phone.Data.Priv.ATGEN.ReplyState != AT_Reply_OK) return ERR_NOTSUPPORTED;

	if (strlen(GetLineString(msg.Buffer, Priv->Lines, 2)) <= MAX_MODEL_LENGTH) {
		CopyLineString(Data->Model, msg.Buffer, Priv->Lines, 2);

		/* Sometimes phone adds this before manufacturer (Sagem) */
		if (strncmp("+CGMM: ", Data->Model, 7) == 0) {
			memmove(Data->Model, Data->Model + 7, strlen(Data->Model + 7) + 1);
		}

		Data->ModelInfo = GetModelData(NULL,Data->Model,NULL);
		if (Data->ModelInfo->number[0] == 0) Data->ModelInfo = GetModelData(NULL,NULL,Data->Model);
		if (Data->ModelInfo->number[0] == 0) Data->ModelInfo = GetModelData(Data->Model,NULL,NULL);

		if (Data->ModelInfo->number[0] != 0) strcpy(Data->Model,Data->ModelInfo->number);

		if (strstr(msg.Buffer,"Nokia")) 	Priv->Manufacturer = AT_Nokia;
		else if (strstr(msg.Buffer,"M20")) 	Priv->Manufacturer = AT_Siemens;
		else if (strstr(msg.Buffer,"MC35")) 	Priv->Manufacturer = AT_Siemens;
		else if (strstr(msg.Buffer,"TC35")) 	Priv->Manufacturer = AT_Siemens;
		else if (strstr(msg.Buffer, "iPAQ")) 	Priv->Manufacturer = AT_HP;

		if (strstr(msg.Buffer,"M20")) 		strcpy(Data->Model,"M20");
		else if (strstr(msg.Buffer,"MC35")) 	strcpy(Data->Model,"MC35");
		else if (strstr(msg.Buffer,"TC35")) 	strcpy(Data->Model,"TC35");
		else if (strstr(msg.Buffer, "iPAQ")) 	strcpy(Data->Model,"iPAQ");
	} else {
		smprintf(s, "WARNING: Model name too long, increase MAX_MODEL_LENGTH to at least %zd\n", strlen(GetLineString(msg.Buffer, Priv->Lines, 2)));
	}

	return ERR_NONE;
}

GSM_Error ATGEN_GetModel(GSM_StateMachine *s)
{
	GSM_Error error;

	if (s->Phone.Data.Model[0] != 0) return ERR_NONE;

	smprintf(s, "Getting model\n");
	error=GSM_WaitFor (s, "AT+CGMM\r", 8, 0x00, 3, ID_GetModel);
	if (error==ERR_NONE) {
		if (s->di.dl==DL_TEXT || s->di.dl==DL_TEXTALL ||
		    s->di.dl==DL_TEXTDATE || s->di.dl==DL_TEXTALLDATE) {
			smprintf(s, "[Connected model  - \"%s\"]\n",s->Phone.Data.Model);
		}
	}
	return error;
}

GSM_Error ATGEN_ReplyGetManufacturer(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData *Priv = &s->Phone.Data.Priv.ATGEN;

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "Manufacturer info received\n");
		Priv->Manufacturer = AT_Unknown;
		if (strlen(GetLineString(msg.Buffer, Priv->Lines, 2)) <= MAX_MANUFACTURER_LENGTH) {
			CopyLineString(s->Phone.Data.Manufacturer, msg.Buffer, Priv->Lines, 2);
		} else {
			smprintf(s, "WARNING: Manufacturer name too long, increase MAX_MANUFACTURER_LENGTH to at least %zd\n", strlen(GetLineString(msg.Buffer, Priv->Lines, 2)));
			s->Phone.Data.Manufacturer[0] = 0;
		}
		/* Sometimes phone adds this before manufacturer (Sagem) */
		if (strncmp("+CGMI: ", s->Phone.Data.Manufacturer, 7) == 0) {
			memmove(s->Phone.Data.Manufacturer, s->Phone.Data.Manufacturer + 7, strlen(s->Phone.Data.Manufacturer + 7) + 1);
		}
		if (strstr(msg.Buffer,"Falcom")) {
			smprintf(s, "Falcom\n");
			strcpy(s->Phone.Data.Manufacturer,"Falcom");
			Priv->Manufacturer = AT_Falcom;
			if (strstr(msg.Buffer,"A2D")) {
				strcpy(s->Phone.Data.Model,"A2D");
				s->Phone.Data.ModelInfo = GetModelData(NULL,s->Phone.Data.Model,NULL);
				smprintf(s, "Model A2D\n");
			}
		}
		if (strstr(msg.Buffer,"Nokia")) {
			smprintf(s, "Nokia\n");
			strcpy(s->Phone.Data.Manufacturer,"Nokia");
			Priv->Manufacturer = AT_Nokia;
		}
		if (strstr(msg.Buffer,"SIEMENS")) {
			smprintf(s, "Siemens\n");
			strcpy(s->Phone.Data.Manufacturer,"Siemens");
			Priv->Manufacturer = AT_Siemens;
		}
		if (strstr(msg.Buffer,"ERICSSON")) {
			smprintf(s, "Ericsson\n");
			strcpy(s->Phone.Data.Manufacturer,"Ericsson");
			Priv->Manufacturer = AT_Ericsson;
		}
		if (strstr(msg.Buffer,"Sony Ericsson")) {
			smprintf(s, "Sony Ericsson\n");
			strcpy(s->Phone.Data.Manufacturer,"Sony Ericsson");
			Priv->Manufacturer = AT_Ericsson;
		}
		if (strstr(msg.Buffer,"iPAQ")) {
			smprintf(s, "iPAQ\n");
			strcpy(s->Phone.Data.Manufacturer,"HP");
			Priv->Manufacturer = AT_HP;
		}
		if (strstr(msg.Buffer,"ALCATEL")) {
			smprintf(s, "Alcatel\n");
			strcpy(s->Phone.Data.Manufacturer,"Alcatel");
			Priv->Manufacturer = AT_Alcatel;
		}
		if (strstr(msg.Buffer,"SAGEM")) {
			smprintf(s, "Sagem\n");
			strcpy(s->Phone.Data.Manufacturer,"Sagem");
			Priv->Manufacturer = AT_Sagem;
		}
		if (strstr(msg.Buffer,"Samsung")) {
			smprintf(s, "Samsung\n");
			strcpy(s->Phone.Data.Manufacturer,"Samsung");
			Priv->Manufacturer = AT_Samsung;
		}
		if (strstr(msg.Buffer,"Mitsubishi")) {
			smprintf(s, "Mitsubishi\n");
			strcpy(s->Phone.Data.Manufacturer,"Mitsubishi");
			Priv->Manufacturer = AT_Mitsubishi;
		}
		return ERR_NONE;
	case AT_Reply_CMSError:
		return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetManufacturer(GSM_StateMachine *s)
{
	if (s->Phone.Data.Manufacturer[0] != 0) return ERR_NONE;

	return GSM_WaitFor (s, "AT+CGMI\r", 8, 0x00, 4, ID_GetManufacturer);
}

GSM_Error ATGEN_ReplyGetFirmwareCGMR(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	unsigned int		i = 0;

	strcpy(s->Phone.Data.Version,"unknown");
	s->Phone.Data.VerNum = 0;
	if (Priv->ReplyState == AT_Reply_OK) {
		CopyLineString(s->Phone.Data.Version, msg.Buffer, Priv->Lines, 2);
		/* Sometimes phone adds this before manufacturer (Sagem) */
		if (strncmp("+CGMR: ", s->Phone.Data.Version, 7) == 0) {
			memmove(s->Phone.Data.Version, s->Phone.Data.Version + 7, strlen(s->Phone.Data.Version + 7) + 1);
		}
	}
	/* @todo: why the hell this? */
	if (Priv->Manufacturer == AT_Ericsson) {
		while (1) {
			if (s->Phone.Data.Version[i] == 0x20) {
				s->Phone.Data.Version[i] = 0x00;
				break;
			}
			if (i == strlen(s->Phone.Data.Version)) break;
			i++;
		}
	}
	smprintf(s, "Received firmware version: \"%s\"\n",s->Phone.Data.Version);
	GSM_CreateFirmwareNumber(s);
	return ERR_NONE;
}

GSM_Error ATGEN_ReplyGetFirmwareATI(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData *Priv = &s->Phone.Data.Priv.ATGEN;

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
//		strcpy(Data->Version,"0.00");
//		*Data->VersionNum=0;
//		if (Data->Priv.ATGEN.ReplyState==AT_Reply_OK) {
//			CopyLineString(Data->Version, msg.Buffer, Priv->Lines, 2);
//		}
//		smprintf(s, "Received firmware version: \"%s\"\n",Data->Version);
//		GSM_CreateFirmwareNumber(Data);
//		return ERR_NONE;
	case AT_Reply_Error:
		return ERR_NOTSUPPORTED;
	case AT_Reply_CMSError:
		return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetFirmware(GSM_StateMachine *s)
{
	GSM_Error error;

	if (s->Phone.Data.Version[0] != 0) return ERR_NONE;

	error=ATGEN_GetManufacturer(s);
	if (error != ERR_NONE) return error;

//	smprintf(s, "Getting firmware - method 1\n");
//	error=GSM_WaitFor (s, "ATI\r", 4, 0x00, 3, ID_GetFirmware);
//	if (error != ERR_NONE) {
		smprintf(s, "Getting firmware - method 2\n");
		error=GSM_WaitFor (s, "AT+CGMR\r", 8, 0x00, 3, ID_GetFirmware);
//	}
	if (error==ERR_NONE) {
		if (s->di.dl==DL_TEXT || s->di.dl==DL_TEXTALL ||
		    s->di.dl==DL_TEXTDATE || s->di.dl==DL_TEXTALLDATE) {
			smprintf(s, "[Firmware version - \"%s\"]\n",s->Phone.Data.Version);
		}
	}
	return error;
}

GSM_Error ATGEN_Initialise(GSM_StateMachine *s)
{
	GSM_Phone_ATGENData     *Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error               error;
    	char                    buff[2];

	Priv->SMSMode			= 0;
	Priv->Manufacturer		= 0;
	Priv->PhoneSMSMemory		= 0;
	Priv->CanSaveSMS		= false;
	Priv->SIMSMSMemory		= 0;
	Priv->SMSMemory			= 0;
	Priv->PBKMemory			= 0;
	Priv->PBKSBNR			= 0;
	Priv->Charset			= 0;
	Priv->EncodedCommands		= false;
	Priv->NormalCharset		= 0;
	Priv->IRACharset		= 0;
	Priv->UnicodeCharset		= 0;
	Priv->PBKMemories[0]		= 0;
	Priv->FirstCalendarPos		= 0;
	Priv->NextMemoryEntry		= 0;
	Priv->FirstMemoryEntry		= -1;
	Priv->file.Used 		= 0;
	Priv->file.Buffer 		= NULL;
	Priv->OBEX			= false;
	Priv->MemorySize		= 0;
	Priv->TextLength		= 0;
	Priv->NumberLength		= 0;

	Priv->CNMIMode			= -1;
	Priv->CNMIProcedure		= -1;
	Priv->CNMIDeliverProcedure	= -1;
#ifdef GSM_ENABLE_CELLBROADCAST
	Priv->CNMIBroadcastProcedure	= -1;
#endif

	Priv->ErrorText			= NULL;

	if (s->ConnectionType != GCT_IRDAAT && s->ConnectionType != GCT_BLUEAT) {
		/* We try to escape AT+CMGS mode, at least Siemens M20
		 * then needs to get some rest
		 */
		smprintf(s, "Escaping SMS mode\n");
		error = s->Protocol.Functions->WriteMessage(s, "\x1B\r", 2, 0x00);
		if (error!=ERR_NONE) return error;

	    	/* Grab any possible garbage */
	    	while (s->Device.Functions->ReadDevice(s, buff, 2) > 0) my_sleep(10);
	}

    	/* When some phones (Alcatel BE5) is first time connected, it needs extra
     	 * time to react, sending just AT wakes up the phone and it then can react
     	 * to ATE1. We don't need to check whether this fails as it is just to
     	 * wake up the phone and does nothing.
     	 */
    	smprintf(s, "Sending simple AT command to wake up some devices\n");
	GSM_WaitFor (s, "AT\r", 3, 0x00, 2, ID_IncomingFrame);

	smprintf(s, "Enabling echo\n");
	error = GSM_WaitFor (s, "ATE1\r", 5, 0x00, 3, ID_EnableEcho);
	/* Some modems (Sony Ericsson GC 79, GC 85) need to enable functionality
	 * (with reset), otherwise they return ERROR on anything!
	 */
	if (error == ERR_UNKNOWN) {
		error = GSM_WaitFor (s, "AT+CFUN=1,1\r", 12, 0x00, 3, ID_Reset);
		if (error != ERR_NONE) return error;
		error = GSM_WaitFor (s, "ATE1\r", 5, 0x00, 3, ID_EnableEcho);
	}
	if (error != ERR_NONE) return error;

	smprintf(s, "Enabling CME errors\n");
	/* Try numeric errors */
	if (GSM_WaitFor (s, "AT+CMEE=1\r", 10, 0x00, 3, ID_EnableErrorInfo) != ERR_NONE) {
		/* Try textual errors */
		if (GSM_WaitFor (s, "AT+CMEE=2\r", 10, 0x00, 3, ID_EnableErrorInfo) != ERR_NONE) {
			smprintf(s, "CME errors could not be enabled, some error types won't be detected.\n");
		}
	}

	error = ATGEN_GetModel(s);
	if (error != ERR_NONE) return error;

	smprintf(s, "Checking for OBEX support\n");
	/* We don't care about error here */
	GSM_WaitFor (s, "AT+CPROT=?\r", 11, 0x00, 3, ID_SetOBEX);

	if (!IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_SLOWWRITE)) {
		s->Protocol.Data.AT.FastWrite = true;
	}

	return error;
}

GSM_Error ATGEN_ReplyGetCharset(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	/* Reply we get here:
		AT+CSCS?
		+CSCS: "GSM"

		OK
	 */
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	char			*line;
	int			i = 0;

	switch (Priv->ReplyState) {
		case AT_Reply_OK:
			line = GetLineString(msg.Buffer, Priv->Lines, 2);
			/* First current charset: */
			while (AT_Charsets[i].charset != 0) {
				if (strstr(line, AT_Charsets[i].text) != NULL) {
					Priv->Charset = AT_Charsets[i].charset;
					break;
				}
				/* We detect encoded UCS2 reply here so that we can handle encoding of values later. */
				if (strstr(line, "0055004300530032") != NULL) {
					Priv->Charset = AT_CHARSET_UCS2;
					Priv->EncodedCommands = true;
					break;
				}
				i++;
			}
			if (Priv->Charset == 0) {
				smprintf(s, "Could not determine charset returned by phone, probably not supported!\n");
				return ERR_NOTSUPPORTED;
			}
			return ERR_NONE;
		case AT_Reply_Error:
			return ERR_NOTSUPPORTED;
		case AT_Reply_CMSError:
			return ATGEN_HandleCMSError(s);
		case AT_Reply_CMEError:
			return ATGEN_HandleCMEError(s);
		default:
			return ERR_UNKNOWNRESPONSE;
	}
}

GSM_Error ATGEN_ReplyGetCharsets(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	/* Reply we get here:
		AT+CSCS=?
		+CSCS: ("GSM","UCS2")

		OK
	 */
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	char			*line;
	int			i = 0;

	switch (Priv->ReplyState) {
		case AT_Reply_OK:
			line = GetLineString(msg.Buffer, Priv->Lines, 2);
			/* First find good charset for non-unicode: */
			while (AT_Charsets[i].charset != 0) {
				if (strstr(line, AT_Charsets[i].text) != NULL) {
					Priv->NormalCharset = AT_Charsets[i].charset;
					Priv->IRACharset = AT_Charsets[i].charset;
					break;
				}
				i++;
			}
			/* Use IRA charset if we support it */
			if (strstr(line, "IRA") != NULL) {
				Priv->IRACharset = AT_CHARSET_IRA;
			}
			if (Priv->NormalCharset == 0) {
				smprintf(s, "Could not find supported charset in list returned by phone!\n");
				return ERR_UNKNOWNRESPONSE;
			}
			/* Then find good charset for unicode: */
			Priv->UnicodeCharset = 0;
			while (AT_Charsets[i].charset != 0) {
				if (AT_Charsets[i].unicode && (strstr(line, AT_Charsets[i].text) != NULL)) {
					Priv->UnicodeCharset = AT_Charsets[i].charset;
					break;
				}
				i++;
			}
			if (Priv->UnicodeCharset == 0) {
				Priv->UnicodeCharset = Priv->NormalCharset;
			}
			return ERR_NONE;
		case AT_Reply_Error:
			return ERR_NOTSUPPORTED;
		case AT_Reply_CMSError:
			return ATGEN_HandleCMSError(s);
		case AT_Reply_CMEError:
			return ATGEN_HandleCMEError(s);
		default:
			return ERR_UNKNOWNRESPONSE;
	}
}


GSM_Error ATGEN_SetCharset(GSM_StateMachine *s, GSM_AT_Charset_Preference Prefer)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error		error;
	char			buffer[100];
	char			buffer2[100];
	char			buffer3[100];
	int			i = 0;
	GSM_AT_Charset		cset;

	/* Do we know available charsets? */
	if (Priv->NormalCharset == 0) {
		/* Get available charsets */
		error = GSM_WaitFor (s, "AT+CSCS=?\r", 10, 0x00, 3, ID_GetMemoryCharset);
		if (error != ERR_NONE) return error;
	}

	/* Do we know current charset? */
	if (Priv->Charset == 0) {
		/* Get current charset */
		error = GSM_WaitFor (s, "AT+CSCS?\r", 9, 0x00, 3, ID_GetMemoryCharset);
		/* ERR_NOTSUPPORTED means that we do not know charset phone returned */
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}

	/* Find charset we want */
	if (Prefer == AT_PREF_CHARSET_UNICODE) {
		cset = Priv->UnicodeCharset;
	} else if (Prefer == AT_PREF_CHARSET_NORMAL) {
		cset = Priv->NormalCharset;
	} else if (Prefer == AT_PREF_CHARSET_IRA) {
		cset = Priv->IRACharset;
	} else {
		return ERR_BUG;
	}

	/* If we already have set our preffered charset there is nothing to do*/
	if (Priv->Charset == cset) return ERR_NONE;

	/* Find text representation */
	while (AT_Charsets[i].charset != 0) {
		if (AT_Charsets[i].charset == cset) {
			break;
		}
		i++;
	}

	/* Should not happen! */
	if (AT_Charsets[i].charset == 0) {
		smprintf(s, "Could not find string representation for charset!\n");
		return ERR_BUG;
	}

	/* And finally set the charset */
	if (Priv->EncodedCommands && Priv->Charset == AT_CHARSET_UCS2) {
		EncodeUnicode(buffer2, AT_Charsets[i].text, strlen(AT_Charsets[i].text));
		EncodeHexUnicode(buffer3, buffer2, strlen(AT_Charsets[i].text));
		sprintf(buffer, "AT+CSCS=\"%s\"\r", buffer3);
	} else {
		sprintf(buffer, "AT+CSCS=\"%s\"\r", AT_Charsets[i].text);
	}
	error = GSM_WaitFor (s, buffer, strlen(buffer), 0x00, 3, ID_SetMemoryCharset);
	if (error == ERR_NONE) Priv->Charset = cset;
	else return error;

	/* Verify we have charset we wanted (this is especially needed to detect whether phone encodes also control information and not only data) */
	error = GSM_WaitFor (s, "AT+CSCS?\r", 9, 0x00, 3, ID_GetMemoryCharset);

	return error;
}

GSM_Error ATGEN_SetSMSC(GSM_StateMachine *s, GSM_SMSC *smsc)
{
	unsigned char req[50];

	if (smsc->Location!=1) return ERR_INVALIDLOCATION;

	sprintf(req, "AT+CSCA=\"%s\"\r",DecodeUnicodeString(smsc->Number));

	smprintf(s, "Setting SMSC\n");
	return GSM_WaitFor (s, req, strlen(req), 0x00, 4, ID_SetSMSC);
}

GSM_Error ATGEN_ReplyGetSMSMemories(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		/* Reply here is:
		 * (memories for reading)[, (memories for writing)[, (memories for storing received messages)]]
		 * each memory is in quotes,
		 * Example: ("SM"), ("SM"), ("SM")
		 *
		 * We need to get from this supported memories. For this case
		 * we assume, that just appearence of memory makes it
		 * available for everything. Then we need to find out whether
		 * phone supports writing to memory. This is done by searching
		 * for "), (", which will appear between lists.
		 */
		s->Phone.Data.Priv.ATGEN.CanSaveSMS = false;
		if (strstr(msg.Buffer, "), (") != NULL || strstr(msg.Buffer, "),(") != NULL) {
			s->Phone.Data.Priv.ATGEN.CanSaveSMS = true;
		}

		if (strstr(msg.Buffer, "\"SM\"") != NULL) s->Phone.Data.Priv.ATGEN.SIMSMSMemory = AT_AVAILABLE;
		else s->Phone.Data.Priv.ATGEN.SIMSMSMemory = AT_NOTAVAILABLE;

		if (strstr(msg.Buffer, "\"ME\"") != NULL) s->Phone.Data.Priv.ATGEN.PhoneSMSMemory = AT_AVAILABLE;
		else s->Phone.Data.Priv.ATGEN.PhoneSMSMemory = AT_NOTAVAILABLE;

		smprintf(s, "Available SMS memories received, ME = %d, SM = %d, cansavesms =", s->Phone.Data.Priv.ATGEN.PhoneSMSMemory, s->Phone.Data.Priv.ATGEN.SIMSMSMemory);
		if (s->Phone.Data.Priv.ATGEN.CanSaveSMS) smprintf(s, "true");
		smprintf(s, "\n");
		return ERR_NONE;
	case AT_Reply_Error:
	case AT_Reply_CMSError:
		return ATGEN_HandleCMSError(s);
	case AT_Reply_CMEError:
		return ATGEN_HandleCMEError(s);
	default:
		return ERR_UNKNOWNRESPONSE;
	}
}

GSM_Error ATGEN_GetSMSMemories(GSM_StateMachine *s)
{
	smprintf(s, "Getting available SMS memories\n");
	return GSM_WaitFor (s, "AT+CPMS=?\r", 10, 0x00, 4, ID_GetSMSMemories);
}

GSM_Error ATGEN_SetSMSMemory(GSM_StateMachine *s, bool SIM)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	char 			req[] = "AT+CPMS=\"XX\",\"XX\"\r";
	int			reqlen = 18;
	GSM_Error		error;

	/* If phone encodes also values in command, we need normal charset */
	if (Priv->EncodedCommands) {
		error = ATGEN_SetCharset(s, AT_PREF_CHARSET_NORMAL);
		if (error != ERR_NONE) return error;
	}

	if ((SIM && Priv->SIMSMSMemory == 0) || (!SIM && Priv->PhoneSMSMemory == 0)) {
		/* We silently ignore error here, because when this fails, we can try to setmemory anyway */
		ATGEN_GetSMSMemories(s);
	}

	/* If phone can not save SMS, don't try to set memory for saving */
	if (!Priv->CanSaveSMS) {
		req[12] = '\r';
		reqlen = 13;
	}

	if (SIM) {
		if (Priv->SMSMemory == MEM_SM) return ERR_NONE;
		if (Priv->SIMSMSMemory == AT_NOTAVAILABLE) return ERR_NOTSUPPORTED;

		req[9]  = 'S'; req[10] = 'M';
		req[14] = 'S'; req[15] = 'M';

		smprintf(s, "Setting SMS memory type to SM\n");
		error=GSM_WaitFor (s, req, reqlen, 0x00, 3, ID_SetMemoryType);
		if (Priv->SIMSMSMemory == 0 && error == ERR_NONE) {
			Priv->SIMSMSMemory = AT_AVAILABLE;
		}
		if (error == ERR_NOTSUPPORTED) {
			smprintf(s, "Can't access SIM card?\n");
			return ERR_SECURITYERROR;
		}
		if (error != ERR_NONE) return error;
		Priv->SMSMemory = MEM_SM;
	} else {
		if (Priv->SMSMemory == MEM_ME) return ERR_NONE;
		if (Priv->PhoneSMSMemory == AT_NOTAVAILABLE) return ERR_NOTSUPPORTED;

		req[9]  = 'M'; req[10] = 'E';
		req[14] = 'M'; req[15] = 'E';

		smprintf(s, "Setting SMS memory type to ME\n");
		error=GSM_WaitFor (s, req, reqlen, 0x00, 3, ID_SetMemoryType);
		if (Priv->PhoneSMSMemory == 0 && error == ERR_NONE) {
			Priv->PhoneSMSMemory = AT_AVAILABLE;
		}
		if (error == ERR_NONE) Priv->SMSMemory = MEM_ME;
	}
	return error;
}

GSM_Error ATGEN_GetSMSMode(GSM_StateMachine *s)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
  	GSM_Error 		error;

	if (Priv->SMSMode != 0) return ERR_NONE;

	smprintf(s, "Trying SMS PDU mode\n");
	error=GSM_WaitFor (s, "AT+CMGF=0\r", 10, 0x00, 3, ID_GetSMSMode);
	if (error==ERR_NONE) {
		Priv->SMSMode = SMS_AT_PDU;
		return ERR_NONE;
	}

	smprintf(s, "Trying SMS text mode\n");
	error=GSM_WaitFor (s, "AT+CMGF=1\r", 10, 0x00, 3, ID_GetSMSMode);
	if (error==ERR_NONE) {
		smprintf(s, "Enabling displaying all parameters in text mode\n");
		error=GSM_WaitFor (s, "AT+CSDH=1\r", 10, 0x00, 3, ID_GetSMSMode);
		if (error == ERR_NONE) Priv->SMSMode = SMS_AT_TXT;
	}

	return error;
}

GSM_Error ATGEN_GetSMSLocation(GSM_StateMachine *s, GSM_SMSMessage *sms, unsigned char *folderid, int *location)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	int			ifolderid, maxfolder;
	GSM_Error		error;

	if (Priv->PhoneSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, false);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}
	if (Priv->SIMSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, true);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}

	if (Priv->SIMSMSMemory != AT_AVAILABLE && Priv->PhoneSMSMemory != AT_AVAILABLE) {
		/* No SMS memory at all */
		return ERR_NOTSUPPORTED;
	}
	if (Priv->SIMSMSMemory == AT_AVAILABLE && Priv->PhoneSMSMemory == AT_AVAILABLE) {
		/* Both available */
		maxfolder = 2;
	} else {
		/* One available */
		maxfolder = 1;
	}

	/* simulate flat SMS memory */
	if (sms->Folder == 0x00) {
		ifolderid = sms->Location / PHONE_MAXSMSINFOLDER;
		if (ifolderid + 1 > maxfolder) return ERR_NOTSUPPORTED;
		*folderid = ifolderid + 1;
		*location = sms->Location - ifolderid * PHONE_MAXSMSINFOLDER;
	} else {
		if (sms->Folder > 2 * maxfolder) return ERR_NOTSUPPORTED;
		*folderid = sms->Folder <= 2 ? 1 : 2;
		*location = sms->Location;
	}
	smprintf(s, "SMS folder %i & location %i -> ATGEN folder %i & location %i\n",
		sms->Folder,sms->Location,*folderid,*location);

	if (Priv->SIMSMSMemory == AT_AVAILABLE && *folderid == 1) {
		return ATGEN_SetSMSMemory(s, true);
	} else {
		return ATGEN_SetSMSMemory(s, false);
	}
}

void ATGEN_SetSMSLocation(GSM_StateMachine *s, GSM_SMSMessage *sms, unsigned char folderid, int location)
{
	sms->Folder	= 0;
	sms->Location	= (folderid - 1) * PHONE_MAXSMSINFOLDER + location;
	smprintf(s, "ATGEN folder %i & location %i -> SMS folder %i & location %i\n",
		folderid,location,sms->Folder,sms->Location);
}

GSM_Error ATGEN_ReplyGetSMSMessage(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv 	= &s->Phone.Data.Priv.ATGEN;
	GSM_SMSMessage		*sms	= &s->Phone.Data.GetSMSMessage->SMS[0];
	int 			current = 0, current2, i;
	unsigned char 		buffer[300],smsframe[800];
	unsigned char		firstbyte, TPDCS, TPUDL, TPStatus;
	GSM_Error		error=ERR_UNKNOWN;

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
		if (Priv->Lines.numbers[4] == 0x00) return ERR_EMPTY;
		s->Phone.Data.GetSMSMessage->Number 	 	= 1;
		s->Phone.Data.GetSMSMessage->SMS[0].Name[0] 	= 0;
		s->Phone.Data.GetSMSMessage->SMS[0].Name[1]	= 0;
		switch (Priv->SMSMode) {
		case SMS_AT_PDU:
			CopyLineString(buffer, msg.Buffer, Priv->Lines, 2);
			switch (buffer[7]) {
				case '0': sms->State = SMS_UnRead; 	break;
				case '1': sms->State = SMS_Read;	break;
				case '2': sms->State = SMS_UnSent;	break;
				default : sms->State = SMS_Sent;	break;//case '3'
			}
			DecodeHexBin (buffer, GetLineString(msg.Buffer,Priv->Lines,3), strlen(GetLineString(msg.Buffer,Priv->Lines,3)));
			/* Siemens MC35 (only ?) */
			if (strstr(msg.Buffer,"+CMGR: 0,,0")!=NULL) return ERR_EMPTY;
			/* Siemens M20 */
			if (IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_M20SMS)) {
				/* we check for the most often visible */
				if (buffer[1]!=NUMBER_UNKNOWN_NUMBERING_PLAN_ISDN && buffer[1]!=NUMBER_INTERNATIONAL_NUMBERING_PLAN_ISDN &&
				    buffer[1]!=NUMBER_ALPHANUMERIC_NUMBERING_PLAN_UNKNOWN) {
					/* Seems to be Delivery Report */
					smprintf(s, "SMS type - status report (M20 style)\n");
					sms->PDU 	 = SMS_Status_Report;
					sms->Folder 	 = 1;	/*INBOX SIM*/
					sms->InboxFolder = true;

					smsframe[12]=buffer[current++];
					smsframe[PHONE_SMSStatusReport.TPMR]=buffer[current++];
					current2=((buffer[current])+1)/2+1;
					for(i=0;i<current2+1;i++) smsframe[PHONE_SMSStatusReport.Number+i]=buffer[current++];
					for(i=0;i<7;i++) smsframe[PHONE_SMSStatusReport.DateTime+i]=buffer[current++];
					smsframe[0] = 0;
					for(i=0;i<7;i++) smsframe[PHONE_SMSStatusReport.SMSCTime+i]=buffer[current++];
					smsframe[PHONE_SMSStatusReport.TPStatus]=buffer[current];
					GSM_DecodeSMSFrame(sms,smsframe,PHONE_SMSStatusReport);
					return ERR_NONE;
				}
			}
			/* We use locations from SMS layouts like in ../phone2.c(h) */
			for(i=0;i<buffer[0]+1;i++) smsframe[i]=buffer[current++];
			smsframe[12]=buffer[current++];
			/* See GSM 03.40 section 9.2.3.1 */
			switch (smsframe[12] & 0x03) {
			case 0x00:
				smprintf(s, "SMS type - deliver\n");
				sms->PDU 	 = SMS_Deliver;
				if (Priv->SMSMemory == MEM_SM) {
					sms->Folder = 1; /*INBOX SIM*/
				} else {
					sms->Folder = 3; /*INBOX ME*/
				}
				sms->InboxFolder = true;
				current2=((buffer[current])+1)/2+1;
				if (IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_M20SMS)) {
					if (buffer[current+1]==NUMBER_ALPHANUMERIC_NUMBERING_PLAN_UNKNOWN) {
						smprintf(s, "Trying to read alphanumeric number\n");
						for(i=0;i<4;i++) smsframe[PHONE_SMSDeliver.Number+i]=buffer[current++];
						current+=6;
						for(i=0;i<current2-3;i++) smsframe[PHONE_SMSDeliver.Number+i+4]=buffer[current++];
					} else {
						for(i=0;i<current2+1;i++) smsframe[PHONE_SMSDeliver.Number+i]=buffer[current++];
					}
				} else {
					for(i=0;i<current2+1;i++) smsframe[PHONE_SMSDeliver.Number+i]=buffer[current++];
				}
				smsframe[PHONE_SMSDeliver.TPPID] = buffer[current++];
				smsframe[PHONE_SMSDeliver.TPDCS] = buffer[current++];
				for(i=0;i<7;i++) smsframe[PHONE_SMSDeliver.DateTime+i]=buffer[current++];
				smsframe[PHONE_SMSDeliver.TPUDL] = buffer[current++];
				for(i=0;i<smsframe[PHONE_SMSDeliver.TPUDL];i++) smsframe[i+PHONE_SMSDeliver.Text]=buffer[current++];
				GSM_DecodeSMSFrame(sms,smsframe,PHONE_SMSDeliver);
				return ERR_NONE;
			case 0x01:
				smprintf(s, "SMS type - submit\n");
				sms->PDU 	 = SMS_Submit;
				if (Priv->SMSMemory == MEM_SM) {
					sms->Folder = 2; /*OUTBOX SIM*/
					smprintf(s, "Outbox SIM\n");
				} else {
					sms->Folder = 4; /*OUTBOX ME*/
				}
				sms->InboxFolder = false;
				smsframe[PHONE_SMSSubmit.TPMR] = buffer[current++];
				current2=((buffer[current])+1)/2+1;
				if (IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_M20SMS)) {
					if (buffer[current+1]==NUMBER_ALPHANUMERIC_NUMBERING_PLAN_UNKNOWN) {
						smprintf(s, "Trying to read alphanumeric number\n");
						for(i=0;i<4;i++) smsframe[PHONE_SMSSubmit.Number+i]=buffer[current++];
						current+=6;
						for(i=0;i<current2-3;i++) smsframe[PHONE_SMSSubmit.Number+i+4]=buffer[current++];
					} else {
						for(i=0;i<current2+1;i++) smsframe[PHONE_SMSSubmit.Number+i]=buffer[current++];
					}
				} else {
					for(i=0;i<current2+1;i++) smsframe[PHONE_SMSSubmit.Number+i]=buffer[current++];
				}
				smsframe[PHONE_SMSSubmit.TPPID] = buffer[current++];
				smsframe[PHONE_SMSSubmit.TPDCS] = buffer[current++];
				/* See GSM 03.40 9.2.3.3 - TPVP can not exist in frame */
				if ((smsframe[12] & 0x18)!=0) current++; //TPVP is ignored now
				smsframe[PHONE_SMSSubmit.TPUDL] = buffer[current++];
				for(i=0;i<smsframe[PHONE_SMSSubmit.TPUDL];i++) smsframe[i+PHONE_SMSSubmit.Text]=buffer[current++];
				GSM_DecodeSMSFrame(sms,smsframe,PHONE_SMSSubmit);
				return ERR_NONE;
			case 0x02:
				smprintf(s, "SMS type - status report\n");
				sms->PDU 	 = SMS_Status_Report;
				sms->Folder 	 = 1;	/*INBOX SIM*/
				sms->InboxFolder = true;
				smprintf(s, "TPMR is %d\n",buffer[current]);
				smsframe[PHONE_SMSStatusReport.TPMR] = buffer[current++];
				current2=((buffer[current])+1)/2+1;
				for(i=0;i<current2+1;i++) smsframe[PHONE_SMSStatusReport.Number+i]=buffer[current++];
				for(i=0;i<7;i++) smsframe[PHONE_SMSStatusReport.DateTime+i]=buffer[current++];
				for(i=0;i<7;i++) smsframe[PHONE_SMSStatusReport.SMSCTime+i]=buffer[current++];
				smsframe[PHONE_SMSStatusReport.TPStatus]=buffer[current];
				GSM_DecodeSMSFrame(sms,smsframe,PHONE_SMSStatusReport);
				return ERR_NONE;
			}
			break;
		case SMS_AT_TXT:
			current = 0;
			while (msg.Buffer[current]!='"') current++;
			current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
			if (!strcmp(buffer,"\"0\"") || !strcmp(buffer,"\"REC UNREAD\"")) {
				smprintf(s, "SMS type - deliver\n");
				sms->State 	 = SMS_UnRead;
				sms->PDU 	 = SMS_Deliver;
				if (Priv->SMSMemory == MEM_SM) {
					sms->Folder = 1; /*INBOX SIM*/
				} else {
					sms->Folder = 3; /*INBOX ME*/
				}
				sms->InboxFolder = true;
			} else if (!strcmp(buffer,"\"1\"") || !strcmp(buffer,"\"REC READ\"")) {
				smprintf(s, "SMS type - deliver\n");
				sms->State 	 = SMS_Read;
				sms->PDU 	 = SMS_Deliver;
				if (Priv->SMSMemory == MEM_SM) {
					sms->Folder = 1; /*INBOX SIM*/
				} else {
					sms->Folder = 3; /*INBOX ME*/
				}
				sms->InboxFolder = true;
			} else if (!strcmp(buffer,"\"2\"") || !strcmp(buffer,"\"STO UNSENT\"")) {
				smprintf(s, "SMS type - submit\n");
				sms->State 	 = SMS_UnSent;
				sms->PDU 	 = SMS_Submit;
				if (Priv->SMSMemory == MEM_SM) {
					sms->Folder = 2; /*OUTBOX SIM*/
				} else {
					sms->Folder = 4; /*OUTBOX ME*/
				}
				sms->InboxFolder = false;
			} else if (!strcmp(buffer,"\"3\"") || !strcmp(buffer,"\"STO SENT\"")) {
				smprintf(s, "SMS type - submit\n");
				sms->State 	 = SMS_Sent;
				sms->PDU 	 = SMS_Submit;
				if (Priv->SMSMemory == MEM_SM) {
					sms->Folder = 2; /*OUTBOX SIM*/
				} else {
					sms->Folder = 4; /*OUTBOX ME*/
				}
				sms->InboxFolder = false;
			}
			current += ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
			/* It's delivery report according to Nokia AT standards */
			if (sms->Folder==1 && buffer[0]!=0 && buffer[0]!='"') {
				/* ??? */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				/* format of sender number */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				/* Sender number */
				/* FIXME: support for all formats */
				EncodeUnicode(sms->Number,buffer+1,strlen(buffer)-2);
				smprintf(s, "Sender \"%s\"\n",DecodeUnicodeString(sms->Number));
				/* ??? */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				/* Sending datetime */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				i = strlen(buffer);
				buffer[i] = ',';
				i++;
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer+i);
				smprintf(s, "\"%s\"\n",buffer);
				error = ATGEN_DecodeDateTime(s, &sms->DateTime, buffer);
				if (error!=ERR_NONE) return error;
				/* Date of SMSC response */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				i = strlen(buffer);
				buffer[i] = ',';
				i++;
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer+i);
				smprintf(s, "\"%s\"\n",buffer);
				error = ATGEN_DecodeDateTime(s, &sms->SMSCTime, buffer);
				if (error!=ERR_NONE) return error;
				/* TPStatus */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				TPStatus=atoi(buffer);
				buffer[PHONE_SMSStatusReport.TPStatus] = TPStatus;
				error=GSM_DecodeSMSFrameStatusReportData(sms, buffer, PHONE_SMSStatusReport);
				if (error!=ERR_NONE) return error;
				/* NO SMSC number */
				sms->SMSC.Number[0]=0;
				sms->SMSC.Number[1]=0;
				sms->PDU = SMS_Status_Report;
				sms->ReplyViaSameSMSC=false;
			} else {
				/* Sender number */
				/* FIXME: support for all formats */
				EncodeUnicode(sms->Number,buffer+1,strlen(buffer)-2);
				/* Sender number in alphanumeric format ? */
				current += ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				if (strlen(buffer)!=0) EncodeUnicode(sms->Number,buffer+1,strlen(buffer)-2);
				smprintf(s, "Sender \"%s\"\n",DecodeUnicodeString(sms->Number));
				/* Sending datetime */
				if (sms->Folder==1 || sms->Folder==3) {
					current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
					/* FIXME: ATGEN_ExtractOneParameter() is broken as it doesn't respect
					 * quoting of parameters and thus +FOO: "ab","cd,ef" will consider
					 * as three arguments: "ab" >> "cd >> ef"
					 */
					if (*buffer=='"') {
						i = strlen(buffer);
						buffer[i] = ',';
						i++;
						current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer+i);
					}
					smprintf(s, "\"%s\"\n",buffer);
					if (*buffer)
						error = ATGEN_DecodeDateTime(s, &sms->DateTime, buffer);
						if (error!=ERR_NONE) return error;
					else {
						/* FIXME: What is the proper undefined GSM_DateTime ? */
						memset(&sms->DateTime, 0, sizeof(sms->DateTime));
					}
					error = ATGEN_DecodeDateTime(s, &sms->DateTime, buffer);
					if (error!=ERR_NONE) return error;
				}
				/* Sender number format */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				/* First byte */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				firstbyte=atoi(buffer);
				sms->ReplyViaSameSMSC=false;
				/* GSM 03.40 section 9.2.3.17 (TP-Reply-Path) */
				if ((firstbyte & 128)==128) sms->ReplyViaSameSMSC=true;
				/* TP PID */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				sms->ReplaceMessage = 0;
				if (atoi(buffer) > 0x40 && atoi(buffer) < 0x48) {
					sms->ReplaceMessage = atoi(buffer) - 0x40;
				}
				smprintf(s, "TPPID: %02x %i\n",atoi(buffer),atoi(buffer));
				/* TP DCS */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				TPDCS=atoi(buffer);
				if (sms->Folder==2 || sms->Folder==4) {
					/*TP VP */
					current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				}
				/* SMSC number */
				/* FIXME: support for all formats */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				EncodeUnicode(sms->SMSC.Number,buffer+1,strlen(buffer)-2);
				/* Format of SMSC number */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				/* TPUDL */
				current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
				TPUDL=atoi(buffer);
				current++;
				sms->Coding = SMS_Coding_8bit;
				/* GSM 03.40 section 9.2.3.10 (TP-Data-Coding-Scheme)
				 * and GSM 03.38 section 4
				 */
				if ((TPDCS & 0xC0) == 0) {
					/* bits 7..4 set to 00xx */
					if ((TPDCS & 0xC) == 0xC) {
						dbgprintf("WARNING: reserved alphabet value in TPDCS\n");
					} else {
						if (TPDCS == 0) 		sms->Coding=SMS_Coding_Default_No_Compression;
						if ((TPDCS & 0x2C) == 0x00) 	sms->Coding=SMS_Coding_Default_No_Compression;
						if ((TPDCS & 0x2C) == 0x20) 	sms->Coding=SMS_Coding_Default_Compression;
						if ((TPDCS & 0x2C) == 0x08) 	sms->Coding=SMS_Coding_Unicode_No_Compression;
						if ((TPDCS & 0x2C) == 0x28) 	sms->Coding=SMS_Coding_Unicode_Compression;
					}
				} else if ((TPDCS & 0xF0) >= 0x40 &&
					   (TPDCS & 0xF0) <= 0xB0) {
					/* bits 7..4 set to 0100 ... 1011 */
					dbgprintf("WARNING: reserved coding group in TPDCS\n");
				} else if (((TPDCS & 0xF0) == 0xC0) ||
				      	   ((TPDCS & 0xF0) == 0xD0)) {
					/* bits 7..4 set to 1100 or 1101 */
					if ((TPDCS & 4) == 4) {
						dbgprintf("WARNING: set reserved bit 2 in TPDCS\n");
					} else {
						sms->Coding=SMS_Coding_Default_No_Compression;
					}
				} else if ((TPDCS & 0xF0) == 0xE0) {
					/* bits 7..4 set to 1110 */
					if ((TPDCS & 4) == 4) {
						dbgprintf("WARNING: set reserved bit 2 in TPDCS\n");
					} else {
						sms->Coding=SMS_Coding_Unicode_No_Compression;
					}
				} else if ((TPDCS & 0xF0) == 0xF0) {
					/* bits 7..4 set to 1111 */
					if ((TPDCS & 8) == 8) {
						dbgprintf("WARNING: set reserved bit 3 in TPDCS\n");
					} else {
						if ((TPDCS & 4) == 0) sms->Coding=SMS_Coding_Default_No_Compression;
					}
				}
				sms->Class = -1;
				if ((TPDCS & 0xD0) == 0x10) {
					/* bits 7..4 set to 00x1 */
					if ((TPDCS & 0xC) == 0xC) {
						dbgprintf("WARNING: reserved alphabet value in TPDCS\n");
					} else {
						sms->Class = (TPDCS & 3);
					}
				} else if ((TPDCS & 0xF0) == 0xF0) {
					/* bits 7..4 set to 1111 */
					if ((TPDCS & 8) == 8) {
						dbgprintf("WARNING: set reserved bit 3 in TPDCS\n");
					} else {
						sms->Class = (TPDCS & 3);
					}
				}
				smprintf(s, "SMS class: %i\n",sms->Class);
				switch (sms->Coding) {
				case SMS_Coding_Default_No_Compression:
					/* GSM 03.40 section 9.2.3.23 (TP-User-Data-Header-Indicator) */
					/* If not SMS with UDH, it's coded normal */
					/* If UDH available, treat it as Unicode or 8 bit */
					if ((firstbyte & 0x40)!=0x40) {
						sms->UDH.Type	= UDH_NoUDH;
						sms->Length	= TPUDL;
						EncodeUnicode(sms->Text,msg.Buffer+Priv->Lines.numbers[2*2],TPUDL);
						break;
					}
				case SMS_Coding_Unicode_No_Compression:
				case SMS_Coding_8bit:
					DecodeHexBin(buffer+PHONE_SMSDeliver.Text, msg.Buffer+current, TPUDL*2);
					buffer[PHONE_SMSDeliver.firstbyte] 	= firstbyte;
					buffer[PHONE_SMSDeliver.TPDCS] 		= TPDCS;
					buffer[PHONE_SMSDeliver.TPUDL] 		= TPUDL;
					return GSM_DecodeSMSFrameText(sms, buffer, PHONE_SMSDeliver);
				default:
					break;
				}
			}
			return ERR_NONE;
		default:
			break;
		}
		break;
	case AT_Reply_CMSError:
		if (Priv->ErrorCode == 320 || Priv->ErrorCode == 500) {
			return ERR_EMPTY;
		} else {
			return ATGEN_HandleCMSError(s);
		}
	case AT_Reply_CMEError:
		return ATGEN_HandleCMEError(s);
	case AT_Reply_Error:
		/* A2D returns Error with empty location */
		return ERR_EMPTY;
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetSMS(GSM_StateMachine *s, GSM_MultiSMSMessage *sms)
{
	unsigned char		req[20], folderid;
	GSM_Error		error;
	int			location, getfolder, add = 0;
	GSM_Phone_ATGENData 	*Priv 	= &s->Phone.Data.Priv.ATGEN;

	error=ATGEN_GetSMSLocation(s,&sms->SMS[0], &folderid, &location);
	if (error!=ERR_NONE) return error;
	if (Priv->SMSMemory == MEM_ME && IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_SMSME900)) add = 899;
	sprintf(req, "AT+CMGR=%i\r", location + add);

	error=ATGEN_GetSMSMode(s);
	if (error != ERR_NONE) return error;

	/* There is possibility that date will be encoded in text mode */
	if (Priv->SMSMode == SMS_AT_TXT) {
		error = ATGEN_SetCharset(s, AT_PREF_CHARSET_NORMAL);
		if (error != ERR_NONE) return error;
	}

	error=ATGEN_GetManufacturer(s);
	if (error != ERR_NONE) return error;

	s->Phone.Data.GetSMSMessage=sms;
	smprintf(s, "Getting SMS\n");
	error=GSM_WaitFor (s, req, strlen(req), 0x00, 5, ID_GetSMSMessage);
	if (error==ERR_NONE) {
		getfolder = sms->SMS[0].Folder;
//		if (getfolder != 0 && getfolder != sms->SMS[0].Folder) return ERR_EMPTY;
		ATGEN_SetSMSLocation(s, &sms->SMS[0], folderid, location);
		sms->SMS[0].Folder = getfolder;
		sms->SMS[0].Memory = MEM_SM;
		if (getfolder > 2) sms->SMS[0].Memory = MEM_ME;
	}
	return error;
}

GSM_Error ATGEN_GetNextSMS(GSM_StateMachine *s, GSM_MultiSMSMessage *sms, bool start)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error 		error;
	int			usedsms;

	if (Priv->PhoneSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, false);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}
	if (Priv->SIMSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, true);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}
	if (Priv->SIMSMSMemory == AT_NOTAVAILABLE && Priv->PhoneSMSMemory == AT_NOTAVAILABLE) return ERR_NOTSUPPORTED;

	if (start) {
		error=s->Phone.Functions->GetSMSStatus(s,&Priv->LastSMSStatus);
		if (error!=ERR_NONE) return error;
		Priv->LastSMSRead		= 0;
		sms->SMS[0].Location 		= 0;
	}
	while (true) {
		sms->SMS[0].Location++;
		if (sms->SMS[0].Location < PHONE_MAXSMSINFOLDER) {
			if (Priv->SIMSMSMemory == AT_AVAILABLE) {
				usedsms = Priv->LastSMSStatus.SIMUsed;
			} else {
				usedsms = Priv->LastSMSStatus.PhoneUsed;
			}

			if (Priv->LastSMSRead >= usedsms) {
				if (Priv->PhoneSMSMemory == AT_NOTAVAILABLE || Priv->LastSMSStatus.PhoneUsed==0) return ERR_EMPTY;
				Priv->LastSMSRead	= 0;
				sms->SMS[0].Location 	= PHONE_MAXSMSINFOLDER + 1;
			}
		} else {
			if (Priv->PhoneSMSMemory == AT_NOTAVAILABLE) return ERR_EMPTY;
			if (Priv->LastSMSRead>=Priv->LastSMSStatus.PhoneUsed) return ERR_EMPTY;
		}
		sms->SMS[0].Folder = 0;
		error=s->Phone.Functions->GetSMS(s, sms);
		if (error==ERR_NONE) {
			Priv->LastSMSRead++;
			break;
		}
		if (error != ERR_EMPTY && error != ERR_INVALIDLOCATION) return error;
	}
	return error;
}

GSM_Error ATGEN_ReplyGetSMSStatus(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_SMSMemoryStatus	*SMSStatus = s->Phone.Data.SMSStatus;
	char 			*start;
	int			current = 0;
	unsigned char		buffer[50];

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "SMS status received\n");
		start = strstr(msg.Buffer, "+CPMS: ") + 7;
		if (strstr(msg.Buffer,"ME")!=NULL) {
			SMSStatus->PhoneUsed 	= atoi(start);
			current+=ATGEN_ExtractOneParameter(start+current, buffer);
			current+=ATGEN_ExtractOneParameter(start+current, buffer);
			SMSStatus->PhoneSize	= atoi(buffer);
			smprintf(s, "Used : %i\n",SMSStatus->PhoneUsed);
			smprintf(s, "Size : %i\n",SMSStatus->PhoneSize);
		} else {
			SMSStatus->SIMUsed 	= atoi(start);
			current+=ATGEN_ExtractOneParameter(start+current, buffer);
			current+=ATGEN_ExtractOneParameter(start+current, buffer);
			SMSStatus->SIMSize	= atoi(buffer);
			smprintf(s, "Used : %i\n",SMSStatus->SIMUsed);
			smprintf(s, "Size : %i\n",SMSStatus->SIMSize);
			if (SMSStatus->SIMSize == 0) {
				smprintf(s, "Can't access SIM card\n");
				return ERR_SECURITYERROR;
			}
		}
		return ERR_NONE;
	case AT_Reply_Error:
		if (strstr(msg.Buffer,"SM")!=NULL) {
			smprintf(s, "Can't access SIM card\n");
			return ERR_SECURITYERROR;
		}
		return ERR_NOTSUPPORTED;
 	case AT_Reply_CMSError:
		return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetSMSStatus(GSM_StateMachine *s, GSM_SMSMemoryStatus *status)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error 		error;

	/* No templates at all */
	status->TemplatesUsed	= 0;

	status->SIMUsed		= 0;
	status->SIMUnRead 	= 0;
	status->SIMSize		= 0;

	s->Phone.Data.SMSStatus=status;

	if ((Priv->SIMSMSMemory == 0) || (Priv->PhoneSMSMemory == 0)) {
		/* We silently ignore error here, because when this fails, we can try to setmemory anyway */
		ATGEN_GetSMSMemories(s);
	}

	if (Priv->PhoneSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, false);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}
	if (Priv->SIMSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, true);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}

	if (Priv->SIMSMSMemory == AT_AVAILABLE) {
		smprintf(s, "Getting SIM SMS status\n");
		if (Priv->CanSaveSMS) {
			error=GSM_WaitFor (s, "AT+CPMS=\"SM\",\"SM\"\r", 18, 0x00, 4, ID_GetSMSStatus);
		} else {
			error=GSM_WaitFor (s, "AT+CPMS=\"SM\"\r", 13, 0x00, 4, ID_GetSMSStatus);
		}
		if (error!=ERR_NONE) return error;
		Priv->SMSMemory = MEM_SM;
	}

	status->PhoneUsed	= 0;
	status->PhoneUnRead 	= 0;
	status->PhoneSize	= 0;

	if (Priv->PhoneSMSMemory == AT_AVAILABLE) {
		smprintf(s, "Getting phone SMS status\n");
		if (Priv->CanSaveSMS) {
			error = GSM_WaitFor (s, "AT+CPMS=\"ME\",\"ME\"\r", 18, 0x00, 4, ID_GetSMSStatus);
		} else {
			error = GSM_WaitFor (s, "AT+CPMS=\"ME\"\r", 13, 0x00, 4, ID_GetSMSStatus);
		}
		if (error!=ERR_NONE) return error;
		Priv->SMSMemory = MEM_ME;
	}

	return ERR_NONE;
}

GSM_Error ATGEN_ReplyGetIMEI(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	CopyLineString(s->Phone.Data.IMEI, msg.Buffer, s->Phone.Data.Priv.ATGEN.Lines, 2);
	smprintf(s, "Received IMEI %s\n",s->Phone.Data.IMEI);
	return ERR_NONE;
}

GSM_Error ATGEN_GetIMEI (GSM_StateMachine *s)
{
	if (s->Phone.Data.IMEI[0] != 0) return ERR_NONE;
	smprintf(s, "Getting IMEI\n");
	return GSM_WaitFor (s, "AT+CGSN\r", 8, 0x00, 2, ID_GetIMEI);
}

GSM_Error ATGEN_ReplyAddSMSMessage(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	char 	*start;
	int	i;

	if (s->Protocol.Data.AT.EditMode) {
		if (s->Phone.Data.Priv.ATGEN.ReplyState != AT_Reply_SMSEdit) {
			return ATGEN_HandleCMSError(s);
		}
		s->Protocol.Data.AT.EditMode = false;
		return ERR_NONE;
	}

	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "SMS saved OK\n");
		for(i=0;i<msg.Length;i++) {
			if (msg.Buffer[i] == 0x00) msg.Buffer[i] = 0x20;
		}
		start = strstr(msg.Buffer, "+CMGW: ");
		if (start == NULL) return ERR_UNKNOWN;
		s->Phone.Data.SaveSMSMessage->Location = atoi(start+7);
		smprintf(s, "Saved at location %i\n",s->Phone.Data.SaveSMSMessage->Location);
		return ERR_NONE;
	case AT_Reply_Error:
		smprintf(s, "Error\n");
		return ERR_NOTSUPPORTED;
	case AT_Reply_CMSError:
		/* This error occurs in case that phone couldn't save SMS */
		return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_MakeSMSFrame(GSM_StateMachine *s, GSM_SMSMessage *message, unsigned char *hexreq, int *current, int *length2)
{
	GSM_Error 		error;
	int			i, length;
	unsigned char		req[1000], buffer[1000];
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_SMSC	 	SMSC;

	error=ATGEN_GetSMSMode(s);
	if (error != ERR_NONE) return error;

	length 	 = 0;
	*current = 0;
	switch (Priv->SMSMode) {
	case SMS_AT_PDU:
		if (message->PDU == SMS_Deliver) {
 			smprintf(s, "SMS Deliver\n");
			error=PHONE_EncodeSMSFrame(s,message,buffer,PHONE_SMSDeliver,&length,true);
			if (error != ERR_NONE) return error;
			length = length - PHONE_SMSDeliver.Text;
			for (i=0;i<buffer[PHONE_SMSDeliver.SMSCNumber]+1;i++) {
				req[(*current)++]=buffer[PHONE_SMSDeliver.SMSCNumber+i];
			}
			req[(*current)++]=buffer[PHONE_SMSDeliver.firstbyte];
			for (i=0;i<((buffer[PHONE_SMSDeliver.Number]+1)/2+1)+1;i++) {
				req[(*current)++]=buffer[PHONE_SMSDeliver.Number+i];
			}
			req[(*current)++]=buffer[PHONE_SMSDeliver.TPPID];
			req[(*current)++]=buffer[PHONE_SMSDeliver.TPDCS];
			for(i=0;i<7;i++) req[(*current)++]=buffer[PHONE_SMSDeliver.DateTime+i];
			req[(*current)++]=buffer[PHONE_SMSDeliver.TPUDL];
			for(i=0;i<length;i++) req[(*current)++]=buffer[PHONE_SMSDeliver.Text+i];
			EncodeHexBin(hexreq, req, *current);
			*length2 = *current * 2;
			*current = *current - (req[PHONE_SMSDeliver.SMSCNumber]+1);
		} else {
			smprintf(s, "SMS Submit\n");
			error=PHONE_EncodeSMSFrame(s,message,buffer,PHONE_SMSSubmit,&length,true);
			if (error != ERR_NONE) return error;
			length = length - PHONE_SMSSubmit.Text;
			for (i=0;i<buffer[PHONE_SMSSubmit.SMSCNumber]+1;i++) {
				req[(*current)++]=buffer[PHONE_SMSSubmit.SMSCNumber+i];
			}
			req[(*current)++]=buffer[PHONE_SMSSubmit.firstbyte];
			req[(*current)++]=buffer[PHONE_SMSSubmit.TPMR];
			for (i=0;i<((buffer[PHONE_SMSSubmit.Number]+1)/2+1)+1;i++) {
				req[(*current)++]=buffer[PHONE_SMSSubmit.Number+i];
			}
			req[(*current)++]=buffer[PHONE_SMSSubmit.TPPID];
			req[(*current)++]=buffer[PHONE_SMSSubmit.TPDCS];
			req[(*current)++]=buffer[PHONE_SMSSubmit.TPVP];
			req[(*current)++]=buffer[PHONE_SMSSubmit.TPUDL];
			for(i=0;i<length;i++) req[(*current)++]=buffer[PHONE_SMSSubmit.Text+i];
			EncodeHexBin(hexreq, req, *current);
			*length2 = *current * 2;
			*current = *current - (req[PHONE_SMSSubmit.SMSCNumber]+1);
		}
		break;
	case SMS_AT_TXT:
		if (Priv->Manufacturer == 0) {
			error=ATGEN_GetManufacturer(s);
			if (error != ERR_NONE) return error;
		}
		if (Priv->Manufacturer != AT_Nokia) {
			if (message->Coding != SMS_Coding_Default_No_Compression) return ERR_NOTSUPPORTED;
		}
		error=PHONE_EncodeSMSFrame(s,message,req,PHONE_SMSDeliver,&i,true);
		if (error != ERR_NONE) return error;
		CopyUnicodeString(SMSC.Number,message->SMSC.Number);
		SMSC.Location=1;
		error=ATGEN_SetSMSC(s,&SMSC);
		if (error!=ERR_NONE) return error;
		sprintf(buffer, "AT+CSMP=%i,%i,%i,%i\r",
			req[PHONE_SMSDeliver.firstbyte],
			req[PHONE_SMSDeliver.TPVP],
			req[PHONE_SMSDeliver.TPPID],
			req[PHONE_SMSDeliver.TPDCS]);
		error=GSM_WaitFor (s, buffer, strlen(buffer), 0x00, 4, ID_SetSMSParameters);
		if (error==ERR_NOTSUPPORTED) {
			/* Nokia Communicator 9000i doesn't support <vp> parameter */
			sprintf(buffer, "AT+CSMP=%i,,%i,%i\r",
				req[PHONE_SMSDeliver.firstbyte],
				req[PHONE_SMSDeliver.TPPID],
				req[PHONE_SMSDeliver.TPDCS]);
			error=GSM_WaitFor (s, buffer, strlen(buffer), 0x00, 4, ID_SetSMSParameters);
		}
		if (error!=ERR_NONE) return error;
		switch (message->Coding) {
		case SMS_Coding_Default_No_Compression:
			/* If not SMS with UDH, it's as normal text */
			if (message->UDH.Type==UDH_NoUDH) {
				strcpy(hexreq,DecodeUnicodeString(message->Text));
				*length2 = UnicodeLength(message->Text);
				break;
			}
	        case SMS_Coding_Unicode_No_Compression:
	        case SMS_Coding_8bit:
			error=PHONE_EncodeSMSFrame(s,message,buffer,PHONE_SMSDeliver,current,true);
			if (error != ERR_NONE) return error;
			EncodeHexBin (hexreq, buffer+PHONE_SMSDeliver.Text, buffer[PHONE_SMSDeliver.TPUDL]);
			*length2 = buffer[PHONE_SMSDeliver.TPUDL] * 2;
			break;
		default:
			break;
		}
		break;
	}
	return ERR_NONE;
}

GSM_Error ATGEN_AddSMS(GSM_StateMachine *s, GSM_SMSMessage *sms)
{
	GSM_Error 		error, error2;
	int			state,Replies,reply, current, current2;
	unsigned char		buffer[1000], hexreq[1000];
	GSM_Phone_Data		*Phone = &s->Phone.Data;
	unsigned char		*statetxt;

	/* This phone supports only sent/unsent messages on SIM */
	if (IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_SMSONLYSENT)) {
		if (sms->Folder != 2) {
			smprintf(s, "This phone supports only folder = 2!\n");
			return ERR_NOTSUPPORTED;
		}
	}

	sms->PDU = SMS_Submit;
	switch (sms->Folder) {
	case 1:  sms->PDU 	= SMS_Deliver;		/* Inbox SIM */
		 sms->Memory 	= MEM_SM;
		 error=ATGEN_SetSMSMemory(s, true);
		 break;
	case 2:  error=ATGEN_SetSMSMemory(s, true);	/* Outbox SIM */
		 sms->Memory 	= MEM_SM;
	 	 break;
	case 3:  sms->PDU = SMS_Deliver;
		 sms->Memory 	= MEM_ME;
		 error=ATGEN_SetSMSMemory(s, false);	/* Inbox phone */
		 break;
	case 4:  error=ATGEN_SetSMSMemory(s, false);	/* Outbox phone */
		 sms->Memory 	= MEM_ME;
		 break;
	default: return ERR_NOTSUPPORTED;
	}
	if (error!=ERR_NONE) return error;

	error = ATGEN_MakeSMSFrame(s, sms, hexreq, &current, &current2);
	if (error != ERR_NONE) return error;

	switch (Phone->Priv.ATGEN.SMSMode) {
	case SMS_AT_PDU:
		if (sms->PDU == SMS_Deliver) {
			state = 0;
			if (sms->State == SMS_Read || sms->State == SMS_Sent) state = 1;
		} else {
			state = 2;
			if (sms->State == SMS_Read || sms->State == SMS_Sent) state = 3;
		}
		/* Siemens M20 */
		if (IsPhoneFeatureAvailable(Phone->ModelInfo, F_M20SMS)) {
			/* No (good and 100% working) support for alphanumeric numbers */
			if (sms->Number[1]!='+' && (sms->Number[1]<'0' || sms->Number[1]>'9')) {
				EncodeUnicode(sms->Number,"123",3);
				error = ATGEN_MakeSMSFrame(s, sms, hexreq, &current, &current2);
				if (error != ERR_NONE) return error;
			}
		}
		sprintf(buffer, "AT+CMGW=%i,%i\r",current,state);
		break;
	case SMS_AT_TXT:
		if (sms->PDU == SMS_Deliver) {
			statetxt = "REC UNREAD";
			if (sms->State == SMS_Read || sms->State == SMS_Sent) statetxt = "REC READ";
		} else {
			statetxt = "STO UNSENT";
			if (sms->State == SMS_Read || sms->State == SMS_Sent) statetxt = "STO SENT";
		}
		/* Siemens M20 */
		if (IsPhoneFeatureAvailable(Phone->ModelInfo, F_M20SMS)) {
			/* No (good and 100% working) support for alphanumeric numbers */
			/* FIXME: Try to autodetect support for <stat> (statetxt) parameter although:
			 * Siemens M20 supports +CMGW <stat> specification but on my model it just
			 * reports ERROR (and <stat> is not respected).
			 * Fortunately it will write "+CMGW: <index>\n" before and the message gets written
			 */
			if (sms->Number[1]!='+' && (sms->Number[1]<'0' || sms->Number[1]>'9')) {
		        	sprintf(buffer, "AT+CMGW=\"123\",,\"%s\"\r",statetxt);
			} else {
		        	sprintf(buffer, "AT+CMGW=\"%s\",,\"%s\"\r",DecodeUnicodeString(sms->Number),statetxt);
			}
		} else {
			sprintf(buffer, "AT+CMGW=\"%s\",,\"%s\"\r",DecodeUnicodeString(sms->Number),statetxt);
		}
	}

	Phone->SaveSMSMessage = sms;

	for (reply=0;reply<s->ReplyNum;reply++) {
		if (reply!=0) {
			if (s->di.dl==DL_TEXT || s->di.dl==DL_TEXTALL || s->di.dl==DL_TEXTERROR ||
			    s->di.dl==DL_TEXTDATE || s->di.dl==DL_TEXTALLDATE || s->di.dl==DL_TEXTERRORDATE) {
			    smprintf(s, "[Retrying %i]\n", reply+1);
			}
		}
		s->Protocol.Data.AT.EditMode 	= true;
		Replies 			= s->ReplyNum;
		s->ReplyNum			= 1;
		smprintf(s,"Waiting for modem prompt\n");
		error=GSM_WaitFor (s, buffer, strlen(buffer), 0x00, 3, ID_SaveSMSMessage);
		s->ReplyNum			 = Replies;
		if (error == ERR_NONE) {
			Phone->DispatchError 	= ERR_TIMEOUT;
			Phone->RequestID 	= ID_SaveSMSMessage;
			smprintf(s, "Saving SMS\n");
			error = s->Protocol.Functions->WriteMessage(s, hexreq, current2, 0x00);
			if (error!=ERR_NONE) return error;
			my_sleep(500);
			/* CTRL+Z ends entering */
			error = s->Protocol.Functions->WriteMessage(s, "\x1A", 1, 0x00);
			if (error!=ERR_NONE) return error;
			error = GSM_WaitForOnce(s, NULL, 0x00, 0x00, 4);
			if (error != ERR_TIMEOUT) return error;
		} else {
			smprintf(s, "Escaping SMS mode\n");
			error2 = s->Protocol.Functions->WriteMessage(s, "\x1B\r", 2, 0x00);
			if (error2 != ERR_NONE) return error2;
			return error;
		}
        }

	return Phone->DispatchError;
}

GSM_Error ATGEN_ReplySendSMS(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	char			*start;

	if (s->Protocol.Data.AT.EditMode) {
		if (s->Phone.Data.Priv.ATGEN.ReplyState != AT_Reply_SMSEdit) {
			return ERR_UNKNOWN;
		}
		s->Protocol.Data.AT.EditMode = false;
		return ERR_NONE;
	}

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
 		smprintf(s, "SMS sent OK\n");
 		if (s->User.SendSMSStatus!=NULL) {
			start = strstr(msg.Buffer, "+CMGS: ");
			if (start != NULL) {
				s->User.SendSMSStatus(s->CurrentConfig->Device,0,atoi(start+7));
			} else {
				s->User.SendSMSStatus(s->CurrentConfig->Device,0,-1);
			}
		}
		return ERR_NONE;
	case AT_Reply_CMSError:
 		smprintf(s, "Error %i\n",Priv->ErrorCode);
 		if (s->User.SendSMSStatus != NULL) {
			s->User.SendSMSStatus(s->CurrentConfig->Device, Priv->ErrorCode, -1);
		}
 		return ATGEN_HandleCMSError(s);
	case AT_Reply_Error:
 		if (s->User.SendSMSStatus != NULL) {
			s->User.SendSMSStatus(s->CurrentConfig->Device, -1, -1);
		}
		return ERR_UNKNOWN;
	default:
 		if (s->User.SendSMSStatus != NULL) {
			s->User.SendSMSStatus(s->CurrentConfig->Device, -1, -1);
		}
		return ERR_UNKNOWNRESPONSE;
	}
}

GSM_Error ATGEN_SendSMS(GSM_StateMachine *s, GSM_SMSMessage *sms)
{
	GSM_Error 		error,error2;
	int			current, current2, Replies;
	unsigned char		buffer[1000], hexreq[1000];
	GSM_Phone_Data		*Phone = &s->Phone.Data;

	if (sms->PDU == SMS_Deliver) sms->PDU = SMS_Submit;

	error = ATGEN_MakeSMSFrame(s, sms, hexreq, &current, &current2);
	if (error != ERR_NONE) return error;

	switch (Phone->Priv.ATGEN.SMSMode) {
	case SMS_AT_PDU:
		sprintf(buffer, "AT+CMGS=%i\r",current);
		break;
	case SMS_AT_TXT:
		sprintf(buffer, "AT+CMGS=\"%s\"\r",DecodeUnicodeString(sms->Number));
	}

	s->Protocol.Data.AT.EditMode 	= true;
	Replies 			= s->ReplyNum;
	s->ReplyNum			= 1;
	smprintf(s,"Waiting for modem prompt\n");
	error=GSM_WaitFor (s, buffer, strlen(buffer), 0x00, 3, ID_IncomingFrame);
	s->ReplyNum			 = Replies;
	if (error == ERR_NONE) {
		smprintf(s, "Sending SMS\n");
		error = s->Protocol.Functions->WriteMessage(s, hexreq, current2, 0x00);
		if (error!=ERR_NONE) return error;
		my_sleep(500);
		/* CTRL+Z ends entering */
		error=s->Protocol.Functions->WriteMessage(s, "\x1A", 1, 0x00);
		my_sleep(100);
		return error;
	} else {
		smprintf(s, "Escaping SMS mode\n");
		error2=s->Protocol.Functions->WriteMessage(s, "\x1B\r", 2, 0x00);
		if (error2 != ERR_NONE) return error2;
	}
	return error;
}

GSM_Error ATGEN_SendSavedSMS(GSM_StateMachine *s, int Folder, int Location)
{
	GSM_Error 	error;
	int		location;
	unsigned char	smsfolder;
	unsigned char	req[100];
	GSM_MultiSMSMessage	msms;

	msms.Number = 0;
	msms.SMS[0].Folder 	= Folder;
	msms.SMS[0].Location 	= Location;

	/* By reading SMS we check if it is really inbox/outbox */
	error = ATGEN_GetSMS(s, &msms);
	if (error != ERR_NONE) return error;

	/* Can not send from other folder that outbox */
	if (msms.SMS[0].Folder != 2 && msms.SMS[0].Folder != 4) return ERR_NOTSUPPORTED;

	error=ATGEN_GetSMSLocation(s, &msms.SMS[0], &smsfolder, &location);
	if (error != ERR_NONE) return error;

	sprintf(req, "AT+CMSS=%i\r",location);
	return s->Protocol.Functions->WriteMessage(s, req, strlen(req), 0x00);
}

GSM_Error ATGEN_ReplyGetDateTime_Alarm(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_Data		*Data = &s->Phone.Data;
	unsigned char		*pos;
	unsigned char		buffer[100];

	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		pos = strchr(msg.Buffer, ':');
		if (pos == NULL) {
			smprintf(s, "Not set in phone\n");
			return ERR_EMPTY;
		}
		pos++;
		while (isspace(*pos)) pos++;
		ATGEN_ExtractOneParameter(pos, buffer);
		return ATGEN_DecodeDateTime(s, (Data->RequestID == ID_GetDateTime) ? Data->DateTime : &(Data->Alarm->DateTime), buffer);
	case AT_Reply_Error:
		return ERR_NOTSUPPORTED;
	case AT_Reply_CMSError:
		return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetDateTime(GSM_StateMachine *s, GSM_DateTime *date_time)
{
	GSM_Error		error;
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;

	/* If phone encodes also values in command, we need normal charset */
	if (Priv->EncodedCommands) {
		error = ATGEN_SetCharset(s, AT_PREF_CHARSET_NORMAL);
		if (error != ERR_NONE) return error;
	}

	s->Phone.Data.DateTime=date_time;
	smprintf(s, "Getting date & time\n");
	return GSM_WaitFor (s, "AT+CCLK?\r", 9, 0x00, 4, ID_GetDateTime);
}

GSM_Error ATGEN_PrivSetDateTime(GSM_StateMachine *s, GSM_DateTime *date_time, bool set_timezone)
{
	char			tz[4] = "";
	char			req[128];
	GSM_Error		error;

	if (set_timezone) {
		sprintf(tz, "+%02i", date_time->Timezone);
	}

	sprintf(req, "AT+CCLK=\"%02i/%02i/%02i,%02i:%02i:%02i%s\"\r",
		     (date_time->Year > 2000 ? date_time->Year-2000 : date_time->Year-1900),
		     date_time->Month ,
		     date_time->Day,
		     date_time->Hour,
		     date_time->Minute,
		     date_time->Second,
		     tz);

	smprintf(s, "Setting date & time\n");

	error = GSM_WaitFor (s, req, strlen(req), 0x00, 4, ID_SetDateTime);

	if (error == ERR_INVALIDDATA && set_timezone
		&& (s->Phone.Data.Priv.ATGEN.ReplyState == AT_Reply_CMEError)
		&& (s->Phone.Data.Priv.ATGEN.ErrorCode == 24)) {
		/* Some firmwares of Ericsson R320s don't like the timezone part,
		even though it is in its command reference. */
		smprintf(s, "Retrying without timezone suffix\n");
		error = ATGEN_PrivSetDateTime(s, date_time, false);
	}
	return error;
}

GSM_Error ATGEN_SetDateTime(GSM_StateMachine *s, GSM_DateTime *date_time)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error		error;

	/* If phone encodes also values in command, we need normal charset */
	if (Priv->EncodedCommands) {
		error = ATGEN_SetCharset(s, AT_PREF_CHARSET_NORMAL);
		if (error != ERR_NONE) return error;
	}
	return ATGEN_PrivSetDateTime(s, date_time, true);
}

GSM_Error ATGEN_GetAlarm(GSM_StateMachine *s, GSM_Alarm *alarm)
{
	GSM_Error		error;
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (alarm->Location != 1) return ERR_NOTSUPPORTED;

	/* If phone encodes also values in command, we need normal charset */
	if (Priv->EncodedCommands) {
		error = ATGEN_SetCharset(s, AT_PREF_CHARSET_NORMAL);
		if (error != ERR_NONE) return error;
	}

	alarm->Repeating = true;
	alarm->Text[0] = 0; alarm->Text[1] = 0;

	s->Phone.Data.Alarm = alarm;
	smprintf(s, "Getting alarm\n");
	return GSM_WaitFor (s, "AT+CALA?\r", 9, 0x00, 4, ID_GetAlarm);
}

/* R320 only takes HH:MM. Do other phones understand full date? */
GSM_Error ATGEN_SetAlarm(GSM_StateMachine *s, GSM_Alarm *alarm)
{
	char			req[20];
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error		error;

	if (alarm->Location != 1) return ERR_INVALIDLOCATION;

	/* If phone encodes also values in command, we need normal charset */
	if (Priv->EncodedCommands) {
		error = ATGEN_SetCharset(s, AT_PREF_CHARSET_NORMAL);
		if (error != ERR_NONE) return error;
	}

	sprintf(req, "AT+CALA=\"%02i:%02i\"\r",alarm->DateTime.Hour,alarm->DateTime.Minute);

	smprintf(s, "Setting Alarm\n");
	return GSM_WaitFor (s, req, strlen(req), 0x00, 3, ID_SetAlarm);
}

GSM_Error ATGEN_ReplyGetSMSC(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_SMSC		*SMSC = s->Phone.Data.SMSC;
	int			current;
	int			len;
	unsigned char 		buffer[100];

	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "SMSC info received\n");

		current = 0;
		while (msg.Buffer[current]!='"') current++;

		/* SMSC number */
		/* FIXME: support for all formats */
		current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
		/*
		 * Some phones return this as unicode encoded when they are
		 * switched to UCS2 mode, so we try to solve this correctly.
		 */
		len 		= strlen(buffer + 1) - 1;
		buffer[len + 1] = 0;
		if ((len > 20) && (len % 4 == 0) && (strchr(buffer + 1, '+') == NULL)) {
			/* This is probably unicode encoded number */
			DecodeHexUnicode(SMSC->Number,buffer + 1,len);
		} else  {
			EncodeUnicode(SMSC->Number,buffer + 1,len);
		}
		smprintf(s, "Number: \"%s\"\n",DecodeUnicodeString(SMSC->Number));

		/* Format of SMSC number */
		current+=ATGEN_ExtractOneParameter(msg.Buffer+current, buffer);
		smprintf(s, "Format %s\n",buffer);

		/* International number */
		ATGEN_TweakInternationalNumber(SMSC->Number,buffer);

		SMSC->Format 		= SMS_FORMAT_Text;
		SMSC->Validity.Format = SMS_Validity_RelativeFormat;
		SMSC->Validity.Relative	= SMS_VALID_Max_Time;
		SMSC->Name[0]		= 0;
		SMSC->Name[1]		= 0;
		SMSC->DefaultNumber[0]	= 0;
		SMSC->DefaultNumber[1]	= 0;
		return ERR_NONE;
	case AT_Reply_CMSError:
		return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetSMSC(GSM_StateMachine *s, GSM_SMSC *smsc)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error		error;

	if (smsc->Location==0x00 || smsc->Location!=0x01) return ERR_INVALIDLOCATION;

	/* If phone encodes also values in command, we need normal charset */
	if (Priv->EncodedCommands) {
		error = ATGEN_SetCharset(s, AT_PREF_CHARSET_NORMAL);
		if (error != ERR_NONE) return error;
	}

	s->Phone.Data.SMSC=smsc;
	smprintf(s, "Getting SMSC\n");
	return GSM_WaitFor (s, "AT+CSCA?\r", 9, 0x00, 4, ID_GetSMSC);
}

GSM_Error ATGEN_ReplyGetNetworkLAC_CID(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_NetworkInfo		*NetworkInfo = s->Phone.Data.NetworkInfo;
	GSM_Lines		Lines;
	int			i=0;
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	char			*answer;
	char			*tmp;

  	if (s->Phone.Data.RequestID == ID_IncomingFrame) {
		smprintf(s, "Incoming LAC & CID info\n");
		return ERR_NONE;
	}

	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		break;
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
	default:
		return ERR_UNKNOWNRESPONSE;
	}

	SplitLines(GetLineString(msg.Buffer,Priv->Lines,2),
		strlen(GetLineString(msg.Buffer,Priv->Lines,2)),
		&Lines, ",", 1, true);

	/* Find number of lines */
	while (Lines.numbers[i*2+1] != 0) {
		/* FIXME: handle special chars correctly */
		tmp = strdup(GetLineString(msg.Buffer,Priv->Lines,2));
		smprintf(s, "%i \"%s\"\n",i+1,GetLineString(tmp,Lines,i+1));
		free(tmp);
		i++;
	}

	smprintf(s, "Network LAC & CID & state received\n");
	tmp = strdup(GetLineString(msg.Buffer,Priv->Lines,2));
	answer = GetLineString(tmp,Lines,2);
	free(tmp);
	while (*answer == 0x20) answer++;
#ifdef DEBUG
	switch (answer[0]) {
		case '0': smprintf(s, "Not registered into any network. Not searching for network\n"); 	  break;
		case '1': smprintf(s, "Home network\n"); 						  break;
		case '2': smprintf(s, "Not registered into any network. Searching for network\n"); 	  break;
		case '3': smprintf(s, "Registration denied\n"); 					  break;
		case '4': smprintf(s, "Unknown\n"); 							  break;
		case '5': smprintf(s, "Registered in roaming network\n"); 				  break;
		default : smprintf(s, "Unknown\n");
	}
#endif
	switch (answer[0]) {
		case '0': NetworkInfo->State = GSM_NoNetwork;		break;
		case '1': NetworkInfo->State = GSM_HomeNetwork; 	break;
		case '2': NetworkInfo->State = GSM_RequestingNetwork; 	break;
		case '3': NetworkInfo->State = GSM_RegistrationDenied;	break;
		case '4': NetworkInfo->State = GSM_NetworkStatusUnknown;break;
		case '5': NetworkInfo->State = GSM_RoamingNetwork; 	break;
		default : NetworkInfo->State = GSM_NetworkStatusUnknown;break;
	}
	if (NetworkInfo->State == GSM_HomeNetwork ||
	    NetworkInfo->State == GSM_RoamingNetwork) {
		memset(NetworkInfo->CID,0,4);
		memset(NetworkInfo->LAC,0,4);

		if (Lines.numbers[3*2+1]==0) return ERR_NONE;

 		tmp = strdup(GetLineString(msg.Buffer,Priv->Lines,2));
 		answer = GetLineString(tmp,Lines,3);
 		free(tmp);
		while (*answer == 0x20) answer++;
		sprintf(NetworkInfo->LAC,	"%c%c%c%c", answer[1], answer[2], answer[3], answer[4]);

 		tmp = strdup(GetLineString(msg.Buffer,Priv->Lines,2));
 		answer = GetLineString(tmp,Lines,4);
 		free(tmp);
		while (*answer == 0x20) answer++;
		sprintf(NetworkInfo->CID,	"%c%c%c%c", answer[1], answer[2], answer[3], answer[4]);

		smprintf(s, "LAC   : %s\n",NetworkInfo->LAC);
		smprintf(s, "CID   : %s\n",NetworkInfo->CID);
	}
	return ERR_NONE;
}

GSM_Error ATGEN_ReplyGetNetworkCode(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_NetworkInfo		*NetworkInfo = s->Phone.Data.NetworkInfo;

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "Network code received\n");
		if (Priv->Manufacturer == AT_Falcom) {
			NetworkInfo->NetworkCode[0] = msg.Buffer[22];
			NetworkInfo->NetworkCode[1] = msg.Buffer[23];
			NetworkInfo->NetworkCode[2] = msg.Buffer[24];
			NetworkInfo->NetworkCode[3] = ' ';
			NetworkInfo->NetworkCode[4] = msg.Buffer[25];
			NetworkInfo->NetworkCode[5] = msg.Buffer[26];
		} else {
			NetworkInfo->NetworkCode[0] = msg.Buffer[23];
			NetworkInfo->NetworkCode[1] = msg.Buffer[24];
			NetworkInfo->NetworkCode[2] = msg.Buffer[25];
			NetworkInfo->NetworkCode[3] = ' ';
			NetworkInfo->NetworkCode[4] = msg.Buffer[26];
			NetworkInfo->NetworkCode[5] = msg.Buffer[27];
		}
		NetworkInfo->NetworkCode[6] = 0;
		smprintf(s, "   Network code              : %s\n", NetworkInfo->NetworkCode);
		smprintf(s, "   Network name for Gammu    : %s ",
			DecodeUnicodeString(GSM_GetNetworkName(NetworkInfo->NetworkCode)));
		smprintf(s, "(%s)\n",DecodeUnicodeString(GSM_GetCountryName(NetworkInfo->NetworkCode)));
		return ERR_NONE;
	case AT_Reply_CMSError:
		return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetNetworkInfo(GSM_StateMachine *s, GSM_NetworkInfo *netinfo)
{
	GSM_Error error;

	s->Phone.Data.NetworkInfo=netinfo;

	netinfo->NetworkName[0] = 0;
	netinfo->NetworkName[1] = 0;
	netinfo->NetworkCode[0] = 0;

	smprintf(s, "Enable full network info\n");
	error=GSM_WaitFor(s, "AT+CREG=2\r", 10, 0x00, 4, ID_GetNetworkInfo);
	if ((error != ERR_NONE) &&
	    (s->Phone.Data.Priv.ATGEN.Manufacturer!=AT_Siemens) &&
	    (s->Phone.Data.Priv.ATGEN.Manufacturer!=AT_Ericsson)) return error;

	smprintf(s, "Getting network LAC and CID and state\n");
	error=GSM_WaitFor(s, "AT+CREG?\r", 9, 0x00, 4, ID_GetNetworkInfo);
	if (error != ERR_NONE) return error;

	if (netinfo->State == GSM_HomeNetwork || netinfo->State == GSM_RoamingNetwork) {
		smprintf(s, "Setting short network name format\n");
		error=GSM_WaitFor(s, "AT+COPS=3,2\r", 12, 0x00, 4, ID_GetNetworkInfo);

		error=ATGEN_GetManufacturer(s);
		if (error != ERR_NONE) return error;

		smprintf(s, "Getting network code\n");
		error=GSM_WaitFor(s, "AT+COPS?\r", 9, 0x00, 4, ID_GetNetworkInfo);
	}
	return error;
}

GSM_Error ATGEN_ReplyGetPBKMemories(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	smprintf(s, "PBK memories received\n");
	if (strlen(msg.Buffer) > AT_PBK_MAX_MEMORIES) {
		smprintf(s, "ERROR: Too long phonebook memories information received! (Recevided %zd, AT_PBK_MAX_MEMORIES is %d\n", strlen(msg.Buffer), AT_PBK_MAX_MEMORIES);
		return ERR_MOREMEMORY;
	}
	memcpy(s->Phone.Data.Priv.ATGEN.PBKMemories,msg.Buffer,strlen(msg.Buffer));
	return ERR_NONE;
}

GSM_Error ATGEN_SetPBKMemory(GSM_StateMachine *s, GSM_MemoryType MemType)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	char 			req[] = "AT+CPBS=\"XX\"\r";
	GSM_Error		error;

	if (Priv->PBKMemory == MemType) return ERR_NONE;

	/* Zero values that are for actual memory */
	Priv->MemorySize		= 0;
	Priv->FirstMemoryEntry		= -1;
	Priv->NextMemoryEntry		= 0;
	Priv->TextLength		= 0;
	Priv->NumberLength		= 0;

	/* If phone encodes also values in command, we need normal charset */
	if (Priv->EncodedCommands) {
		error = ATGEN_SetCharset(s, AT_PREF_CHARSET_NORMAL);
		if (error != ERR_NONE) return error;
	}

	if (Priv->PBKMemories[0] == 0) {
		error=GSM_WaitFor (s, "AT+CPBS=?\r", 10, 0x00, 3, ID_SetMemoryType);
		if (error != ERR_NONE) return error;
	}

	switch (MemType) {
		case MEM_SM:
			req[9] = 'S'; req[10] = 'M';
			break;
		case MEM_ME:
		        if (strstr(Priv->PBKMemories,"ME")==NULL) return ERR_NOTSUPPORTED;
			req[9] = 'M'; req[10] = 'E';
			break;
		case MEM_RC:
		        if (strstr(Priv->PBKMemories,"RC")==NULL) return ERR_NOTSUPPORTED;
			req[9] = 'R'; req[10] = 'C';
			break;
		case MEM_MC:
		        if (strstr(Priv->PBKMemories,"MC")==NULL) return ERR_NOTSUPPORTED;
			req[9] = 'M'; req[10] = 'C';
			break;
		case MEM_ON:
		        if (strstr(Priv->PBKMemories,"ON")==NULL) return ERR_NOTSUPPORTED;
			req[9] = 'O'; req[10] = 'N';
			break;
		case MEM_FD:
		        if (strstr(Priv->PBKMemories,"FD")==NULL) return ERR_NOTSUPPORTED;
			req[9] = 'F'; req[10] = 'D';
			break;
		case MEM_DC:
			if (strstr(Priv->PBKMemories,"DC")!=NULL) {
				req[9] = 'D'; req[10] = 'C';
				break;
			}
			if (strstr(Priv->PBKMemories,"LD")!=NULL) {
				req[9] = 'L'; req[10] = 'D';
				break;
			}
			return ERR_NOTSUPPORTED;
			break;
		default:
			return ERR_NOTSUPPORTED;
	}

	smprintf(s, "Setting memory type\n");
	error=GSM_WaitFor (s, req, 13, 0x00, 3, ID_SetMemoryType);
	if (error == ERR_NONE) Priv->PBKMemory = MemType;
	return error;
}

GSM_Error ATGEN_ReplyGetCPBSMemoryStatus(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_MemoryStatus	*MemoryStatus = s->Phone.Data.MemoryStatus;
	char 			*start;

	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "Memory status received\n");
  		MemoryStatus->MemoryUsed = 0;
		MemoryStatus->MemoryFree = 0;
		start = strchr(msg.Buffer, ',');
		if (start) {
			start++;
			MemoryStatus->MemoryUsed = atoi(start);
			start = strchr(start, ',');
			if (start) {
				start++;
				MemoryStatus->MemoryFree = atoi(start) - MemoryStatus->MemoryUsed;
				return ERR_NONE;
			} else return ERR_UNKNOWN;
		} else return ERR_UNKNOWN;
	case AT_Reply_CMSError:
		return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_ReplyGetCPBRMemoryInfo(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
 	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	char 			*pos;

 	switch (Priv->ReplyState) {
 	case AT_Reply_OK:
		smprintf(s, "Memory info received\n");
 		/* Parse +CPBR: (first-last),max_number_len,max_name_len */
 		/* Some phones (eg. Motorola C350) reply is different:
		   +CPBR: first-last,max_number_len,max_name_len */

		/* Parse first location */
		pos = strchr(msg.Buffer, '(');
 		if (!pos) {
 			pos = strchr(msg.Buffer, ':');
 			if (!pos) return ERR_UNKNOWNRESPONSE;
 			pos++;
 			if (*pos ==  ' ') pos++;
 			if (!isdigit(*pos)) return ERR_UNKNOWNRESPONSE;
 		} else {
 			pos++;
 		}
		Priv->FirstMemoryEntry = atoi(pos);

		/* Parse last location*/
		pos = strchr(pos, '-');
 		if (!pos) return ERR_UNKNOWNRESPONSE;
		pos++;
		Priv->MemorySize = atoi(pos) + 1 - Priv->FirstMemoryEntry;

		/* Parse number length*/
		pos = strchr(pos, ',');
 		if (!pos) return ERR_UNKNOWNRESPONSE;
		pos++;
		Priv->NumberLength = atoi(pos);

		/* Parse text length*/
		pos = strchr(pos, ',');
 		if (!pos) return ERR_UNKNOWNRESPONSE;
		pos++;
		Priv->TextLength = atoi(pos);

		return ERR_NONE;
	case AT_Reply_Error:
		return ERR_UNKNOWN;
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
 	default:
		return ERR_UNKNOWNRESPONSE;
	}
}

GSM_Error ATGEN_ReplyGetCPBRMemoryStatus(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_MemoryStatus	*MemoryStatus = s->Phone.Data.MemoryStatus;
	int			line=0;
	char			*str;
	int			cur, last = -1;

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "Memory entries received\n");
		/* Walk through lines with +CPBR: */
		while (Priv->Lines.numbers[line*2+1]!=0) {
			str = GetLineString(msg.Buffer,Priv->Lines,line+1);
			if (strncmp(str, "+CPBR: ", 7) == 0) {
				if (sscanf(str, "+CPBR: %d,", &cur) == 1) {
					/* Some phones wrongly return several lines with same location,
					 * we need to catch it here to get correct count. */
					if (cur != last) {
						MemoryStatus->MemoryUsed++;
					}
					last = cur;
					cur -= Priv->FirstMemoryEntry - 1;
					if (cur == Priv->NextMemoryEntry || Priv->NextMemoryEntry == 0)
						Priv->NextMemoryEntry = cur + 1;
				} else {
					MemoryStatus->MemoryUsed++;
				}
			}
			line++;
		}
		return ERR_NONE;
	case AT_Reply_Error:
		return ERR_UNKNOWN;
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
	default:
		return ERR_UNKNOWNRESPONSE;
	}
}

GSM_Error ATGEN_GetMemoryInfo(GSM_StateMachine *s, GSM_MemoryStatus *Status, GSM_AT_NeededMemoryInfo NeededInfo)
{
	GSM_Error		error;
	char			req[20];
	int			start;
	int			end;
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;

	smprintf(s, "Getting memory information\n");

	Priv->MemorySize		= 0;
	Priv->TextLength		= 0;
	Priv->NumberLength		= 0;

	error = GSM_WaitFor (s, "AT+CPBR=?\r", 10, 0x00, 4, ID_GetMemoryStatus);
	if (Priv->Manufacturer == AT_Samsung)
		error = GSM_WaitFor (s, "", 0, 0x00, 4, ID_GetMemoryStatus);
	if (error != ERR_NONE) return error;
	if (NeededInfo == AT_Total || NeededInfo == AT_Sizes || NeededInfo == AT_First) return ERR_NONE;

	smprintf(s, "Getting memory status by reading values\n");

	s->Phone.Data.MemoryStatus	= Status;
	Status->MemoryUsed		= 0;
	Status->MemoryFree		= 0;
	start				= Priv->FirstMemoryEntry;
	Priv->NextMemoryEntry		= 0;
	while (1) {
		end	= start + 20;
		if (end > Priv->MemorySize) end = Priv->MemorySize;
		sprintf(req, "AT+CPBR=%i,%i\r", start, end);
		error	= GSM_WaitFor (s, req, strlen(req), 0x00, 4, ID_GetMemoryStatus);
		if (error != ERR_NONE) return error;
		if (NeededInfo == AT_NextEmpty && Priv->NextMemoryEntry != 0 && Priv->NextMemoryEntry != end + 1) return ERR_NONE;
		if (end == Priv->MemorySize) {
			Status->MemoryFree = Priv->MemorySize - Status->MemoryUsed;
			return ERR_NONE;
		}
		start = end + 1;
	}
}

GSM_Error ATGEN_GetMemoryStatus(GSM_StateMachine *s, GSM_MemoryStatus *Status)
{
	GSM_Error		error;
 	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;

	error = ATGEN_SetPBKMemory(s, Status->MemoryType);
	if (error != ERR_NONE) return error;

	s->Phone.Data.MemoryStatus=Status;

	/* in some phones doesn't work or doesn't return memory status inside */
	/* Some workaround for buggy mobile, that hangs after "AT+CPBS?" for other
	 * memory than SM.
	 */
	if (!IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_BROKENCPBS) || (Status->MemoryType == MEM_SM)) {
		smprintf(s, "Getting memory status\n");
		error=GSM_WaitFor (s, "AT+CPBS?\r", 9, 0x00, 4, ID_GetMemoryStatus);
		if (error == ERR_NONE) return ERR_NONE;
	}

	/* Catch errorneous 0 returned by some Siemens phones for ME. There is
	 * probably no way to get status there. */
	if (Priv->PBKSBNR == AT_SBNR_AVAILABLE && Status->MemoryType == MEM_ME && Status->MemoryFree == 0)
		return ERR_NOTSUPPORTED;

	return ATGEN_GetMemoryInfo(s, Status, AT_Status);
}

GSM_Error ATGEN_ReplyGetMemory(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
 	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
 	GSM_MemoryEntry		*Memory = s->Phone.Data.Memory;
	char			*pos;
	unsigned char		buffer[500],buffer2[500];
	int			len;
	int			offset;

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
 		smprintf(s, "Phonebook entry received\n");
 		Memory->EntriesNum = 0;
		if (Priv->Lines.numbers[4]==0) return ERR_EMPTY;
		pos = strstr(msg.Buffer, "+CPBR:");
		if (pos == NULL) return ERR_UNKNOWN;
		/* Go after +CPBR: */
		pos += 6;

		/* Location */
		while (*pos && !isdigit(*pos)) pos++;
		Memory->Location = atoi(pos) + 1 - Priv->FirstMemoryEntry;
 		smprintf(s, "Location: %d\n", Memory->Location);

		/* Number */
		while (*pos != '"') pos++;
		pos += ATGEN_ExtractOneParameter(pos, buffer);
 		smprintf(s, "Number: %s\n",buffer);
 		Memory->EntriesNum++;
 		Memory->Entries[0].EntryType  = PBK_Number_General;
 		Memory->Entries[0].VoiceTag   = 0;
 		Memory->Entries[0].SMSList[0] = 0;

		len = strlen(buffer + 1) - 1;
		if (Priv->Charset == AT_CHARSET_HEX && (len > 10) && (len % 2 == 0) && (strchr(buffer + 1, '+') == NULL)) {
			/* This is probably hex encoded number */
			DecodeHexBin(buffer2, buffer+1, len);
			DecodeDefault(Memory->Entries[0].Text ,buffer2, strlen(buffer2), false, NULL);
		} else if (Priv->Charset == AT_CHARSET_UCS2 && (len > 20) && (len % 4 == 0) && (strchr(buffer + 1, '+') == NULL)) {
			/* This is probably unicode encoded number */
			DecodeHexUnicode(Memory->Entries[0].Text, buffer + 1,len);
		} else  {
	 		EncodeUnicode(Memory->Entries[0].Text, buffer + 1, len);
		}

		/* Number format */
		pos += ATGEN_ExtractOneParameter(pos, buffer);
 		smprintf(s, "Number format: %s\n",buffer);

		/* International number */
		ATGEN_TweakInternationalNumber(Memory->Entries[0].Text,buffer);

		/* Name */
		pos += ATGEN_ExtractOneParameter(pos, buffer);
 		smprintf(s, "Name text: %s\n",buffer);

 		/* Some phones (Motorola) don't put name iniside quotes */
 		if (buffer[0] == '"') offset = 1;
 		else offset = 0;

 		Memory->EntriesNum++;
 		Memory->Entries[1].EntryType=PBK_Text_Name;

		switch (Priv->Charset) {
		case AT_CHARSET_HEX:
 			DecodeHexBin(buffer2, buffer + offset, strlen(buffer) - (offset * 2));
   			DecodeDefault(Memory->Entries[1].Text,buffer2,strlen(buffer2),false,NULL);
  			break;
  		case AT_CHARSET_GSM:
  			DecodeDefault(Memory->Entries[1].Text, buffer + offset, strlen(buffer) - (offset * 2), false, NULL);
  			break;
  		case AT_CHARSET_UCS2:
 			DecodeHexUnicode(Memory->Entries[1].Text, buffer + offset, strlen(buffer) - (offset * 2));
  			break;
  		case AT_CHARSET_IRA: /* IRA is ASCII only, so it's safe to treat is as UTF-8 */
  		case AT_CHARSET_UTF8:
 			DecodeUTF8(Memory->Entries[1].Text, buffer + offset, strlen(buffer) - (offset * 2));
  			break;
  		case AT_CHARSET_PCCP437:
  			/* FIXME: correctly decode PCCP437 */
  			DecodeDefault(Memory->Entries[1].Text, buffer + offset, strlen(buffer) - (offset * 2), false, NULL);
			break;
		}

		/* Samsung number type */
		if (Priv->Manufacturer == AT_Samsung) {
			int type;

			pos += ATGEN_ExtractOneParameter(pos, buffer);
 			smprintf(s, "Number type: %s\n",buffer);
			type = strtoul(buffer, NULL, 0);
			switch (type) {
			case 0:
 				Memory->Entries[0].EntryType = PBK_Number_Mobile;
				break;
			case 1:
 				Memory->Entries[0].EntryType = PBK_Number_Work;
				break;
			case 2:
 				Memory->Entries[0].EntryType = PBK_Number_Home;
				break;
			case 3:
 				Memory->Entries[0].EntryType = PBK_Text_Email;
				break;
			default:
 				Memory->Entries[0].EntryType = PBK_Number_General;
			}
		}

		return ERR_NONE;
	case AT_Reply_CMEError:
		return ATGEN_HandleCMEError(s);
	case AT_Reply_Error:
 		smprintf(s, "Error - too high location ?\n");
		return ERR_INVALIDLOCATION;
	case AT_Reply_CMSError:
 	        return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_PrivGetMemory (GSM_StateMachine *s, GSM_MemoryEntry *entry, int endlocation)
{
	GSM_Error 		error;
	unsigned char		req[20];
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (entry->Location==0x00) return ERR_INVALIDLOCATION;

	if (entry->MemoryType == MEM_ME) {
		if (Priv->PBKSBNR == 0) {
			sprintf(req, "AT^SBNR=?\r");
			smprintf(s, "Checking availablity of SBNR\n");
			GSM_WaitFor (s, req, strlen(req), 0x00, 4, ID_GetMemory);
		}
		if (Priv->PBKSBNR == AT_SBNR_AVAILABLE) {
			sprintf(req, "AT^SBNR=vcf,%i\r",entry->Location + Priv->FirstMemoryEntry - 1);
			s->Phone.Data.Memory=entry;
			smprintf(s, "Getting phonebook entry\n");
			return GSM_WaitFor (s, req, strlen(req), 0x00, 4, ID_GetMemory);
		}
	}

	error=ATGEN_GetManufacturer(s);
	if (error != ERR_NONE) return error;

	error=ATGEN_SetPBKMemory(s, entry->MemoryType);
	if (error != ERR_NONE) return error;

	if (Priv->FirstMemoryEntry == -1) {
		error = ATGEN_GetMemoryInfo(s, NULL, AT_First);
		if (error != ERR_NONE) return error;
	}


	error=ATGEN_SetCharset(s, AT_PREF_CHARSET_UNICODE); /* For reading we prefer unicode */
	if (error != ERR_NONE) return error;

	if (endlocation == 0) {
		sprintf(req, "AT+CPBR=%i\r", entry->Location + Priv->FirstMemoryEntry - 1);
	} else {
		sprintf(req, "AT+CPBR=%i,%i\r", entry->Location + Priv->FirstMemoryEntry - 1, endlocation + Priv->FirstMemoryEntry - 1);
	}

	s->Phone.Data.Memory=entry;
	smprintf(s, "Getting phonebook entry\n");
	return GSM_WaitFor (s, req, strlen(req), 0x00, 4, ID_GetMemory);
}

GSM_Error ATGEN_GetMemory (GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	return ATGEN_PrivGetMemory(s, entry, 0);
}

GSM_Error ATGEN_GetNextMemory (GSM_StateMachine *s, GSM_MemoryEntry *entry, bool start)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error		error;
	int			step = 0;

	if (Priv->MemorySize == 0) {
		error = ATGEN_GetMemoryInfo(s, NULL, AT_Total);
		if (error != ERR_NONE) return error;
	}

	if (start) {
		entry->Location = 1;
	} else {
		entry->Location++;
	}
	while ((error = ATGEN_PrivGetMemory(s, entry, step == 0 ? 0 : MIN(Priv->MemorySize, entry->Location + step))) == ERR_EMPTY) {
		entry->Location += step + 1;
		if (entry->Location > Priv->MemorySize) break;
		/* SBNR works only for one location */
		if (entry->MemoryType != MEM_ME || Priv->PBKSBNR != AT_SBNR_AVAILABLE) step = MIN(step + 2, 20);
	}
	if (error == ERR_INVALIDLOCATION) return ERR_EMPTY;
	return error;
}

GSM_Error ATGEN_DeleteAllMemory(GSM_StateMachine *s, GSM_MemoryType type)
{
	GSM_Error 		error;
	unsigned char		req[100];
	int			i;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	error = ATGEN_SetPBKMemory(s, type);
	if (error != ERR_NONE) return error;

	if (Priv->MemorySize == 0) {
		error = ATGEN_GetMemoryInfo(s, NULL, AT_Total);
		if (error != ERR_NONE) return error;
	}

	if (Priv->FirstMemoryEntry == -1) {
		error = ATGEN_GetMemoryInfo(s, NULL, AT_First);
		if (error != ERR_NONE) return error;
	}


	smprintf(s, "Deleting all phonebook entries\n");
	for (i = Priv->FirstMemoryEntry; i < Priv->FirstMemoryEntry + Priv->MemorySize; i++) {
		sprintf(req, "AT+CPBW=%d\r",i);
		error = GSM_WaitFor (s, req, strlen(req), 0x00, 4, ID_SetMemory);
		if (error != ERR_NONE) return error;
	}
	return ERR_NONE;
}

GSM_Error ATGEN_ReplyDialVoice(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "Dial voice OK\n");
		return ERR_NONE;
	case AT_Reply_Error:
		smprintf(s, "Dial voice error\n");
		return ERR_UNKNOWN;
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_DialVoice(GSM_StateMachine *s, char *number, GSM_CallShowNumber ShowNumber)
{
	char req[39] = "ATDT";

	if (ShowNumber != GSM_CALL_DefaultNumberPresence) return ERR_NOTSUPPORTED;
	if (strlen(number) > 32) return (ERR_UNKNOWN);

	strcat(req, number);
	strcat(req, ";\r");

	smprintf(s, "Making voice call\n");
	return GSM_WaitFor (s, req, 4+2+strlen(number), 0x00, 5, ID_DialVoice);
}

GSM_Error ATGEN_ReplyEnterSecurityCode(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "Security code was OK\n");
		return ERR_NONE;
	case AT_Reply_Error:
		smprintf(s, "Incorrect security code\n");
		return ERR_SECURITYERROR;
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
	case AT_Reply_CMEError:
	        return ATGEN_HandleCMEError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_EnterSecurityCode(GSM_StateMachine *s, GSM_SecurityCode Code)
{
	unsigned char req[50];

	switch (Code.Type) {
	case SEC_Pin :
		sprintf(req, "AT+CPIN=\"%s\"\r" , Code.Code);
		break;
	case SEC_Pin2 :
		if (s->Phone.Data.Priv.ATGEN.Manufacturer == AT_Siemens) {
			sprintf(req, "AT+CPIN2=\"%s\"\r", Code.Code);
		} else {
			sprintf(req, "AT+CPIN=\"%s\"\r" , Code.Code);
		}
		break;
	default : return ERR_NOTIMPLEMENTED;
	}

	smprintf(s, "Entering security code\n");
	return GSM_WaitFor (s, req, strlen(req), 0x00, 6, ID_EnterSecurityCode);
}

GSM_Error ATGEN_ReplyGetSecurityStatus(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_SecurityCodeType *Status = s->Phone.Data.SecurityStatus;

	smprintf(s, "Security status received - ");
	if (strstr(msg.Buffer,"READY")) {
		*Status = SEC_None;
		smprintf(s, "nothing to enter\n");
		return ERR_NONE;
	}
	if (strstr(msg.Buffer,"PH_SIM PIN")) {
		smprintf(s, "no SIM inside or other error\n");
		return ERR_UNKNOWN;
	}
	if (strstr(msg.Buffer,"SIM PIN2")) {
		*Status = SEC_Pin2;
		smprintf(s, "waiting for PIN2\n");
		return ERR_NONE;
	}
	if (strstr(msg.Buffer,"SIM PUK2")) {
		*Status = SEC_Puk2;
		smprintf(s, "waiting for PUK2\n");
		return ERR_NONE;
	}
	if (strstr(msg.Buffer,"SIM PIN")) {
		*Status = SEC_Pin;
		smprintf(s, "waiting for PIN\n");
		return ERR_NONE;
	}
	if (strstr(msg.Buffer,"SIM PUK")) {
		*Status = SEC_Puk;
		smprintf(s, "waiting for PUK\n");
		return ERR_NONE;
	}
	smprintf(s, "unknown\n");
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetSecurityStatus(GSM_StateMachine *s, GSM_SecurityCodeType *Status)
{
	s->Phone.Data.SecurityStatus=Status;

	smprintf(s, "Getting security code status\n");
	/* Please note, that A2D doesn't return OK on the end.
 	 * Because of it ReplyGetSecurityStatus is called after receiving line
	 * with +CPIN:
	 */
	return GSM_WaitFor (s, "AT+CPIN?\r", 9, 0x00, 4, ID_GetSecurityStatus);
}

GSM_Error ATGEN_AnswerCall(GSM_StateMachine *s, int ID, bool all)
{
	if (all) {
		smprintf(s, "Answering all calls\n");
		return GSM_WaitFor (s, "ATA\r", 4, 0x00, 4, ID_AnswerCall);
	}
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_ReplyCancelCall(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Call call;

	switch(s->Phone.Data.Priv.ATGEN.ReplyState) {
        case AT_Reply_OK:
     	    smprintf(s, "Calls canceled\n");
            call.CallIDAvailable = false;
            call.Status 	 = GSM_CALL_CallLocalEnd;
            if (s->User.IncomingCall) s->User.IncomingCall(s->CurrentConfig->Device, call);

            return ERR_NONE;
    	case AT_Reply_CMSError:
            return ATGEN_HandleCMSError(s);
        default:
    	    return ERR_UNKNOWN;
	}
}

GSM_Error ATGEN_CancelCall(GSM_StateMachine *s, int ID, bool all)
{
	GSM_Error error;

	if (all) {
		smprintf(s, "Dropping all calls\n");
		error = GSM_WaitFor (s, "ATH\r", 4, 0x00, 4, ID_CancelCall);
		if (error == ERR_UNKNOWN) {
		    return GSM_WaitFor (s, "AT+CHUP\r", 8, 0x00, 4, ID_CancelCall);
		}
		return error;
	}
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_ReplyReset(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	smprintf(s, "Reset done\n");
	return ERR_NONE;
}

GSM_Error ATGEN_Reset(GSM_StateMachine *s, bool hard)
{
	GSM_Error error;

	if (!hard) return ERR_NOTSUPPORTED;

	smprintf(s, "Resetting device\n");
	/* Siemens 35 */
	error=GSM_WaitFor (s, "AT+CFUN=1,1\r", 12, 0x00, 8, ID_Reset);
	if (error != ERR_NONE) {
		/* Siemens M20 */
		error=GSM_WaitFor (s, "AT^SRESET\r", 10, 0x00, 8, ID_Reset);
	}
	return error;
}

GSM_Error ATGEN_ReplyResetPhoneSettings(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	smprintf(s, "Reset done\n");
	return ERR_NONE;
}

GSM_Error ATGEN_ResetPhoneSettings(GSM_StateMachine *s, GSM_ResetSettingsType Type)
{
	smprintf(s, "Resetting settings to default\n");
	return GSM_WaitFor (s, "AT&F\r", 5, 0x00, 4, ID_ResetPhoneSettings);
}

GSM_Error ATGEN_SetAutoNetworkLogin(GSM_StateMachine *s)
{
	smprintf(s, "Enabling automatic network login\n");
	return GSM_WaitFor (s, "AT+COPS=0\r", 10, 0x00, 4, ID_SetAutoNetworkLogin);
}

GSM_Error ATGEN_SendDTMF(GSM_StateMachine *s, char *sequence)
{
	unsigned char 	req[80] = "AT+VTS=";
	int 		n;

	for (n = 0; n < 32; n++) {
		if (sequence[n] == '\0') break;
		if (n != 0) req[6 + 2 * n] = ',';
		req[7 + 2 * n] = sequence[n];
	}

	strcat(req, ";\r");

	smprintf(s, "Sending DTMF\n");
	return GSM_WaitFor (s, req, 7+2+2*strlen(sequence), 0x00, 4, ID_SendDTMF);
}

GSM_Error ATGEN_ReplyDeleteSMSMessage(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "SMS deleted OK\n");
		return ERR_NONE;
	case AT_Reply_Error:
		smprintf(s, "Invalid location\n");
		return ERR_INVALIDLOCATION;
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_DeleteSMS(GSM_StateMachine *s, GSM_SMSMessage *sms)
{
	unsigned char		req[20], folderid;
	GSM_Error		error;
	int			location;
	GSM_MultiSMSMessage	msms;

	msms.Number = 0;
	msms.SMS[0] = *sms;

	/* By reading SMS we check if it is really inbox/outbox */
	error = ATGEN_GetSMS(s, &msms);
	if (error != ERR_NONE) return error;

	error = ATGEN_GetSMSLocation(s, sms, &folderid, &location);
	if (error != ERR_NONE) return error;

	sprintf(req, "AT+CMGD=%i\r",location);

	smprintf(s, "Deleting SMS\n");
	return GSM_WaitFor (s, req, strlen(req), 0x00, 5, ID_DeleteSMSMessage);
}

GSM_Error ATGEN_GetSMSFolders(GSM_StateMachine *s, GSM_SMSFolders *folders)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error 		error;
	int			used = 0;

	if (Priv->PhoneSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, false);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}
	if (Priv->SIMSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, true);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}

	folders->Number = 0;
	if (Priv->PhoneSMSMemory == AT_NOTAVAILABLE && Priv->SIMSMSMemory == AT_NOTAVAILABLE) {
		return ERR_NONE;
	}

	PHONE_GetSMSFolders(s,folders);

	if (Priv->SIMSMSMemory == AT_AVAILABLE) {
		used = 2;
	}

	if (Priv->PhoneSMSMemory == AT_AVAILABLE) {
		if (used != 0) {
			CopyUnicodeString(folders->Folder[used    ].Name,folders->Folder[0].Name);
			CopyUnicodeString(folders->Folder[used + 1].Name,folders->Folder[1].Name);
			folders->Folder[used    ].InboxFolder 	= true;
			folders->Folder[used + 1].InboxFolder 	= false;
		}
		folders->Folder[used    ].Memory 	= MEM_ME;
		folders->Folder[used + 1].Memory 	= MEM_ME;
		folders->Number += 2;
		used += 2;
	}

	return ERR_NONE;
}

GSM_Error ATGEN_ReplySetMemory(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "Phonebook entry written OK\n");
		return ERR_NONE;
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
	case AT_Reply_CMEError:
		if (s->Phone.Data.Priv.ATGEN.ErrorCode == 255 && s->Phone.Data.Priv.ATGEN.Manufacturer == AT_Ericsson) {
			smprintf(s, "CME Error %i, probably means empty entry\n", s->Phone.Data.Priv.ATGEN.ErrorCode);
			return ERR_EMPTY;
		}
	        return ATGEN_HandleCMEError(s);
	case AT_Reply_Error:
		return ERR_INVALIDDATA;
	default:
		return ERR_UNKNOWNRESPONSE;
	}
}

GSM_Error ATGEN_DeleteMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	GSM_Error 		error;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	unsigned char		req[100];

	if (entry->Location < 1) return ERR_INVALIDLOCATION;

	error = ATGEN_SetPBKMemory(s, entry->MemoryType);
	if (error != ERR_NONE) return error;

	if (Priv->FirstMemoryEntry == -1) {
		error = ATGEN_GetMemoryInfo(s, NULL, AT_First);
		if (error != ERR_NONE) return error;
	}

	sprintf(req, "AT+CPBW=%d\r",entry->Location + Priv->FirstMemoryEntry - 1);

	smprintf(s, "Deleting phonebook entry\n");
	error = GSM_WaitFor (s, req, strlen(req), 0x00, 4, ID_SetMemory);
	if (error == ERR_EMPTY) return ERR_NONE;
	return error;
}

GSM_Error ATGEN_PrivSetMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	/* REQUEST_SIZE should be big enough to handle all possibl cases
	 * correctly, especially with unicode entries */
#define REQUEST_SIZE	((4 * GSM_PHONEBOOK_TEXT_LENGTH) + 30)
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	int			Group, Name, Number,NumberType=0, len;
	GSM_Error 		error;
	unsigned char		req[REQUEST_SIZE + 1];
	unsigned char		name[2*(GSM_PHONEBOOK_TEXT_LENGTH + 1)];
	unsigned char		uname[2*(GSM_PHONEBOOK_TEXT_LENGTH + 1)];
	unsigned char		number[GSM_PHONEBOOK_TEXT_LENGTH + 1];
	int			reqlen, i;
	GSM_AT_Charset_Preference	Prefer = AT_PREF_CHARSET_NORMAL;

	if (entry->Location == 0) return ERR_INVALIDLOCATION;

	error = ATGEN_SetPBKMemory(s, entry->MemoryType);
	if (error != ERR_NONE) return error;

	for (i=0;i<entry->EntriesNum;i++) {
		entry->Entries[i].AddError = ERR_NOTSUPPORTED;
	}

	GSM_PhonebookFindDefaultNameNumberGroup(entry, &Name, &Number, &Group);

	name[0] = 0;
	if (Name != -1) {
		len = UnicodeLength(entry->Entries[Name].Text);

		/* Compare if we would loose some information when not using
		 * unicode */
		EncodeDefault(name, entry->Entries[Name].Text, &len, true, NULL);
		DecodeDefault(uname, name, len, true, NULL);
		if (!mywstrncmp(uname, entry->Entries[Name].Text, len)) {
			/* Get maximal text length */
			if (Priv->TextLength == 0) {
				ATGEN_GetMemoryInfo(s, NULL, AT_Sizes);
			}

			/* I char stored in GSM alphabet takes 7 bits, one
			 * unicode 16, if storing in unicode would truncate
			 * text, do not use it, otherwise we will use it */
			if ((Priv->TextLength != 0) && ((Priv->TextLength * 7 / 16) <= len)) {
				Prefer = AT_PREF_CHARSET_NORMAL;
			} else {
				Prefer = AT_PREF_CHARSET_UNICODE;
			}
		}

		error = ATGEN_SetCharset(s, Prefer);
		if (error != ERR_NONE) return error;

		switch (Priv->Charset) {
		case AT_CHARSET_HEX:
			EncodeHexBin(name, DecodeUnicodeString(entry->Entries[Name].Text), UnicodeLength(entry->Entries[Name].Text));
			len = strlen(name);
			break;
		case AT_CHARSET_GSM:
			smprintf(s, "str: %s\n", DecodeUnicodeString(entry->Entries[Name].Text));
			len = UnicodeLength(entry->Entries[Name].Text);
			EncodeDefault(name, entry->Entries[Name].Text, &len, true, NULL);
			break;
		case AT_CHARSET_UCS2:
			EncodeHexUnicode(name, entry->Entries[Name].Text, UnicodeLength(entry->Entries[Name].Text));
			len = strlen(name);
			break;
  		case AT_CHARSET_IRA:
			return ERR_NOTSUPPORTED;
  		case AT_CHARSET_UTF8:
			EncodeUTF8(name, entry->Entries[Name].Text);
			len = strlen(name);
  			break;
		case AT_CHARSET_PCCP437:
			/* FIXME: correctly decode PCCP437 */
			smprintf(s, "str: %s\n", DecodeUnicodeString(entry->Entries[Name].Text));
			len = UnicodeLength(entry->Entries[Name].Text);
			EncodeDefault(name, entry->Entries[Name].Text, &len, true, NULL);
			break;
		}
		entry->Entries[Name].AddError = ERR_NONE;
	} else {
		smprintf(s, "WARNING: No usable name found!\n");
		len = 0;
	}

	if (Number != -1) {
		GSM_PackSemiOctetNumber(entry->Entries[Number].Text, number, false);
		NumberType = number[0];
		sprintf(number,"%s",DecodeUnicodeString(entry->Entries[Number].Text));
		entry->Entries[Number].AddError = ERR_NONE;
	} else {
		smprintf(s, "WARNING: No usable number found!\n");
		number[0] = 0;
	}

	if (Priv->FirstMemoryEntry == -1) {
		error = ATGEN_GetMemoryInfo(s, NULL, AT_First);
		if (error != ERR_NONE) return error;
	}

	/* We can't use here:
	 * sprintf(req, "AT+CPBW=%d, \"%s\", %i, \"%s\"\r",
	 *         entry->Location, number, NumberType, name);
	 * because name can contain 0 when using GSM alphabet.
	 */
	sprintf(req, "AT+CPBW=%d, \"%s\", %i, \"", entry->Location + Priv->FirstMemoryEntry - 1, number, NumberType);
	reqlen = strlen(req);
	if (reqlen + len > REQUEST_SIZE - 2) {
		smprintf(s, "WARNING: Text truncated to fit in buffer!\n");
		len = REQUEST_SIZE - 2 - reqlen;
	}
	memcpy(req + reqlen, name, len);
	reqlen += len;
	memcpy(req + reqlen, "\"\r", 2);
	reqlen += 2;

	smprintf(s, "Writing phonebook entry\n");
	return GSM_WaitFor (s, req, reqlen, 0x00, 4, ID_SetMemory);
#undef REQUEST_SIZE
}

GSM_Error ATGEN_SetMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	if (entry->Location == 0) return ERR_INVALIDLOCATION;
	return ATGEN_PrivSetMemory(s, entry);
}

GSM_Error ATGEN_AddMemory(GSM_StateMachine *s, GSM_MemoryEntry *entry)
{
	GSM_Error 		error;
	GSM_MemoryStatus	Status;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

 	/* Switch to desired memory type */
 	error = ATGEN_SetPBKMemory(s, entry->MemoryType);
 	if (error != ERR_NONE) return error;

	/* Find out empty location */
	error = ATGEN_GetMemoryInfo(s, &Status, AT_NextEmpty);
	if (error != ERR_NONE) return error;
	if (Priv->NextMemoryEntry == 0) return ERR_FULL;
	entry->Location = Priv->NextMemoryEntry;

	return ATGEN_PrivSetMemory(s, entry);
}

/* Use ATGEN_ExtractOneParameter ?? */
void Extract_CLIP_number(char *dest, char *buf)
{
	char 	*start, *stop;
	int 	i = 0;

	stop = strstr(buf, ",");
        if (stop != NULL) {
        	start = strstr(buf, ":");
	        if (start != NULL) {
			for (start = start + 2; start + i < stop;  i++)
			dest[i] = start[i];
       		}
	}
	dest[i] = 0; /* end the number */

	return;
}

GSM_Error ATGEN_ReplyIncomingCallInfo(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	char 			num[128];
	GSM_Call 		call;

	smprintf(s, "Incoming call info\n");
	if (s->Phone.Data.EnableIncomingCall && s->User.IncomingCall!=NULL) {
		call.CallIDAvailable 	= false;
		num[0] 			= 0;
		if (strstr(msg.Buffer, "RING")) {
			call.Status = GSM_CALL_IncomingCall;
			Extract_CLIP_number(num, msg.Buffer);
		} else if (strstr(msg.Buffer, "NO CARRIER")) {
			call.Status = GSM_CALL_CallEnd;
		} else if (strstr(msg.Buffer, "COLP:")) {
			call.Status = GSM_CALL_CallStart;
			Extract_CLIP_number(num, msg.Buffer);
		} else {
			smprintf(s, "CLIP: error\n");
			return ERR_NONE;
		}
		EncodeUnicode(call.PhoneNumber, num, strlen(num));

		s->User.IncomingCall(s->CurrentConfig->Device, call);
	}

	return ERR_NONE;
}

GSM_Error ATGEN_IncomingGPRS(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	/* "+CGREG: 1,1" */
	smprintf(s, "GPRS change\n");
	return ERR_NONE;
}

GSM_Error ATGEN_IncomingBattery(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	int 	level = 0;
	char 	*p;

	/* "_OBS: 92,1" */
	p = strstr(msg.Buffer, "_OBS:");
	if (p) level = atoi(p + 5);
	smprintf(s, "Battery level changed to %d\n", level);
	return ERR_NONE;
}

GSM_Error ATGEN_IncomingNetworkLevel(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	int 	level = 0;
	char 	*p;

	/* "_OSIGQ: 12,0" */
	p = strstr(msg.Buffer, "_OSIGQ: ");
	if (p) level = atoi(p + 7);
	smprintf(s, "Network level changed to %d\n", level);
	return ERR_NONE;
}

GSM_Error ATGEN_ReplyGetSIMIMSI(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Phone_Data		*Data = &s->Phone.Data;
	char 			*c;

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
		CopyLineString(Data->PhoneString, msg.Buffer, Priv->Lines, 2);

        	/* Read just IMSI also on phones that prepend it by "<IMSI>:" (Alcatel BE5) */
		c = strstr(Data->PhoneString, "<IMSI>:");
		if (c != NULL) {
			c += 7;
			memmove(Data->PhoneString, c, strlen(c) + 1);
		}

		smprintf(s, "Received IMSI %s\n",Data->PhoneString);
		return ERR_NONE;
	case AT_Reply_Error:
		smprintf(s, "No access to SIM card or not supported by device\n");
		return ERR_SECURITYERROR;
	case AT_Reply_CMEError:
	        return ATGEN_HandleCMEError(s);
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
	default:
		break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetSIMIMSI(GSM_StateMachine *s, char *IMSI)
{
	s->Phone.Data.PhoneString = IMSI;
	smprintf(s, "Getting SIM IMSI\n");
	return GSM_WaitFor (s, "AT+CIMI\r", 8, 0x00, 4, ID_GetSIMIMSI);
}

GSM_Error ATGEN_GetDisplayStatus(GSM_StateMachine *s, GSM_DisplayFeatures *features)
{
	return ERR_NOTSUPPORTED;

	s->Phone.Data.DisplayFeatures = features;
	smprintf(s, "Getting display status\n");
	return GSM_WaitFor (s, "AT+CIND?\r",9, 0x00, 4, ID_GetDisplayStatus);
}

GSM_Error ATGEN_IncomingSMSCInfo(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	return ERR_NONE;
}

GSM_Error ATGEN_ReplyGetBatteryCharge(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
    GSM_Phone_Data		*Data = &s->Phone.Data;
    int 			i;

    switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
        case AT_Reply_OK:
            smprintf(s, "Battery level received\n");
            Data->BatteryCharge->BatteryPercent = atoi(msg.Buffer+17);
            i = atoi(msg.Buffer+14);
            if (i >= 0 && i <= 3) {
                Data->BatteryCharge->ChargeState = i + 1;
            }
            return ERR_NONE;
        case AT_Reply_Error:
            smprintf(s, "Can't get battery level\n");
            return ERR_UNKNOWN;
        case AT_Reply_CMSError:
            smprintf(s, "Can't get battery level\n");
            return ATGEN_HandleCMSError(s);
        default:
            break;
    }
    return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetBatteryCharge(GSM_StateMachine *s, GSM_BatteryCharge *bat)
{
	GSM_ClearBatteryCharge(bat);
	s->Phone.Data.BatteryCharge = bat;
	smprintf(s, "Getting battery charge\n");
	return GSM_WaitFor (s, "AT+CBC\r", 7, 0x00, 4, ID_GetBatteryCharge);
}

GSM_Error ATGEN_ReplyGetSignalQuality(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_SignalQuality	*Signal = s->Phone.Data.SignalQuality;
	int 			i;
	char 			*pos;

	Signal->SignalStrength 	= -1;
	Signal->SignalPercent 	= -1;
	Signal->BitErrorRate 	= -1;

	switch (s->Phone.Data.Priv.ATGEN.ReplyState) {
        case AT_Reply_OK:
            smprintf(s, "Signal quality info received\n");
            i = atoi(msg.Buffer+15);
            if (i != 99) {
                /* from GSM 07.07 section 8.5 */
                Signal->SignalStrength = 2 * i - 113;

                /* FIXME: this is wild guess and probably will be phone dependant */
                Signal->SignalPercent = 15 * i;
                if (Signal->SignalPercent > 100) Signal->SignalPercent = 100;
            }
            pos = strchr(msg.Buffer + 15, ',');
            if (pos != NULL) {
                i = atoi(pos + 1);
                /* from GSM 05.08 section 8.2.4 */
                switch (i) {
                    case 0: Signal->BitErrorRate =  0; break; /* 0.14 */
                    case 1: Signal->BitErrorRate =  0; break; /* 0.28 */
                    case 2: Signal->BitErrorRate =  1; break; /* 0.57 */
                    case 3: Signal->BitErrorRate =  1; break; /* 1.13 */
                    case 4: Signal->BitErrorRate =  2; break; /* 2.26 */
                    case 5: Signal->BitErrorRate =  5; break; /* 4.53 */
                    case 6: Signal->BitErrorRate =  9; break; /* 9.05 */
                    case 7: Signal->BitErrorRate = 18; break; /* 18.10 */
                }
            }
            return ERR_NONE;
        case AT_Reply_CMSError:
            return ATGEN_HandleCMSError(s);
        default:
            break;
	}
	return ERR_UNKNOWNRESPONSE;
}

GSM_Error ATGEN_GetSignalQuality(GSM_StateMachine *s, GSM_SignalQuality *sig)
{
	s->Phone.Data.SignalQuality = sig;
	smprintf(s, "Getting signal quality info\n");
	return GSM_WaitFor (s, "AT+CSQ\r", 7, 0x00, 4, ID_GetSignalQuality);
}

/* When use AT+CPIN?, A2D returns it without OK and because of it Gammu
   parses answer without it.
   MC35 and other return OK after answer for AT+CPIN?. Here we handle it.
   Any better idea ?
 */
GSM_Error ATGEN_ReplyOK(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	return ERR_NONE;
}

static GSM_Error ATGEN_GetNextCalendar(GSM_StateMachine *s, GSM_CalendarEntry *Note, bool start)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer==AT_Siemens ) return SIEMENS_GetNextCalendar(s,Note,start);
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_Terminate(GSM_StateMachine *s)
{
	GSM_Phone_ATGENData *Priv = &s->Phone.Data.Priv.ATGEN;

	free(Priv->file.Buffer);
	return ERR_NONE;
}

GSM_Error ATGEN_SetCalendarNote(GSM_StateMachine *s, GSM_CalendarEntry *Note)
{
	GSM_Phone_ATGENData *Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer==AT_Siemens)  return SIEMENS_SetCalendarNote(s, Note);
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_AddCalendarNote(GSM_StateMachine *s, GSM_CalendarEntry *Note)
{
	GSM_Phone_ATGENData *Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer==AT_Siemens)  return SIEMENS_AddCalendarNote(s, Note);
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_DelCalendarNote(GSM_StateMachine *s, GSM_CalendarEntry *Note)
{
	GSM_Phone_ATGENData *Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer==AT_Siemens)  return SIEMENS_DelCalendarNote(s, Note);
	return ERR_NOTSUPPORTED;
}


GSM_Error ATGEN_GetBitmap(GSM_StateMachine *s, GSM_Bitmap *Bitmap)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer==AT_Siemens) return SIEMENS_GetBitmap(s, Bitmap);
	if (Priv->Manufacturer==AT_Samsung) return SAMSUNG_GetBitmap(s, Bitmap);
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_SetBitmap(GSM_StateMachine *s, GSM_Bitmap *Bitmap)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer==AT_Siemens) return SIEMENS_SetBitmap(s, Bitmap);
	if (Priv->Manufacturer==AT_Samsung) return SAMSUNG_SetBitmap(s, Bitmap);
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_GetRingtone(GSM_StateMachine *s, GSM_Ringtone *Ringtone, bool PhoneRingtone)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer==AT_Siemens) return SIEMENS_GetRingtone(s, Ringtone, PhoneRingtone);
	if (Priv->Manufacturer==AT_Samsung) return SAMSUNG_GetRingtone(s, Ringtone, PhoneRingtone);
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_SetRingtone(GSM_StateMachine *s, GSM_Ringtone *Ringtone, int *maxlength)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;

	if (Priv->Manufacturer==AT_Siemens) return SIEMENS_SetRingtone(s, Ringtone, maxlength);
	if (Priv->Manufacturer==AT_Samsung) return SAMSUNG_SetRingtone(s, Ringtone, maxlength);
	return ERR_NOTSUPPORTED;
}

GSM_Error ATGEN_PressKey(GSM_StateMachine *s, GSM_KeyCode Key, bool Press)
{
	GSM_Error	error;
	unsigned char 	frame[20];

	frame[0] = 0;

	ATGEN_SetCharset(s, AT_PREF_CHARSET_IRA);

	strcat(frame, "AT+CKPD=\"");

	if (Press) {
		switch (Key) {
			case GSM_KEY_1 			: strcat(frame,"1"); break;
			case GSM_KEY_2			: strcat(frame,"2"); break;
			case GSM_KEY_3			: strcat(frame,"3"); break;
			case GSM_KEY_4			: strcat(frame,"4"); break;
			case GSM_KEY_5			: strcat(frame,"5"); break;
			case GSM_KEY_6			: strcat(frame,"6"); break;
			case GSM_KEY_7			: strcat(frame,"7"); break;
			case GSM_KEY_8			: strcat(frame,"8"); break;
			case GSM_KEY_9			: strcat(frame,"9"); break;
			case GSM_KEY_0			: strcat(frame,"0"); break;
			case GSM_KEY_HASH		: strcat(frame,"#"); break;
			case GSM_KEY_ASTERISK		: strcat(frame,"*"); break;
			case GSM_KEY_POWER		: strcat(frame,"P"); break;
			case GSM_KEY_GREEN		: strcat(frame,"S"); break;
			case GSM_KEY_RED		: strcat(frame,"E"); break;
			case GSM_KEY_INCREASEVOLUME	: strcat(frame,"U"); break;
			case GSM_KEY_DECREASEVOLUME	: strcat(frame,"D"); break;
			case GSM_KEY_UP			: strcat(frame,"^"); break;
			case GSM_KEY_DOWN		: strcat(frame,"V"); break;
			case GSM_KEY_MENU		: strcat(frame,"F"); break;
			case GSM_KEY_LEFT		: strcat(frame,"<"); break;
			case GSM_KEY_RIGHT		: strcat(frame,">"); break;
			case GSM_KEY_SOFT1		: strcat(frame,"["); break;
			case GSM_KEY_SOFT2		: strcat(frame,"]"); break;
			case GSM_KEY_HEADSET		: strcat(frame,"H"); break;
			case GSM_KEY_JOYSTICK		: strcat(frame,":J"); break;
			case GSM_KEY_CAMERA		: strcat(frame,":C"); break;
			case GSM_KEY_OPERATOR		: strcat(frame,":O"); break;
			case GSM_KEY_RETURN		: strcat(frame,":R"); break;
			case GSM_KEY_CLEAR		: strcat(frame,"C"); break;
			case GSM_KEY_MEDIA		: strcat(frame,":S"); break;
			case GSM_KEY_DESKTOP		: strcat(frame,":D"); break;
			case GSM_KEY_NONE		: return ERR_NONE; /* Nothing to do here */
			case GSM_KEY_NAMES		: return ERR_NOTSUPPORTED;
		}
		strcat(frame, "\"\r");
		smprintf(s, "Pressing key\n");
		error = GSM_WaitFor (s, frame, 12, 0x00, 4, ID_PressKey);
		if (error != ERR_NONE) return error;

		/* Strange. My T310 needs it */
		return GSM_WaitFor (s, "ATE1\r", 5, 0x00, 4, ID_EnableEcho);
	} else {
		return ERR_NONE;
	}
}

#ifdef GSM_ENABLE_CELLBROADCAST

GSM_Error ATGEN_ReplyIncomingCB(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_CBMessage 	CB;
	int		i,j;
	char		Buffer[300],Buffer2[300];

	smprintf(s, "CB received\n");
	return ERR_NONE;

	DecodeHexBin (Buffer,msg.Buffer+6,msg.Length-6);
	DumpMessage(&di ,Buffer,msg.Length-6);

	CB.Channel = Buffer[4];

	for (j=0;j<msg.Length;j++) {
	smprintf(s, "j=%i\n",j);
	i=GSM_UnpackEightBitsToSeven(0, msg.Buffer[6], msg.Buffer[6], msg.Buffer+j, Buffer2);
//	i = msg.Buffer[6] - 1;
//	while (i!=0) {
//		if (Buffer[i] == 13) i = i - 1; else break;
//	}
	DecodeDefault(CB.Text, Buffer2, msg.Buffer[6], false, NULL);
	smprintf(s, "Channel %i, text \"%s\"\n",CB.Channel,DecodeUnicodeString(CB.Text));
	}
	if (s->Phone.Data.EnableIncomingCB && s->User.IncomingCB!=NULL) {
		s->User.IncomingCB(s->CurrentConfig->Device,CB);
	}
	return ERR_NONE;
}

#endif

bool InRange(int *range, int i) {
	while (*range != -1) {
		if (*range == i) return true;
		range++;
	}
	return false;
}

int *GetRange(char *buffer)
{
	int	*result;
	int	commas = 0, dashes = 0, i1, i2, i;
	char	*c = buffer, *c2;

	if (c[0] != '(') return NULL;
	c++;
	c2 = c;

	while (*c2 != ')') {
		if (*c2 == ',') commas++;
		else if (*c2 == '-') dashes++;
		c2++;
	}

	if ((commas != 0 && dashes != 0) || dashes > 1) {
		return NULL;
	} else if (commas == 0 && dashes == 0) {
		result = calloc(2, sizeof(int));
		if (result == NULL) return NULL;
		result[0] = atoi(c);
		result[1] = -1;
	} else if (dashes == 1) {
		i1 = atoi(c);
		c2 = c;
		while (*c2 != '-') c2++;
		c2++;
		i2  = atoi(c2);
		if (i2 < i1) return NULL;
		result = calloc(2 + i2 - i1, sizeof(int));
		if (result == NULL) return NULL;
		for (i = i1; i <= i2; i++) {
			result[i - i1] = i;
		}
		result[1 + i2 - i1] = -1;
	} else {
		result = calloc(2 + commas, sizeof(int));
		if (result == NULL) return NULL;
		i = 1;
		c2 = c;
		result[0] = atoi(c2);
		while (*c2 != ')') {
			if (*c2 == ',') {
				result[i] = atoi(c2 + 1);
				i++;
			}
			c2++;
		}
		result[i] = -1;
	}
	i = 0;
	return result;
}

GSM_Error ATGEN_ReplyGetCNMIMode(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	char			*buffer;
	int			*range;

	/* Sample resposne we get here:
	AT+CNMI=?
	+CNMI: (0-2),(0,1,3),(0),(0,1),(0,1)
	*/
	Priv->CNMIMode			= 0;
	Priv->CNMIProcedure		= 0;
	Priv->CNMIDeliverProcedure	= 0;
#ifdef GSM_ENABLE_CELLBROADCAST
	Priv->CNMIBroadcastProcedure	= 0;
#endif

	buffer = strchr(msg.Buffer, '\n');
	if (buffer == NULL) return  ERR_UNKNOWNRESPONSE;
	while (isspace(*buffer)) buffer++;

	if (strncmp(buffer, "+CNMI: ", 7) != 0) return ERR_UNKNOWNRESPONSE;
	buffer += 7;

	buffer = strchr(buffer, '(');
	if (buffer == NULL) return  ERR_UNKNOWNRESPONSE;
	range = GetRange(buffer);
	if (range == NULL) return  ERR_UNKNOWNRESPONSE;
	if (InRange(range, 2)) Priv->CNMIMode = 2; 	/* 2 = buffer messages and send them when link is free */
	else if (InRange(range, 3)) Priv->CNMIMode = 3; /* 3 = send messages directly */
	else return ERR_NONE; /* we don't want: 1 = ignore new messages, 0 = store messages and no indication */
	free(range);

	buffer++;
	buffer = strchr(buffer, '(');
	if (buffer == NULL) return  ERR_UNKNOWNRESPONSE;
	range = GetRange(buffer);
	if (range == NULL) return  ERR_UNKNOWNRESPONSE;
	if (InRange(range, 1)) Priv->CNMIProcedure = 1; 	/* 1 = store message and send where it is stored */
	else if (InRange(range, 2)) Priv->CNMIProcedure = 2; 	/* 2 = route message to TE */
	else if (InRange(range, 3)) Priv->CNMIProcedure = 3; 	/* 3 = 1 + route class 3 to TE */
	/* we don't want: 0 = just store to memory */
	free(range);

	buffer++;
	buffer = strchr(buffer, '(');
#ifdef GSM_ENABLE_CELLBROADCAST
	if (buffer == NULL) return  ERR_UNKNOWNRESPONSE;
	range = GetRange(buffer);
	if (range == NULL) return  ERR_UNKNOWNRESPONSE;
	if (InRange(range, 2)) Priv->CNMIBroadcastProcedure = 2; /* 2 = route message to TE */
	else if (InRange(range, 1)) Priv->CNMIBroadcastProcedure = 1; /* 1 = store message and send where it is stored */
	else if (InRange(range, 3)) Priv->CNMIBroadcastProcedure = 3; /* 3 = 1 + route class 3 to TE */
	/* we don't want: 0 = just store to memory */
	free(range);
#endif

	buffer++;
	buffer = strchr(buffer, '(');
	if (buffer == NULL) return  ERR_UNKNOWNRESPONSE;
	range = GetRange(buffer);
	if (range == NULL) return  ERR_UNKNOWNRESPONSE;
	if (InRange(range, 1)) Priv->CNMIDeliverProcedure = 1; /* 1 = route message to TE */
	else if (InRange(range, 2)) Priv->CNMIDeliverProcedure = 2; /* 1 = store message and send where it is stored */
	/* we don't want: 0 = no routing */
	free(range);

	return ERR_NONE;

}

GSM_Error ATGEN_GetCNMIMode(GSM_StateMachine *s)
{
	return GSM_WaitFor(s, "AT+CNMI=?\r", 10, 0x00, 4, ID_GetCNMIMode);
}

GSM_Error ATGEN_SetIncomingCB(GSM_StateMachine *s, bool enable)
{
#ifdef GSM_ENABLE_CELLBROADCAST
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error 		error;
	char			buffer[100];

	if (Priv->CNMIMode == -1) {
		error = ATGEN_GetCNMIMode(s);
		if (error != ERR_NONE) return error;
	}

	if (Priv->CNMIMode == 0) return ERR_NOTSUPPORTED;
	if (Priv->CNMIBroadcastProcedure == 0) return ERR_NOTSUPPORTED;

	if (s->Phone.Data.EnableIncomingCB!=enable) {
		s->Phone.Data.EnableIncomingCB 	= enable;
		if (enable) {
			smprintf(s, "Enabling incoming CB\n");
			sprintf(buffer, "AT+CNMI=%d,,%d\r", Priv->CNMIMode, Priv->CNMIBroadcastProcedure);
			return GSM_WaitFor(s, buffer, strlen(buffer), 0x00, 4, ID_SetIncomingCB);
		} else {
			smprintf(s, "Disabling incoming CB\n");
			sprintf(buffer, "AT+CNMI=%d,,%d\r", Priv->CNMIMode, 0);
			return GSM_WaitFor(s, buffer, strlen(buffer), 0x00, 4, ID_SetIncomingCB);
		}
	}
	return ERR_NONE;
#else
	return ERR_SOURCENOTAVAILABLE;
#endif
}

GSM_Error ATGEN_SetFastSMSSending(GSM_StateMachine *s, bool enable)
{
	if (enable) {
		smprintf(s, "Enabling fast SMS sending\n");
		return GSM_WaitFor(s, "AT+CMMS=2\r", 10, 0x00, 4, ID_SetFastSMSSending);
	} else {
		smprintf(s, "Disabling fast SMS sending\n");
		return GSM_WaitFor(s, "AT+CMMS=0\r", 10, 0x00, 4, ID_SetFastSMSSending);
	}
}

GSM_Error ATGEN_IncomingSMSInfo(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	/* We get here: +CMTI: SM, 19 */
	char			*buffer;
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Phone_Data		*Data = &s->Phone.Data;
	GSM_SMSMessage		sms;

	memset(&sms, 0, sizeof(sms));
	smprintf(s, "Incoming SMS\n");
	if (Data->EnableIncomingSMS && s->User.IncomingSMS!=NULL) {
		sms.State 	 = 0;
		sms.InboxFolder  = true;
		sms.PDU 	 = 0;

		buffer = strchr(msg.Buffer, ':');
		if (buffer == NULL) return ERR_UNKNOWNRESPONSE;
		buffer++;
		while (isspace(*buffer)) buffer++;

		if (strncmp(buffer, "ME", 2) == 0 || strncmp(buffer, "\"ME\"", 4) == 0) {
			if (Priv->PhoneSMSMemory == AT_AVAILABLE) sms.Folder = 1;
			else sms.Folder = 3;
		} else if (strncmp(buffer, "SM", 2) == 0 || strncmp(buffer, "\"SM\"", 4) == 0) {
			sms.Folder = 1;
		} else {
			return ERR_UNKNOWNRESPONSE;
		}

		buffer = strchr(msg.Buffer, ',');
		if (buffer == NULL) return ERR_UNKNOWNRESPONSE;
		buffer++;
		while (isspace(*buffer)) buffer++;

		sms.Location = atoi(buffer);

		s->User.IncomingSMS(s->CurrentConfig->Device, sms);
	}
	return ERR_NONE;
}

GSM_Error ATGEN_IncomingSMSDeliver(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_Data		*Data = &s->Phone.Data;
	GSM_SMSMessage 		sms;
	int 			current = 0, current2, i=0;
	unsigned char 		buffer[300],smsframe[800];

	smprintf(s, "Incoming SMS received (Deliver)\n");
	if (Data->EnableIncomingSMS && s->User.IncomingSMS!=NULL) {
		sms.State 	 = SMS_UnRead;
		sms.InboxFolder  = true;
		sms.PDU 	 = SMS_Deliver;

		/* T310 with larger SMS goes crazy and mix this incoming
                 * frame with normal answers. PDU is always last frame
		 * We find its' number and parse it */
		while (Data->Priv.ATGEN.Lines.numbers[i*2+1] != 0) {
			/* FIXME: handle special chars correctly */
			i++;
		}
		DecodeHexBin (buffer,
			GetLineString(msg.Buffer,Data->Priv.ATGEN.Lines,i),
			strlen(GetLineString(msg.Buffer,Data->Priv.ATGEN.Lines,i)));

		/* We use locations from SMS layouts like in ../phone2.c(h) */
		for(i=0;i<buffer[0]+1;i++) smsframe[i]=buffer[current++];
		smsframe[12]=buffer[current++];

		current2=((buffer[current])+1)/2+1;
		for(i=0;i<current2+1;i++) smsframe[PHONE_SMSDeliver.Number+i]=buffer[current++];
		smsframe[PHONE_SMSDeliver.TPPID] = buffer[current++];
		smsframe[PHONE_SMSDeliver.TPDCS] = buffer[current++];
		for(i=0;i<7;i++) smsframe[PHONE_SMSDeliver.DateTime+i]=buffer[current++];
		smsframe[PHONE_SMSDeliver.TPUDL] = buffer[current++];
		for(i=0;i<smsframe[PHONE_SMSDeliver.TPUDL];i++) smsframe[i+PHONE_SMSDeliver.Text]=buffer[current++];
		GSM_DecodeSMSFrame(&sms,smsframe,PHONE_SMSDeliver);

		s->User.IncomingSMS(s->CurrentConfig->Device,sms);
	}
	return ERR_NONE;
}

/* I don't have phone able to do it and can't fill it */
GSM_Error ATGEN_IncomingSMSReport(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	smprintf(s, "Incoming SMS received (Report)\n");
	return ERR_NONE;
}

GSM_Error ATGEN_SetIncomingSMS(GSM_StateMachine *s, bool enable)
{
	GSM_Phone_ATGENData	*Priv = &s->Phone.Data.Priv.ATGEN;
	GSM_Error 		error;
	char			buffer[100];

	/* We will need this when incoming message, but we can invoke AT commands there: */
	if (Priv->PhoneSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, false);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}
	if (Priv->SIMSMSMemory == 0) {
		error = ATGEN_SetSMSMemory(s, true);
		if (error != ERR_NONE && error != ERR_NOTSUPPORTED) return error;
	}

	if (Priv->CNMIMode == -1) {
		error = ATGEN_GetCNMIMode(s);
		if (error != ERR_NONE) return error;
	}

	if (Priv->CNMIMode == 0) return ERR_NOTSUPPORTED;
	if (Priv->CNMIProcedure == 0 && Priv->CNMIDeliverProcedure == 0) return ERR_NOTSUPPORTED;

	if (s->Phone.Data.EnableIncomingSMS!=enable) {
		s->Phone.Data.EnableIncomingSMS = enable;
		if (enable) {
			smprintf(s, "Enabling incoming SMS\n");

			/* Delivery reports */
			if (Priv->CNMIDeliverProcedure != 0) {
				sprintf(buffer, "AT+CNMI=%d,,,%d\r", Priv->CNMIMode, Priv->CNMIDeliverProcedure);
				error = GSM_WaitFor(s, buffer, strlen(buffer), 0x00, 4, ID_SetIncomingSMS);
				if (error != ERR_NONE) return error;
			}

			/* Normal messages */
			if (Priv->CNMIProcedure != 0) {
				sprintf(buffer, "AT+CNMI=%d,%d\r", Priv->CNMIMode, Priv->CNMIProcedure);
				error = GSM_WaitFor(s, buffer, strlen(buffer), 0x00, 4, ID_SetIncomingSMS);
				if (error != ERR_NONE) return error;
			}
		} else {
			smprintf(s, "Disabling incoming SMS\n");

			/* Delivery reports */
			sprintf(buffer,"AT+CNMI=%d,,,%d\r", Priv->CNMIMode, 0);
			error = GSM_WaitFor(s, buffer, strlen(buffer), 0x00, 4, ID_SetIncomingSMS);
			if (error != ERR_NONE) return error;

			/* Normal messages */
			sprintf(buffer, "AT+CNMI=%d,%d\r", Priv->CNMIMode, 0);
			error = GSM_WaitFor(s, buffer, strlen(buffer), 0x00, 4, ID_SetIncomingSMS);
			if (error != ERR_NONE) return error;
		}
	}
	return ERR_NONE;
}

/**
 * Detects what additional protocols are being supported
 */
GSM_Error ATGEN_ReplyCheckProt(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	GSM_Phone_ATGENData 	*Priv = &s->Phone.Data.Priv.ATGEN;
	int			line = 0;
	char			*str;
	int			cur;

	switch (Priv->ReplyState) {
	case AT_Reply_OK:
		smprintf(s, "Protocol entries received\n");
		/* Walk through lines with +CPROT: */
		while (Priv->Lines.numbers[line*2+1]!=0) {
			str = GetLineString(msg.Buffer,Priv->Lines,line+1);
			if (strncmp(str, "+CPROT: ", 7) == 0) {
				if (sscanf(str, "+CPROT: %d,", &cur) == 1 || sscanf(str, "+CPROT: (%d,", &cur) == 1) {
					smprintf(s, "OBEX seems to be supported!\n");
					if (!IsPhoneFeatureAvailable(s->Phone.Data.ModelInfo, F_OBEX)) {
						/*
						 * We do not enable this automatically, because some
						 * phones provide less features over OBEX than AT
						 * using AT commands.
						 */
						smprintf(s, "Please consider adding F_OBEX to your phone capabilities in common/gsmstate.c\n");
					} else {
#ifdef GSM_ENABLE_SONYERICSSON
						/* Tell OBEX driver that AT+CPROT=0 is supported */
						s->Phone.Data.Priv.SONYERICSSON.HasOBEX = SONYERICSSON_OBEX_CPROT0;
#endif
					}
				}
			}
			line++;
		}
		return ERR_NONE;
	case AT_Reply_Error:
		return ERR_UNKNOWN;
	case AT_Reply_CMSError:
	        return ATGEN_HandleCMSError(s);
	default:
		return ERR_UNKNOWNRESPONSE;
	}
}

GSM_Reply_Function ATGENReplyFunctions[] = {
{ATGEN_GenericReply,		"AT\r"			,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_GenericReply,		"ATE1" 	 		,0x00,0x00,ID_EnableEcho	 },
{ATGEN_GenericReply,		"AT+CMEE=" 		,0x00,0x00,ID_EnableErrorInfo	 },
{ATGEN_GenericReply,		"AT+CKPD="		,0x00,0x00,ID_PressKey		 },
{ATGEN_ReplyGetSIMIMSI,		"AT+CIMI" 	 	,0x00,0x00,ID_GetSIMIMSI	 },
{ATGEN_ReplyCheckProt,		"AT+CPROT=?" 	 	,0x00,0x00,ID_SetOBEX		 },

{ATGEN_ReplyGetCNMIMode,	"AT+CNMI=?"		,0x00,0x00,ID_GetCNMIMode	 },
#ifdef GSM_ENABLE_CELLBROADCAST
{ATGEN_ReplyIncomingCB,		"+CBM:" 	 	,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_GenericReply,		"AT+CNMI"		,0x00,0x00,ID_SetIncomingCB	 },
#endif

{ATGEN_IncomingBattery,		"_OBS:"		 	,0x00,0x00,ID_IncomingFrame      },
{ATGEN_ReplyGetBatteryCharge,	"AT+CBC"		,0x00,0x00,ID_GetBatteryCharge	 },

{ATGEN_ReplyGetModel,		"AT+CGMM"		,0x00,0x00,ID_GetModel           },
{ATGEN_ReplyGetManufacturer,	"AT+CGMI"		,0x00,0x00,ID_GetManufacturer	 },
{ATGEN_ReplyGetFirmwareCGMR,	"AT+CGMR"		,0x00,0x00,ID_GetFirmware	 },
{ATGEN_ReplyGetFirmwareATI,	"ATI"			,0x00,0x00,ID_GetFirmware	 },
{ATGEN_ReplyGetIMEI,		"AT+CGSN"		,0x00,0x00,ID_GetIMEI	 	 },

{ATGEN_ReplySendSMS,		"AT+CMGS"		,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_ReplySendSMS,		"AT+CMSS"		,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_GenericReply,		"AT+CNMI"		,0x00,0x00,ID_SetIncomingSMS	 },
{ATGEN_GenericReply,		"AT+CMGF"		,0x00,0x00,ID_GetSMSMode	 },
{ATGEN_GenericReply,		"AT+CSDH"		,0x00,0x00,ID_GetSMSMode	 },
{ATGEN_ReplyGetSMSMessage,	"AT+CMGR"		,0x00,0x00,ID_GetSMSMessage	 },
{ATGEN_GenericReply,		"AT+CPMS"		,0x00,0x00,ID_SetMemoryType	 },
{ATGEN_ReplyGetSMSStatus,	"AT+CPMS"		,0x00,0x00,ID_GetSMSStatus	 },
{ATGEN_ReplyGetSMSMemories,	"AT+CPMS=?"		,0x00,0x00,ID_GetSMSMemories	 },
{ATGEN_ReplyAddSMSMessage,	"AT+CMGW"		,0x00,0x00,ID_SaveSMSMessage	 },
{ATGEN_GenericReply,		"AT+CSMP"		,0x00,0x00,ID_SetSMSParameters	 },
{ATGEN_GenericReply,		"AT+CSCA"		,0x00,0x00,ID_SetSMSC		 },
{ATGEN_ReplyGetSMSC,		"AT+CSCA?"		,0x00,0x00,ID_GetSMSC		 },
{ATGEN_ReplyDeleteSMSMessage,	"AT+CMGD"		,0x00,0x00,ID_DeleteSMSMessage	 },
{ATGEN_GenericReply,		"ATE1"			,0x00,0x00,ID_SetSMSParameters	 },
{ATGEN_GenericReply,		"\x1b\x0D"		,0x00,0x00,ID_SetSMSParameters	 },
{ATGEN_GenericReply,		"AT+CMMS"		,0x00,0x00,ID_SetFastSMSSending  },
{ATGEN_IncomingSMSInfo,		"+CMTI:" 	 	,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_IncomingSMSDeliver,	"+CMT:" 	 	,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_IncomingSMSReport,	"+CDS:" 	 	,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_IncomingSMSCInfo,	"^SCN:"			,0x00,0x00,ID_IncomingFrame	 },

{ATGEN_ReplyGetDateTime_Alarm,	"AT+CCLK?"		,0x00,0x00,ID_GetDateTime	 },
{ATGEN_GenericReply,		"AT+CCLK="		,0x00,0x00,ID_SetDateTime	 },
{ATGEN_GenericReply,		"AT+CALA="		,0x00,0x00,ID_SetAlarm		 },
{ATGEN_ReplyGetDateTime_Alarm,	"AT+CALA?"		,0x00,0x00,ID_GetAlarm		 },

{ATGEN_ReplyGetNetworkLAC_CID,	"AT+CREG?"		,0x00,0x00,ID_GetNetworkInfo	 },
{ATGEN_GenericReply,		"AT+CREG=2"		,0x00,0x00,ID_GetNetworkInfo	 },
{ATGEN_GenericReply,		"AT+COPS="		,0x00,0x00,ID_GetNetworkInfo	 },
{ATGEN_GenericReply,		"AT+COPS="		,0x00,0x00,ID_SetAutoNetworkLogin},
{ATGEN_ReplyGetNetworkCode,	"AT+COPS"		,0x00,0x00,ID_GetNetworkInfo	 },
{ATGEN_ReplyGetSignalQuality,	"AT+CSQ"		,0x00,0x00,ID_GetSignalQuality	 },
{ATGEN_IncomingNetworkLevel,	"_OSIGQ:"	 	,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_IncomingGPRS,		"+CGREG:"	 	,0x00,0x00,ID_IncomingFrame      },
{ATGEN_ReplyGetNetworkLAC_CID,	"+CREG:"		,0x00,0x00,ID_IncomingFrame	 },

{ATGEN_ReplyGetPBKMemories,	"AT+CPBS=?"		,0x00,0x00,ID_SetMemoryType	 },
{ATGEN_GenericReply,		"AT+CPBS="		,0x00,0x00,ID_SetMemoryType	 },
{ATGEN_ReplyGetCPBSMemoryStatus,"AT+CPBS?"		,0x00,0x00,ID_GetMemoryStatus	 },
// /* Samsung phones reply +CPBR: after OK --claudio*/
{ATGEN_ReplyGetCPBRMemoryInfo,	"AT+CPBR=?"		,0x00,0x00,ID_GetMemoryStatus	 },
{ATGEN_ReplyGetCPBRMemoryInfo,	"+CPBR:"		,0x00,0x00,ID_GetMemoryStatus	 },
{ATGEN_ReplyGetCPBRMemoryStatus,"AT+CPBR="		,0x00,0x00,ID_GetMemoryStatus	 },
{ATGEN_ReplyGetCharsets,	"AT+CSCS=?"		,0x00,0x00,ID_GetMemoryCharset	 },
{ATGEN_ReplyGetCharset,		"AT+CSCS?"		,0x00,0x00,ID_GetMemoryCharset	 },
{ATGEN_GenericReply,		"AT+CSCS="		,0x00,0x00,ID_SetMemoryCharset	 },
{ATGEN_ReplyGetMemory,		"AT+CPBR="		,0x00,0x00,ID_GetMemory		 },
{SIEMENS_ReplyGetMemoryInfo,	"AT^SBNR=?"		,0x00,0x00,ID_GetMemory		 },
{SIEMENS_ReplyGetMemory,	"AT^SBNR"		,0x00,0x00,ID_GetMemory		 },
{ATGEN_ReplySetMemory,		"AT+CPBW"		,0x00,0x00,ID_SetMemory		 },

{SIEMENS_ReplyGetBitmap,	"AT^SBNR=\"bmp\""	,0x00,0x00,ID_GetBitmap	 	 },
{SIEMENS_ReplySetBitmap,	"AT^SBNW=\"bmp\""	,0x00,0x00,ID_SetBitmap	 	 },

{SIEMENS_ReplyGetRingtone,	"AT^SBNR=\"mid\""	,0x00,0x00,ID_GetRingtone	 },
{SIEMENS_ReplySetRingtone,	"AT^SBNW=\"mid\""	,0x00,0x00,ID_SetRingtone	 },

{SIEMENS_ReplyGetNextCalendar,	"AT^SBNR=\"vcs\""	,0x00,0x00,ID_GetCalendarNote	 },
{SIEMENS_ReplyAddCalendarNote,	"AT^SBNW=\"vcs\""	,0x00,0x00,ID_SetCalendarNote	 },
{SIEMENS_ReplyDelCalendarNote,	"AT^SBNW=\"vcs\""	,0x00,0x00,ID_DeleteCalendarNote },

{ATGEN_ReplyEnterSecurityCode,	"AT+CPIN="		,0x00,0x00,ID_EnterSecurityCode	 },
{ATGEN_ReplyEnterSecurityCode,	"AT+CPIN2="		,0x00,0x00,ID_EnterSecurityCode	 },
{ATGEN_ReplyGetSecurityStatus,	"AT+CPIN?"		,0x00,0x00,ID_GetSecurityStatus	 },
{ATGEN_ReplyOK,			"OK"			,0x00,0x00,ID_IncomingFrame	 },

{ATGEN_GenericReply, 		"AT+VTS"		,0x00,0x00,ID_SendDTMF		 },
{ATGEN_ReplyCancelCall,		"AT+CHUP"		,0x00,0x00,ID_CancelCall	 },
{ATGEN_ReplyDialVoice,		"ATDT"			,0x00,0x00,ID_DialVoice		 },
{ATGEN_ReplyCancelCall,		"ATH"			,0x00,0x00,ID_CancelCall	 },
{ATGEN_GenericReply, 		"AT+CUSD"		,0x00,0x00,ID_SetUSSD		 },
{ATGEN_ReplyGetUSSD, 		"+CUSD"			,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_GenericReply,            "AT+CLIP=1"      	,0x00,0x00,ID_IncomingFrame      },
{ATGEN_ReplyIncomingCallInfo,	"+CLIP"			,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_ReplyIncomingCallInfo,	"+COLP"    		,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_ReplyIncomingCallInfo,	"RING"			,0x00,0x00,ID_IncomingFrame	 },
{ATGEN_ReplyIncomingCallInfo,	"NO CARRIER"		,0x00,0x00,ID_IncomingFrame	 },

{ATGEN_ReplyReset,		"AT^SRESET"		,0x00,0x00,ID_Reset		 },
{ATGEN_ReplyReset,		"AT+CFUN=1,1"		,0x00,0x00,ID_Reset		 },
{ATGEN_ReplyResetPhoneSettings, "AT&F"			,0x00,0x00,ID_ResetPhoneSettings },

{SAMSUNG_ReplyGetBitmap,	"AT+IMGR="		,0x00,0x00,ID_GetBitmap	 	 },
{SAMSUNG_ReplySetBitmap,	"SDNDCRC ="		,0x00,0x00,ID_SetBitmap		 },

{SAMSUNG_ReplyGetRingtone,	"AT+MELR="		,0x00,0x00,ID_GetRingtone	 },
{SAMSUNG_ReplySetRingtone,	"SDNDCRC ="		,0x00,0x00,ID_SetRingtone	 },

#ifdef GSM_ENABLE_SONYERICSSON
{ATGEN_GenericReply,		"AT*EOBEX=?"		,0x00,0x00,ID_SetOBEX		 },
{ATGEN_GenericReply,		"AT*EOBEX"		,0x00,0x00,ID_SetOBEX		 },
{ATGEN_GenericReply,		"AT+CPROT=0" 	 	,0x00,0x00,ID_SetOBEX		 },

{ATGEN_GenericReply,		"AT*ESDF="		,0x00,0x00,ID_SetLocale		 },
{ATGEN_GenericReply,		"AT*ESTF="		,0x00,0x00,ID_SetLocale		 },

{SONYERICSSON_ReplyGetDateLocale,	"AT*ESDF?"	,0x00,0x00,ID_GetLocale		 },
{SONYERICSSON_ReplyGetTimeLocale,	"AT*ESTF?"	,0x00,0x00,ID_GetLocale	 	 },
{SONYERICSSON_ReplyGetFileSystemStatus,	"AT*EMEM"	,0x00,0x00,ID_FileSystemStatus 	 },
{ATGEN_GenericReply,			"AT*EBCA"	,0x00,0x00,ID_GetBatteryCharge 	 },
{SONYERICSSON_ReplyGetBatteryCharge,	"*EBCA:"	,0x00,0x00,ID_IncomingFrame	 },
#endif
#ifdef GSM_ENABLE_ALCATEL
/*  Why do I give Alcatel specific things here? It's simple, Alcatel needs
 *  some AT commands to start it's binary mode, so this needs to be in AT
 *  related stuff.
 *
 *  XXX: AT+IFC could later move outside this ifdef, because it is not Alcatel
 *  specific and it's part of ETSI specifications
 */
{ATGEN_GenericReply,		"AT+IFC" 	 	,0x00,0x00,ID_SetFlowControl  	 },
{ALCATEL_ProtocolVersionReply,	"AT+CPROT=?" 	 	,0x00,0x00,ID_AlcatelProtocol    },
{ATGEN_GenericReply,		"AT+CPROT=16" 	 	,0x00,0x00,ID_AlcatelConnect	 },
#endif

{NULL,				"\x00"			,0x00,0x00,ID_None		 }
};

GSM_Phone_Functions ATGENPhone = {
	"A2D|iPAQ|at|M20|S25|MC35|TC35|C35i|S65|S300|5110|5130|5190|5210|6110|6130|6150|6190|6210|6250|6310|6310i|6510|7110|8210|8250|8290|8310|8390|8850|8855|8890|8910|9110|9210",
	ATGENReplyFunctions,
	ATGEN_Initialise,
	ATGEN_Terminate,
	ATGEN_DispatchMessage,
	NOTSUPPORTED,			/* 	ShowStartInfo		*/
	ATGEN_GetManufacturer,
	ATGEN_GetModel,
	ATGEN_GetFirmware,
	ATGEN_GetIMEI,
	NOTSUPPORTED,			/*	GetOriginalIMEI		*/
	NOTSUPPORTED,			/*	GetManufactureMonth	*/
	NOTSUPPORTED,			/*	GetProductCode		*/
	NOTSUPPORTED,			/*	GetHardware		*/
	NOTSUPPORTED,			/*	GetPPM			*/
	ATGEN_GetSIMIMSI,
	ATGEN_GetDateTime,
	ATGEN_SetDateTime,
	ATGEN_GetAlarm,
	ATGEN_SetAlarm,
	NOTSUPPORTED,			/*	GetLocale		*/
	NOTSUPPORTED,			/*	SetLocale		*/
	ATGEN_PressKey,
	ATGEN_Reset,
	ATGEN_ResetPhoneSettings,
	ATGEN_EnterSecurityCode,
	ATGEN_GetSecurityStatus,
	ATGEN_GetDisplayStatus,
	ATGEN_SetAutoNetworkLogin,
	ATGEN_GetBatteryCharge,
	ATGEN_GetSignalQuality,
	ATGEN_GetNetworkInfo,
 	NOTSUPPORTED,       		/*  	GetCategory 		*/
 	NOTSUPPORTED,       		/*  	AddCategory 		*/
 	NOTSUPPORTED,        		/*  	GetCategoryStatus 	*/
	ATGEN_GetMemoryStatus,
	ATGEN_GetMemory,
	ATGEN_GetNextMemory,
	ATGEN_SetMemory,
	ATGEN_AddMemory,
	ATGEN_DeleteMemory,
	ATGEN_DeleteAllMemory,
	NOTSUPPORTED,			/*	GetSpeedDial		*/
	NOTSUPPORTED,			/*	SetSpeedDial		*/
	ATGEN_GetSMSC,
	ATGEN_SetSMSC,
	ATGEN_GetSMSStatus,
	ATGEN_GetSMS,
	ATGEN_GetNextSMS,
	NOTSUPPORTED,			/*	SetSMS			*/
	ATGEN_AddSMS,
	ATGEN_DeleteSMS,
	ATGEN_SendSMS,
	ATGEN_SendSavedSMS,
	ATGEN_SetFastSMSSending,
	ATGEN_SetIncomingSMS,
	ATGEN_SetIncomingCB,
	ATGEN_GetSMSFolders,
 	NOTSUPPORTED,			/* 	AddSMSFolder		*/
 	NOTSUPPORTED,			/* 	DeleteSMSFolder		*/
	ATGEN_DialVoice,
	ATGEN_AnswerCall,
	ATGEN_CancelCall,
 	NOTSUPPORTED,			/* 	HoldCall 		*/
 	NOTSUPPORTED,			/* 	UnholdCall 		*/
 	NOTSUPPORTED,			/* 	ConferenceCall 		*/
 	NOTSUPPORTED,			/* 	SplitCall		*/
 	NOTSUPPORTED,			/* 	TransferCall		*/
 	NOTSUPPORTED,			/* 	SwitchCall		*/
 	NOTSUPPORTED,			/* 	GetCallDivert		*/
 	NOTSUPPORTED,			/* 	SetCallDivert		*/
 	NOTSUPPORTED,			/* 	CancelAllDiverts	*/
	NONEFUNCTION,			/*	SetIncomingCall		*/
	ATGEN_SetIncomingUSSD,
	ATGEN_SendDTMF,
	ATGEN_GetRingtone,
	ATGEN_SetRingtone,
	NOTSUPPORTED,			/*	GetRingtonesInfo	*/
	NOTSUPPORTED,			/* 	DeleteUserRingtones	*/
	NOTSUPPORTED,			/*	PlayTone		*/
	NOTSUPPORTED,			/* 	GetWAPBookmark		*/
	NOTSUPPORTED,			/* 	SetWAPBookmark 		*/
	NOTSUPPORTED,	 		/* 	DeleteWAPBookmark 	*/
	NOTSUPPORTED,			/* 	GetWAPSettings 		*/
	NOTSUPPORTED,			/* 	SetWAPSettings 		*/
	NOTSUPPORTED,			/*	GetSyncMLSettings	*/
	NOTSUPPORTED,			/*	SetSyncMLSettings	*/
	NOTSUPPORTED,			/*	GetChatSettings		*/
	NOTSUPPORTED,			/*	SetChatSettings		*/
	NOTSUPPORTED,			/* 	GetMMSSettings		*/
	NOTSUPPORTED,			/* 	SetMMSSettings		*/
	NOTSUPPORTED,			/*	GetMMSFolders		*/
	NOTSUPPORTED,			/*	GetNextMMSFileInfo	*/
	ATGEN_GetBitmap,		/* 	GetBitmap		*/
	ATGEN_SetBitmap,		/*	SetBitmap		*/
	NOTSUPPORTED,			/*	GetToDoStatus		*/
	NOTSUPPORTED,			/*	GetToDo			*/
	NOTSUPPORTED,			/*	GetNextToDo		*/
	NOTSUPPORTED,			/*	SetToDo			*/
	NOTSUPPORTED,			/*	AddToDo			*/
	NOTSUPPORTED,			/*	DeleteToDo		*/
	NOTSUPPORTED,			/*	DeleteAllToDo		*/
	NOTSUPPORTED,			/*	GetCalendarStatus	*/
	NOTIMPLEMENTED,			/*	GetCalendar		*/
    	ATGEN_GetNextCalendar,
	ATGEN_SetCalendarNote,
	ATGEN_AddCalendarNote,
	ATGEN_DelCalendarNote,
	NOTIMPLEMENTED,			/*	DeleteAllCalendar	*/
	NOTSUPPORTED,			/* 	GetCalendarSettings	*/
	NOTSUPPORTED,			/* 	SetCalendarSettings	*/
	NOTSUPPORTED,			/*	GetNoteStatus		*/
	NOTSUPPORTED,			/*	GetNote			*/
	NOTSUPPORTED,			/*	GetNextNote		*/
	NOTSUPPORTED,			/*	SetNote			*/
	NOTSUPPORTED,			/*	AddNote			*/
	NOTSUPPORTED,			/* 	DeleteNote		*/
	NOTSUPPORTED,			/*	DeleteAllNotes		*/
	NOTSUPPORTED, 			/*	GetProfile		*/
	NOTSUPPORTED, 			/*	SetProfile		*/
    	NOTSUPPORTED,			/*  	GetFMStation        	*/
    	NOTSUPPORTED,			/* 	SetFMStation        	*/
    	NOTSUPPORTED,			/* 	ClearFMStations       	*/
	NOTSUPPORTED,			/* 	GetNextFileFolder	*/
	NOTSUPPORTED,			/*	GetFolderListing	*/
	NOTSUPPORTED,			/*	GetNextRootFolder	*/
	NOTSUPPORTED,			/*	SetFileAttributes	*/
	NOTSUPPORTED,			/* 	GetFilePart		*/
	NOTSUPPORTED,			/* 	AddFile			*/
	NOTSUPPORTED, 			/* 	GetFileSystemStatus	*/
	NOTSUPPORTED,			/* 	DeleteFile		*/
	NOTSUPPORTED,			/* 	AddFolder		*/
	NOTSUPPORTED,			/* 	DeleteFolder		*/
	NOTSUPPORTED,			/* 	GetGPRSAccessPoint	*/
	NOTSUPPORTED			/* 	SetGPRSAccessPoint	*/
};

#endif

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
