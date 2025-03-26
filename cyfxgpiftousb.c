/*
 ## Cypress USB 3.0 Platform source file (cyfxgpiftousb.c)
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

/*
    This is a sample application that demonstrates data transfer from the FX3 GPIF port to the
    USB port.

    The application makes use a simple GPIF configuration which continually latches the state
    of the GPIF data pins and fills them into full data packets to be sent to the USB host through
    a BULK-IN endpoint. The GPIF configuration and DMA data path are setup to facilitate the fastest
    possible data transfer.

    This application also demonstrates the use of the endpoint specific CYU3P_USBEP_SS_RESET_EVT
    event to detect and recover from potential USB data corruption due to protocol level errors.
 */

#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyfxgpiftousb.h"
#include "cyu3usb.h"
#include "cyu3uart.h"
#include "cyu3gpio.h"
#include "cyu3utils.h"
#include "cyu3pib.h"
#include "cyu3gpif.h"
#include "cyfxgpif2config.h"

CyU3PThread     glAppThread;            /* Application thread structure */
CyU3PDmaChannel glDmaChHandle;
CyU3PDmaChannel glLoopOutChHandle;
CyU3PDmaChannel glLoopInChHandle;

CyBool_t glIsApplnActive = CyFalse;     /* Whether the application is active or not. */
CyBool_t glForceLinkU2   = CyFalse;     /* Whether the device should try to initiate U2 mode. */

uint16_t glEp0Buffer[64] __attribute__ ((aligned (32))); /* Local buffer used for vendor command handling. */
volatile uint8_t  vendorRqtCnt = 0;
volatile uint32_t underrunCnt = 0;
volatile CyBool_t glRstRqt = CyFalse;

/*
 * USB event logging: We use a 4 KB buffer to store USB driver event data, which can then be viewed
 * through JTAG or USB vendor request.
 */
#define CYFX_USBLOG_SIZE (0x1000)
uint8_t *gl_UsbLogBuffer = NULL;

static volatile uint32_t BulkEpEvtCount = 0;    /* Number of endpoint events received on streaming endpoint. */

/* Application Error Handler */
void
CyFxAppErrorHandler (
        CyU3PReturnStatus_t apiRetStatus    /* API return status */
        )
{
    /* Application failed with the error code apiRetStatus */

    /* Add custom debug or recovery actions here */

    /* Let's do a device reset here. */
    CyU3PThreadSleep (100);
    CyU3PDeviceReset (CyFalse);

    /* Loop Indefinitely */
    for (;;)
    {
        /* Thread sleep : 100 ms */
        CyU3PThreadSleep (100);
    }
}

/* This function initializes the debug module. The debug prints
 * are routed to the UART and can be seen using a UART console
 * running at 115200 baud rate. */
void
CyFxApplnDebugInit (void)
{
    CyU3PUartConfig_t uartConfig;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Initialize the UART for printing debug messages */
    apiRetStatus = CyU3PUartInit();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error handling */
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set UART configuration */
    CyU3PMemSet ((uint8_t *)&uartConfig, 0, sizeof (uartConfig));
    uartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
    uartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
    uartConfig.parity = CY_U3P_UART_NO_PARITY;
    uartConfig.txEnable = CyTrue;
    uartConfig.rxEnable = CyFalse;
    uartConfig.flowCtrl = CyFalse;
    uartConfig.isDma = CyTrue;

    apiRetStatus = CyU3PUartSetConfig (&uartConfig, NULL);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set the UART transfer to a really large value. */
    apiRetStatus = CyU3PUartTxSetBlockXfer (0xFFFFFFFF);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Initialize the debug module. */
    apiRetStatus = CyU3PDebugInit (CY_U3P_LPP_SOCKET_UART_CONS, 8);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyFxAppErrorHandler(apiRetStatus);
    }

    CyU3PDebugPreamble(CyFalse);
}
uint32_t glProdCount = 0;
uint32_t glConsCount = 0;

GpifToUsbDmaCallback (
        CyU3PDmaChannel   *chHandle,
        CyU3PDmaCbType_t   type,
        CyU3PDmaCBInput_t *input)
{
    if (type == CY_U3P_DMA_CB_PROD_EVENT)
        {
    		glProdCount++;
        }
    else if (type == CY_U3P_DMA_CB_CONS_EVENT)
        {
        	glConsCount++;
        }
}


