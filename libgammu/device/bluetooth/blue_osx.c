/*

  Based over gnokii code, addapation to Gammu by Michal Čihař

  Gnokii is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Gnokii is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with gnokii; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  Copyright (C) 1999-2000  Hugh Blemings & Pavel Janík ml.
  Copyright (C) 2003       Siegfried Schloissnig

*/

#include "../../gsmstate.h"

#ifdef GSM_ENABLE_BLUETOOTHDEVICE
#ifdef OSX_BLUE_FOUND

#include <CoreFoundation/CoreFoundation.h>
#include <IOBluetooth/Bluetooth.h>
#include <IOBluetooth/IOBluetoothUtilities.h>
#include <IOBluetooth/IOBluetoothUserLib.h>
#include <pthread.h>

typedef struct {
	IOBluetoothRFCOMMChannelRef rfcommChannel;
	IOReturn ioReturnValue;
	pthread_t threadID;

	BluetoothDeviceAddress deviceAddress;
	uint8_t nChannel;

	pthread_mutex_t mutexWait;

	CFMutableArrayRef arrDataReceived;
} threadContext;

typedef struct {
	void *pData;
	unsigned int nSize;
} dataBlock;

static void thread_rfcommDataListener(IOBluetoothRFCOMMChannelRef rfcommChannel,
                               void* data, UInt16 length, void* refCon)
{
	threadContext *pContext = (threadContext *)refCon;
	void *pBuffer = malloc(length);
	dataBlock *pDataBlock = (dataBlock *)malloc(sizeof(dataBlock));

	memcpy(pBuffer, data, length);

	pDataBlock->pData = pBuffer;
	pDataBlock->nSize = length;

	pthread_mutex_lock(&(pContext->mutexWait));
	CFArrayAppendValue(pContext->arrDataReceived, pDataBlock);
	pthread_mutex_unlock(&(pContext->mutexWait));
}

#ifdef OSX_BLUE_2_0
void thread_rfcommEventListener (IOBluetoothRFCOMMChannelRef rfcommChannel,
			void *refCon, IOBluetoothRFCOMMChannelEvent *event)
{
        if (event->eventType == kIOBluetoothRFCOMMNewDataEvent) {
		thread_rfcommDataListener(rfcommChannel, event->u.newData.dataPtr, event->u.newData.dataSize , refCon);
	}
}
#endif



static void *thread_main(void *pArg)
{
	threadContext* pContext = (threadContext *)pArg;
	IOBluetoothDeviceRef device = IOBluetoothDeviceCreateWithAddress(&(pContext->deviceAddress));
	IOBluetoothRFCOMMChannelRef rfcommChannel;

#ifdef OSX_BLUE_2_0
	if (IOBluetoothDeviceOpenRFCOMMChannelSync(device, &rfcommChannel, pContext->nChannel,
			thread_rfcommEventListener, pArg) != kIOReturnSuccess) {
		rfcommChannel = 0;
	}
#else
	if (IOBluetoothDeviceOpenRFCOMMChannel(device, pContext->nChannel,
				      &rfcommChannel) != kIOReturnSuccess) {
		rfcommChannel = 0;
	} else {
		/* register an incoming data listener */
		if (IOBluetoothRFCOMMChannelRegisterIncomingDataListener(rfcommChannel,
			 thread_rfcommDataListener, pArg) != kIOReturnSuccess) {
		    rfcommChannel = 0;
		}
	}
#endif

	pContext->rfcommChannel = rfcommChannel;

	pthread_mutex_unlock(&(pContext->mutexWait));

	/* start the runloop */
	CFRunLoopRun();

	return NULL;
}

/* ---- bluetooth io thread ---- */

GSM_Error bluetooth_connect(GSM_StateMachine *s, int port, char *device)
{
	GSM_Device_BlueToothData 	*d = &s->Device.Data.BlueTooth;
	/* create the thread context and start the thread */
	CFStringRef strDevice;
	threadContext *pContext = (threadContext *)malloc(sizeof(threadContext));
	if (pContext == NULL) return ERR_MOREMEMORY;

	strDevice = CFStringCreateWithCString(kCFAllocatorDefault, device, kCFStringEncodingMacRomanLatin1);
	IOBluetoothCFStringToDeviceAddress(strDevice, &pContext->deviceAddress);
	CFRelease(strDevice);

	pContext->arrDataReceived = CFArrayCreateMutable(kCFAllocatorDefault, 0, NULL);
	pContext->rfcommChannel = 0;
	pContext->nChannel = port;

	pthread_mutex_init(&(pContext->mutexWait), NULL);
	pthread_mutex_lock(&(pContext->mutexWait));	/* lock */

	pthread_create(&(pContext->threadID), NULL, thread_main, pContext);

	/* wait until main finishes its initialization */
	pthread_mutex_lock(&(pContext->mutexWait));
	/* unblock the mutex */
	pthread_mutex_unlock(&(pContext->mutexWait));

	if (pContext->rfcommChannel == 0) {
		return ERR_DEVICEOPENERROR;
	} else {
		/* return the thread context as the file descriptor */
		d->Data = pContext;
		return ERR_NONE;
	}
}

