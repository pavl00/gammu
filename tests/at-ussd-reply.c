/* Test for parsing USSD reply on AT driver */

#include <gammu.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../libgammu/protocol/protocol.h"	/* Needed for GSM_Protocol_Message */
#include "../libgammu/gsmstate.h"	/* Needed for state machine internals */
#include "../libgammu/gsmphones.h"	/* Phone data */

#include "common.h"

#define BUFFER_SIZE 16384

extern GSM_Error ATGEN_ReplyGetUSSD(GSM_Protocol_Message *msg, GSM_StateMachine * s);

void IncomingUSSD(GSM_StateMachine *sm UNUSED, GSM_USSDMessage *ussd, void *user_data UNUSED)
{
        printf("Status: %d\n", ussd->Status);
        printf("Response: %s\n", DecodeUnicodeConsole(ussd->Text));
}



int main(int argc, char **argv)
{
	GSM_Debug_Info *debug_info;
	GSM_Phone_ATGENData *Priv;
	GSM_Phone_Data *Data;
	unsigned char buffer[BUFFER_SIZE];
	FILE *f;
	size_t len;
	GSM_StateMachine *s;
	GSM_Protocol_Message msg;
	GSM_Error error;

	/* Check parameters */
	if (argc != 3) {
		printf("Not enough parameters!\nUsage: at-ussd-reply comm.dump MODEL\n");
		return 1;
	}

	/* Open file */
	f = fopen(argv[1], "r");
	if (f == NULL) {
		printf("Could not open %s\n", argv[1]);
		return 1;
	}

	/* Read data */
	len = fread(buffer, 1, sizeof(buffer) - 1, f);
	if (!feof(f)) {
		printf("Could not read whole file %s\n", argv[1]);
		fclose(f);
		return 1;
	}
	/* Zero terminate data */
	buffer[len] = 0;

	/* Close file */
	fclose(f);

	/* Configure state machine */
	GSM_InitLocales(NULL);
	debug_info = GSM_GetGlobalDebug();
	GSM_SetDebugFileDescriptor(stderr, FALSE, debug_info);
	GSM_SetDebugLevel("textall", debug_info);

	/* Allocates state machine */
	s = GSM_AllocStateMachine();
	test_result(s != NULL);
	debug_info = GSM_GetDebug(s);
	GSM_SetDebugGlobal(TRUE, debug_info);

	/* Initialize AT engine */
	Data = &s->Phone.Data;
	Data->ModelInfo = GetModelData(NULL, NULL, argv[2], NULL);
	Priv = &s->Phone.Data.Priv.ATGEN;
	Priv->ReplyState = AT_Reply_OK;
	Priv->SMSMode = SMS_AT_PDU;
	Priv->Charset = AT_CHARSET_GSM;
	s->User.IncomingUSSD = IncomingUSSD;
	s->User.IncomingUSSDUserData = NULL;
	s->Phone.Data.EnableIncomingUSSD = TRUE;

	/* Init message */
	msg.Type = 0;
	msg.Length = len;
	msg.Buffer = buffer;
	SplitLines(msg.Buffer, msg.Length, &Priv->Lines, "\x0D\x0A", 2, "\"", 1, TRUE);

	/* Parse it */
	error = ATGEN_ReplyGetUSSD(&msg, s);

	/* This is normally done by ATGEN_Terminate */
	FreeLines(&Priv->Lines);
	GetLineString(NULL, NULL, 0);

	/* Free state machine */
	GSM_FreeStateMachine(s);

	gammu_test_result(error, "ATGEN_ReplyGetUSSD");

	return 0;
}

/* Editor configuration
 * vim: noexpandtab sw=8 ts=8 sts=8 tw=72:
 */