/* Endpoint specific event callback. For now, we only keep a count of the endpoint events that occur. */
static void
CyFxApplnEpCallback (
        CyU3PUsbEpEvtType evtype,
        CyU3PUSBSpeed_t   usbSpeed,
        uint8_t           epNum)
{
    if (epNum == CY_FX_EP_CONSUMER)
        BulkEpEvtCount++;

    if (evtype == CYU3P_USBEP_SS_RESET_EVT)
    {
        if (epNum == CY_FX_EP_CONSUMER)
        {
            CyU3PUsbStall (CY_FX_EP_CONSUMER, CyTrue, CyFalse);
        }
    }
}

/* This function starts the application. This is called
 * when a SET_CONF event is received from the USB host. The endpoints
 * are configured and the DMA pipe is setup in this function. */
void
CyFxApplnStart (uint16_t samplingFactor)
{
    uint16_t size = 0;
    CyU3PEpConfig_t epCfg;
    CyU3PDmaChannelConfig_t dmaCfg;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
    CyU3PUSBSpeed_t usbSpeed = CyU3PUsbGetSpeed();

    /* Optimize PIB clock for 32 channels */
    CyU3PPibClock_t pibClk = {
        samplingFactor,    /* Clock divider */
        CyFalse,          /* No clock inversion */
        CyFalse,          /* No clock delay */
        CY_U3P_SYS_CLK    /* Use system clock */
    };

    /* Initialize the PIB block with optimized settings */
    apiRetStatus = CyU3PPibInit(CyTrue, &pibClk);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint(4, "PIB Init failed, error code = %d\r\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }


    /* First identify the usb speed. Once that is identified,
     * create a DMA channel and start the transfer on this. */

    /* Based on the Bus Speed configure the endpoint packet size */
    switch (usbSpeed)
    {
    case CY_U3P_FULL_SPEED:
        size = 64;
        break;

    case CY_U3P_HIGH_SPEED:
        size = 512;
        break;

    case  CY_U3P_SUPER_SPEED:
        /* Disable USB link low power entry to optimize USB throughput. */
        CyU3PUsbLPMDisable();
        size = 1024;  /* Maximum packet size for SuperSpeed */
        break;

    default:
        CyU3PDebugPrint (4, "Error! Invalid USB speed.\n");
        CyFxAppErrorHandler (CY_U3P_ERROR_FAILURE);
        break;
    }

    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
    epCfg.enable = CyTrue;
    epCfg.epType = CY_U3P_USB_EP_BULK;
    epCfg.burstLen = (usbSpeed == CY_U3P_SUPER_SPEED) ? 16 : 1;  /* Increased burst length for SuperSpeed */
    epCfg.streams = 0;
    epCfg.pcktSize = size;

    /* Consumer endpoint configuration */
    apiRetStatus = CyU3PSetEpConfig (CY_FX_EP_CONSUMER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /*
       Configure the IN endpoint to allow combining data from multiple buffers into one burst.
       This can help achieve better performance in most cases.
     */
    CyU3PUsbEPSetBurstMode (CY_FX_EP_CONSUMER, CyTrue);

    /* Flush the endpoint memory */
    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);

    epCfg.burstLen = (usbSpeed == CY_U3P_SUPER_SPEED) ? 4 : 1;

    CyU3PUsbRegisterEpEvtCallback (CyFxApplnEpCallback, 0x1B0, 0x0F, 0x0F);

    /* Create a DMA AUTO channel for the GPIF to USB transfer. */
    CyU3PMemSet ((uint8_t *)&dmaCfg, 0, sizeof (dmaCfg));
    dmaCfg.size  = CY_FX_DMA_BUF_SIZE * 2;  /* Doubled buffer size for better throughput */
    dmaCfg.count = CY_FX_DMA_BUF_COUNT * 2;  /* Doubled buffer count for better buffering */
    dmaCfg.prodSckId = CY_FX_GPIF_PRODUCER_SOCKET;
    dmaCfg.consSckId = CY_FX_EP_CONSUMER_SOCKET;
    dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;
    dmaCfg.prodHeader = 0;
    dmaCfg.prodFooter = 0;
    dmaCfg.consHeader = 0;
    dmaCfg.prodAvailCount = 0;
    dmaCfg.notification = CY_U3P_DMA_CB_CONS_SUSP|CY_U3P_DMA_CB_PROD_EVENT | CY_U3P_DMA_CB_CONS_EVENT;
    dmaCfg.cb = GpifToUsbDmaCallback;
    apiRetStatus = CyU3PDmaChannelCreate (&glDmaChHandle, CY_U3P_DMA_TYPE_AUTO_SIGNAL, &dmaCfg);

    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelCreate failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Set DMA Channel transfer size */
    apiRetStatus = CyU3PDmaChannelSetXfer (&glDmaChHandle, CY_FX_GPIFTOUSB_DMA_TX_SIZE);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Load and start the GPIF state machine. */
    apiRetStatus = CyU3PGpifLoad (&CyFxGpifConfig);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PGpifLoad failed, error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    apiRetStatus = CyU3PGpifSMStart (START, ALPHA_START);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PGpifSMStart failed, error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }

    /* Update the flag so that the application thread is notified of this. */
    glIsApplnActive = CyTrue;
}

