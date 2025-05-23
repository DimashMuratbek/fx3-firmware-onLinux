/*
 ## Cypress USB 3.0 Platform header file (cyfxgpiftousb.h)
 ## ===========================
 ##
 ##  Copyright Cypress Semiconductor Corporation, 2010-2018,
 ##  All Rights Reserved
 ##  UNPUBLISHED, LICENSED SOFTWARE.
 ##
 ##  CONFIDENTIAL AND PROPRIETARY INFORMATION
 ##  WHICH IS THE PROPERTY OF CYPRESS.
 ##
 ##  Use of this file is governed
 ##  by the license agreement included in the file
 ##
 ##     <install>/license/license.txt
 ##
 ##  where <install> is the Cypress software
 ##  installation root directory path.
 ##
 ## ===========================
*/

/* This file contains the constants used by the bulk source sink application example */

#ifndef _INCLUDED_CYFXGPIFTOUSB_H_
#define _INCLUDED_CYFXGPIFTOUSB_H_

#include "cyu3types.h"
#include "cyu3usbconst.h"
#include "cyu3externcstart.h"

#define CY_FX_GPIFTOUSB_DMA_TX_SIZE        (0)                       /* DMA transfer size is set to infinite */
#define CY_FX_GPIFTOUSB_THREAD_STACK       (0x1000)                  /* Bulk loop application thread stack size */
#define CY_FX_GPIFTOUSB_THREAD_PRIORITY    (8)                       /* Bulk loop application thread priority */
#define CY_FX_GPIFTOUSB_PATTERN            (0xAA)                    /* 8-bit pattern to be loaded to the source buffers. */

/* Endpoint and socket definitions for the bulk source sink application */

/* To change the producer and consumer EP enter the appropriate EP numbers for the #defines.
 * In the case of IN endpoints enter EP number along with the direction bit.
 * For eg. EP 6 IN endpoint is 0x86
 *     and EP 6 OUT endpoint is 0x06.
 * To change sockets mention the appropriate socket number in the #defines. */

/* Note: For USB 2.0 the endpoints and corresponding sockets are one-to-one mapped
         i.e. EP 1 is mapped to UIB socket 1 and EP 2 to socket 2 so on */

#define CY_FX_EP_CONSUMER               0x82    /*was 0x82 EP 2 IN */
#define CY_FX_EP_CONSUMER_SOCKET        CY_U3P_UIB_SOCKET_CONS_2  /*changed to 1, was 2*/  /* Socket 1 is consumer */
#define CY_FX_GPIF_PRODUCER_SOCKET      CY_U3P_PIB_SOCKET_0 /* was 0, chnaged to 1 */

/* Burst mode definitions: Only for super speed operation. The maximum burst mode 
 * supported is limited by the USB hosts available. The maximum value for this is 16
 * and the minimum (no-burst) is 1. */

/* Burst length in 1 KB packets. Only applicable to USB 3.0. */
#ifndef CY_FX_EP_BURST_LENGTH
#define CY_FX_EP_BURST_LENGTH           (16)
#endif

/* Size of DMA buffers used by the application. */
#ifndef CY_FX_DMA_BUF_SIZE
#define CY_FX_DMA_BUF_SIZE              (9216)  /* 4096 Increased for 32 channels   4608 for 8dig,8 ana*/
#endif

/* Number of DMA buffers to be used on the channel. */
#ifndef CY_FX_DMA_BUF_COUNT
#define CY_FX_DMA_BUF_COUNT             (8)          /*4 Increased buffer count for better buffering */
#endif

/* Parameters for loop-back function. */
#define CY_FX_EP_LOOP_OUT               (0x03)
#define CY_FX_EP_LOOP_IN                (0x83)
#define CY_FX_LOOP_PRODUCER_SOCK        CY_U3P_UIB_SOCKET_PROD_3
#define CY_FX_LOOP_CONSUMER_SOCK        CY_U3P_UIB_SOCKET_CONS_3

/* Extern definitions for the USB Descriptors */
extern const uint8_t CyFxUSB20DeviceDscr[];
extern const uint8_t CyFxUSB30DeviceDscr[];
extern const uint8_t CyFxUSBDeviceQualDscr[];
extern const uint8_t CyFxUSBFSConfigDscr[];
extern const uint8_t CyFxUSBHSConfigDscr[];
extern const uint8_t CyFxUSBBOSDscr[];
extern const uint8_t CyFxUSBSSConfigDscr[];
extern const uint8_t CyFxUSBStringLangIDDscr[];
extern const uint8_t CyFxUSBManufactureDscr[];
extern const uint8_t CyFxUSBProductDscr[];
extern const uint8_t CyFxUsbOSDscr[];

#include <cyu3externcend.h>

#endif /* _INCLUDED_CYFXGPIFTOUSB_H_ */

/*[]*/
