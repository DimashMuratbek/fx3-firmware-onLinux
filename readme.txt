
                        CYPRESS SEMICONDUCTOR CORPORATION
                                    FX3 SDK

GPIF to USB Transfer Example (Modified for sigrok-firmware/cypress-fx3)
----------------------------

  This example illustrates the use of a DMA channel to continuously
  transfer data from the GPIF port to the USB host.

  A stub GPIF state machine which does not require any external devices
  is used to continuously commit data into the DMA buffers. This state
  machine continues to push data into the DMA channel whenever the thread
  is in the ready state.

  The device enumerates as a vendor specific USB device with one Bulk-IN
  endpoint. The data committed by the GPIF state machine is continuously
  streamed to this endpoint without any firmware intervention.

  This application also demonstrates the use of the endpoint specific
  CYU3P_USBEP_SS_RESET_EVT event to detect and recover from potential USB
  data corruption due to protocol level errors.

  Files:

    * cyfx_gcc_startup.S    : Start-up code for the ARM-9 core on the FX3 device.
      This assembly source file follows the syntax for the GNU assembler.

    * cyfxgpiftousb.h       : Default constant definitions for this firmware
      examples including endpoint number, burst size, number and size of the
      buffers.

    * cyfxgpiftousb.c       : Source file implementing the GPIF to USB transfer
      example.
      
    * cyfxbulkdscr.c        : C source file containing the USB descriptors that
      are used by this firmware example. VID and PID is defined in this file.

    * cyfxgpif2config.h     : GPIF II Designer generated header file that implements
      the state machine used by this example.

    * cyfxtx.c              : ThreadX RTOS wrappers and utility functions required
      by the FX3 API library.

    * makefile              : GNU make compliant build script for compiling this
      example.

    * continuous_read.cydsn : Folder containing the GPIF II Designer project used
      in this example.Bus width is reduced to 16-bit. The SDK example uses a 32-bit data bus.