/* This function stops the application. This shall be called whenever a RESET
 * or DISCONNECT event is received from the USB host. The endpoints are
 * disabled and the DMA pipe is destroyed by this function. */
void
CyFxApplnStop (
        void)
{
    CyU3PEpConfig_t epCfg;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Update the flag so that the application thread is notified of this. */
    glIsApplnActive = CyFalse;

    CyU3PGpifDisable (CyTrue);

    CyU3PPibDeInit();

    /* Destroy the channels */
    CyU3PDmaChannelDestroy (&glDmaChHandle);

    /* Flush the endpoint memory */
    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);

    /* Disable endpoints. */
    CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
    epCfg.enable = CyFalse;

    /* Disable the GPIF->USB endpoint. */
    apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler (apiRetStatus);
    }
}



#define CMD_GET_FW_VERSION		(0xb0)
#define CMD_START				(0xb1)
#define CMD_GET_REVID_VERSION	(0xb2)


uint8_t version[] = {1,0};


/* Callback to handle the USB setup requests. */
CyBool_t
CyFxApplnUSBSetupCB (
        uint32_t setupdat0, /* SETUP Data 0 */
        uint32_t setupdat1  /* SETUP Data 1 */
    )
{
    /* Fast enumeration is used. Only requests addressed to the interface, class,
     * vendor and unknown control requests are received by this function.
     * This application does not support any class or vendor requests. */

    uint8_t  bRequest, bReqType;
    uint8_t  bType, bTarget;
    uint16_t wValue, wIndex, wLength;
    CyBool_t isHandled = CyFalse;

    /* Decode the fields from the setup request. */
    bReqType = (setupdat0 & CY_U3P_USB_REQUEST_TYPE_MASK);
    bType    = (bReqType & CY_U3P_USB_TYPE_MASK);
    bTarget  = (bReqType & CY_U3P_USB_TARGET_MASK);
    bRequest = ((setupdat0 & CY_U3P_USB_REQUEST_MASK) >> CY_U3P_USB_REQUEST_POS);
    wValue   = ((setupdat0 & CY_U3P_USB_VALUE_MASK)   >> CY_U3P_USB_VALUE_POS);
    wIndex   = ((setupdat1 & CY_U3P_USB_INDEX_MASK)   >> CY_U3P_USB_INDEX_POS);
    wLength  = ((setupdat1 & CY_U3P_USB_LENGTH_MASK)  >> CY_U3P_USB_LENGTH_POS);

    if (bType == CY_U3P_USB_VENDOR_RQT)
    {
        /* Vendor command is sent by test applications. Start the loop that tries to keep the link
         * in U0.
         */

    	CyU3PDebugPrint (0, "Vendor Command Receiveds\r\n");

        switch (bRequest)
        {
            case CMD_START:
            	CyU3PDebugPrint(0,"Start Command, wLength = %d\r\n",wLength);
            	CyU3PUsbGetEP0Data(wLength, glEp0Buffer, NULL);
            	CyU3PDebugPrint(0,"glEp0Buffer[0]=%d\r\n",glEp0Buffer[0]);
                if (glIsApplnActive)
                {
                    CyFxApplnStop ();
                }
                /*glEp0Buffer[0] should be the factor by which the 400MHZ clock should be divided*/
                CyFxApplnStart(glEp0Buffer[0]);
            	isHandled = CyTrue;
                break;

            case CMD_GET_FW_VERSION:
            	CyU3PDebugPrint(0,"Version\r\n");
                CyU3PUsbSendEP0Data (2, version);
            	break;

            case CMD_GET_REVID_VERSION:
            	CyU3PDebugPrint(0,"Revision\r\n");
                CyU3PUsbSendEP0Data (1, version);
            	break;

            default:
                break;
        }
    }

    if (bType == CY_U3P_USB_STANDARD_RQT)
    {
        /* Handle SET_FEATURE(FUNCTION_SUSPEND) and CLEAR_FEATURE(FUNCTION_SUSPEND)
         * requests here. It should be allowed to pass if the device is in configured
         * state and failed otherwise. */
        if ((bTarget == CY_U3P_USB_TARGET_INTF) && ((bRequest == CY_U3P_USB_SC_SET_FEATURE)
                    || (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)) && (wValue == 0))
        {
            if (glIsApplnActive)
            {
                CyU3PUsbAckSetup ();

                /* As we have only one interface, the link can be pushed into U2 state as soon as
                   this interface is suspended.
                 */
                if (bRequest == CY_U3P_USB_SC_SET_FEATURE)
                {
                    glForceLinkU2 = CyTrue;
                }
                else
                {
                    glForceLinkU2 = CyFalse;
                }
            }
            else
                CyU3PUsbStall (0, CyTrue, CyFalse);

            isHandled = CyTrue;
        }

        /* CLEAR_FEATURE request for endpoint is always passed to the setup callback
         * regardless of the enumeration model used. When a clear feature is received,
         * the previous transfer has to be flushed and cleaned up. This is done at the
         * protocol level. Since this is just a loopback operation, there is no higher
         * level protocol. So flush the EP memory and reset the DMA channel associated
         * with it. If there are more than one EP associated with the channel reset both
         * the EPs. The endpoint stall and toggle / sequence number is also expected to be
         * reset. Return CyFalse to make the library clear the stall and reset the endpoint
         * toggle. Or invoke the CyU3PUsbStall (ep, CyFalse, CyTrue) and return CyTrue.
         * Here we are clearing the stall. */
        if ((bTarget == CY_U3P_USB_TARGET_ENDPT) && (bRequest == CY_U3P_USB_SC_CLEAR_FEATURE)
                && (wValue == CY_U3P_USBX_FS_EP_HALT))
        {
            if (glIsApplnActive)
            {
                if (wIndex == CY_FX_EP_CONSUMER)
                {
                    CyU3PUsbSetEpNak (CY_FX_EP_CONSUMER, CyTrue);
                    CyU3PBusyWait (125);

                    CyU3PDmaChannelReset (&glDmaChHandle);
                    CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);
                    CyU3PUsbResetEp (CY_FX_EP_CONSUMER);
                    CyU3PDmaChannelSetXfer (&glDmaChHandle, CY_FX_GPIFTOUSB_DMA_TX_SIZE);
                    CyU3PUsbStall (wIndex, CyFalse, CyTrue);

                    CyU3PUsbSetEpNak (CY_FX_EP_CONSUMER, CyFalse);
                    isHandled = CyTrue;
                    CyU3PUsbAckSetup ();
                }

                if(wIndex == 0)
                {
                	CyU3PDebugPrint(0,"Handling Clear Feature on EP0r\n");

                }


            }
        }
    }

    return isHandled;
}