GSM_Error bluetooth_close(GSM_StateMachine *s)
{
	GSM_Device_BlueToothData 	*d = &s->Device.Data.BlueTooth;
	threadContext *pContext = (threadContext *)d->Data;
        IOBluetoothDeviceRef device;

	sleep(2);

	if (pContext != NULL && pContext->rfcommChannel > 0) {
#ifndef OSX_BLUE_2_0
		/* de-register the callback */
		IOBluetoothRFCOMMChannelRegisterIncomingDataListener(pContext->rfcommChannel, NULL, NULL);
#endif

		/* close channel and device connection */
		IOBluetoothRFCOMMChannelCloseChannel(pContext->rfcommChannel);
		device = IOBluetoothRFCOMMChannelGetDevice(pContext->rfcommChannel);
		IOBluetoothDeviceCloseConnection(device);
		IOBluetoothObjectRelease(pContext->rfcommChannel);
		IOBluetoothObjectRelease(device);
	}

	return ERR_NONE;
}

int bluetooth_write(GSM_StateMachine *s, const void *buf, size_t nbytes)
{
	GSM_Device_BlueToothData 	*d = &s->Device.Data.BlueTooth;
	threadContext *pContext = (threadContext *)d->Data;

#ifdef OSX_BLUE_2_0
	if (IOBluetoothRFCOMMChannelWriteSync(pContext->rfcommChannel, (void *)buf, nbytes) != kIOReturnSuccess)
		return -1;
#else
	if (IOBluetoothRFCOMMChannelWrite(pContext->rfcommChannel, (void *)buf, nbytes, TRUE) != kIOReturnSuccess)
		return -1;
#endif

	return nbytes;
}

int bluetooth_read(GSM_StateMachine *s, void *buffer, size_t size)
{
	GSM_Device_BlueToothData 	*d = &s->Device.Data.BlueTooth;
	threadContext *pContext = (threadContext *)d->Data;
	int nOffset = 0;
	int nBytes = 0;
        dataBlock* pDataBlock;

	/* no data received so far */
	if (CFArrayGetCount(pContext->arrDataReceived) == 0)
		return 0;

	while (CFArrayGetCount(pContext->arrDataReceived) != 0) {
		pthread_mutex_lock(&(pContext->mutexWait));
		pDataBlock = (dataBlock*)CFArrayGetValueAtIndex(pContext->arrDataReceived, 0);
		pthread_mutex_unlock(&(pContext->mutexWait));

		if (pDataBlock->nSize == size) {
			/* copy data and remove block */
			memcpy(((char *)buffer) + nOffset, pDataBlock->pData, size);

			pthread_mutex_lock(&(pContext->mutexWait));
			CFArrayRemoveValueAtIndex(pContext->arrDataReceived, 0);
			pthread_mutex_unlock(&(pContext->mutexWait));

			free(pDataBlock->pData);
			pDataBlock->pData=NULL;
			free(pDataBlock);
			pDataBlock=NULL;

			return nBytes + size;
		} else if (pDataBlock->nSize > size) {
			/* copy data and update block contents */
			memcpy(((char *)buffer) + nOffset, pDataBlock->pData, size);
			memmove(pDataBlock->pData, ((char *)pDataBlock->pData) + size, pDataBlock->nSize - size);
			pDataBlock->nSize -= size;
			return nBytes + size;
		} else { /* pDataBlock->nSize < size */
			/* copy data and remove block */
			memcpy(((char *)buffer) + nOffset, pDataBlock->pData, pDataBlock->nSize);

			size -= pDataBlock->nSize;
			nOffset += pDataBlock->nSize;
			nBytes += pDataBlock->nSize;

			pthread_mutex_lock(&(pContext->mutexWait));
			CFArrayRemoveValueAtIndex(pContext->arrDataReceived, 0);
			pthread_mutex_unlock(&(pContext->mutexWait));

			free(pDataBlock->pData);
			pDataBlock->pData=NULL;
			free(pDataBlock);
			pDataBlock=NULL;
		}
	}

	return nBytes;
}


#endif
#endif

/* How should editor hadle tabs in this file? Add editor commands here.
 * vim: noexpandtab sw=8 ts=8 sts=8:
 */