/* This is the callback function to handle the USB events. */
void
CyFxApplnUSBEventCB (
    CyU3PUsbEventType_t evtype, /* Event type */
    uint16_t            evdata  /* Event data */
    )
{
    if ((evtype != CY_U3P_USB_EVENT_EP0_STAT_CPLT) && (evtype != CY_U3P_USB_EVENT_RESUME))
        CyU3PDebugPrint (2, "USB event: %d %d\r\n", evtype, evdata);

    switch (evtype)
    {
        case CY_U3P_USB_EVENT_CONNECT:
            CyU3PDebugPrint (8, "CY_U3P_USB_EVENT_CONNECT detected\r\n");
            break;

        case CY_U3P_USB_EVENT_SETCONF:
            break;

        case CY_U3P_USB_EVENT_RESET:
        case CY_U3P_USB_EVENT_DISCONNECT:
            glForceLinkU2 = CyFalse;

            /* Stop the function. */
            if (glIsApplnActive)
            {
                CyFxApplnStop ();
            }

            if (evtype == CY_U3P_USB_EVENT_DISCONNECT) {
                CyU3PDebugPrint (8, "CY_U3P_USB_EVENT_DISCONNECT detected\r\n");
            }
            break;

        case CY_U3P_USB_EVENT_EP_UNDERRUN:
            underrunCnt++;
            CyU3PDebugPrint (4, "EP Underrun on %d \n\r",evdata);
            CyU3PUsbResetEndpointMemories ();
            break;

        case CY_U3P_USB_EVENT_EP0_STAT_CPLT:
            break;

        default:
            break;
    }
}

/* Callback function to handle LPM requests from the USB 3.0 host. This function is invoked by the API
   whenever a state change from U0 -> U1 or U0 -> U2 happens. If we return CyTrue from this function, the
   FX3 device is retained in the low power state. If we return CyFalse, the FX3 device immediately tries
   to trigger an exit back to U0.

   This application does not have any state in which we should not allow U1/U2 transitions; and therefore
   the function always return CyTrue.
 */
CyBool_t
CyFxApplnLPMRqtCB (
        CyU3PUsbLinkPowerMode link_mode)
{
    return CyTrue;
}

/* This function initializes the USB Module, sets the enumeration descriptors.
 * This function does not start the bulk streaming and this is done only when
 * SET_CONF event is received. */
void
CyFxApplnInit (void)
{
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Start the USB functionality. */
    apiRetStatus = CyU3PUsbStart();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "CyU3PUsbStart failed to Start, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* The fast enumeration is the easiest way to setup a USB connection,
     * where all enumeration phase is handled by the library. Only the
     * class / vendor requests need to be handled by the application. */
    CyU3PUsbRegisterSetupCallback(CyFxApplnUSBSetupCB, CyTrue);

    /* Setup the callback to handle the USB events. */
    CyU3PUsbRegisterEventCallback(CyFxApplnUSBEventCB);

    /* Register a callback to handle LPM requests from the USB 3.0 host. */
    CyU3PUsbRegisterLPMRequestCallback(CyFxApplnLPMRqtCB);

    /* Set the USB Enumeration descriptors */

    /* Super speed device descriptor. */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB30DeviceDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* High speed device descriptor. */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_DEVICE_DESCR, 0, (uint8_t *)CyFxUSB20DeviceDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* BOS descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_BOS_DESCR, 0, (uint8_t *)CyFxUSBBOSDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Device qualifier descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_DEVQUAL_DESCR, 0, (uint8_t *)CyFxUSBDeviceQualDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set device qualifier descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Super speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBSSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* High speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBHSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Set Other Speed Descriptor failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Full speed configuration descriptor */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_FS_CONFIG_DESCR, 0, (uint8_t *)CyFxUSBFSConfigDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Set Configuration Descriptor failed, Error Code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 0 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 1 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* String descriptor 2 */
    apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }

    /* Register a buffer into which the USB driver can log relevant events. */
    gl_UsbLogBuffer = (uint8_t *)CyU3PDmaBufferAlloc (CYFX_USBLOG_SIZE);
    if (gl_UsbLogBuffer)
        CyU3PUsbInitEventLog (gl_UsbLogBuffer, CYFX_USBLOG_SIZE);

    CyU3PDebugPrint (4, "About to connect to USB host\r\n");

    /* Connect the USB Pins with super speed operation enabled. */
    apiRetStatus = CyU3PConnectState (CyTrue, CyTrue);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        CyU3PDebugPrint (4, "USB Connect failed, Error code = %d\n", apiRetStatus);
        CyFxAppErrorHandler(apiRetStatus);
    }
    CyU3PDebugPrint (8, "CyFxApplnInit complete\r\n");
}


void printGpifToUSBDMAStats()
{
	CyU3PDebugPrint(0,"Prod:%d , Cons:%d\r\n",glProdCount,glConsCount);
	glProdCount = 0;
	glConsCount = 0;
}

/* Entry function for the glAppThread. */
void
CyFxAppThread_Entry (
        uint32_t input)
{
    /* Initialize the debug module */
    CyFxApplnDebugInit();

    printGpifToUSBDMAStats();
    /* Initialize the application */
    CyFxApplnInit();

    while (!glIsApplnActive)
        CyU3PThreadSleep (100);

    for (;;)
    {
        if(glIsApplnActive && (glProdCount + glConsCount > 0))
        {
        	printGpifToUSBDMAStats();
        }
        CyU3PThreadSleep (10);
    }
}

/* Application define function which creates the threads. */
void
CyFxApplicationDefine (
        void)
{
    void *ptr = NULL;
    uint32_t ret = CY_U3P_SUCCESS;

    /* Allocate the memory for the threads */
    ptr = CyU3PMemAlloc (CY_FX_GPIFTOUSB_THREAD_STACK);

    /* Create the thread for the application */
    ret = CyU3PThreadCreate (&glAppThread,                      /* App thread structure */
                          "21:Gpit_to_USB",                     /* Thread ID and thread name */
                          CyFxAppThread_Entry,                  /* App thread entry function */
                          0,                                    /* No input parameter to thread */
                          ptr,                                  /* Pointer to the allocated thread stack */
                          CY_FX_GPIFTOUSB_THREAD_STACK,         /* App thread stack size */
                          CY_FX_GPIFTOUSB_THREAD_PRIORITY,      /* App thread priority */
                          CY_FX_GPIFTOUSB_THREAD_PRIORITY,      /* App thread priority */
                          CYU3P_NO_TIME_SLICE,                  /* No time slice for the application thread */
                          CYU3P_AUTO_START                      /* Start the thread immediately */
                          );

    /* Check the return code */
    if (ret != 0)
    {
        /* Thread Creation failed with the error code retThrdCreate */

        /* Add custom recovery or debug actions here */

        /* Application cannot continue */
        /* Loop indefinitely */
        while(1);
    }
}

/*
 * Main function
 */
int
main (void)
{
    CyU3PIoMatrixConfig_t io_cfg;
    CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

    /* Initialize the device */
    CyU3PSysClockConfig_t clockConfig;

    clockConfig.setSysClk400  = CyTrue;
    clockConfig.cpuClkDiv     = 2;
    clockConfig.dmaClkDiv     = 2;
    clockConfig.mmioClkDiv    = 2;
    clockConfig.useStandbyClk = CyFalse;
    clockConfig.clkSrc        = CY_U3P_SYS_CLK;
    status = CyU3PDeviceInit (&clockConfig);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* Initialize the caches. The D-Cache is not enabled because it will cause a significant slowing down for
     * an application which does not touch the data in the DMA buffers.
     */
    status = CyU3PDeviceCacheControl (CyTrue, CyFalse, CyFalse);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }
    /* Configure the IO matrix for the device. On the FX3 DVK board, the COM port
     * is connected to the IO(53:56). This means that either DQ32 mode should be
     * selected or lppMode should be set to UART_ONLY. Here we are choosing
     * UART_ONLY configuration for 16 bit slave FIFO configuration and setting
     * isDQ32Bit for 32-bit slave FIFO configuration. */


    CyU3PMemSet ((uint8_t *)&io_cfg, 0, sizeof (io_cfg));
    io_cfg.isDQ32Bit = CyTrue; /*CyFalse*/
    io_cfg.useUart   = CyTrue;
    io_cfg.useI2C    = CyFalse;
    io_cfg.useI2S    = CyFalse;
    io_cfg.useSpi    = CyFalse;
    io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_DEFAULT;  /*use CY_U3P_IO_MATRIX_LPP_UART_ONLY for 16*/
    io_cfg.s0Mode    = CY_U3P_SPORT_INACTIVE;
    io_cfg.s1Mode    = CY_U3P_SPORT_INACTIVE;

    /* No GPIOs are enabled. */
    io_cfg.gpioSimpleEn[0]  = 0;
    io_cfg.gpioSimpleEn[1]  = 0;
    io_cfg.gpioComplexEn[0] = 0;
    io_cfg.gpioComplexEn[1] = 0;
    status = CyU3PDeviceConfigureIOMatrix (&io_cfg);
    if (status != CY_U3P_SUCCESS)
    {
        goto handle_fatal_error;
    }

    /* This is a non returnable call for initializing the RTOS kernel */
    CyU3PKernelEntry ();

    /* Dummy return to make the compiler happy */
    return 0;

handle_fatal_error:

    /* Cannot recover from this error. */
    while (1);
}

/* [ ] */

