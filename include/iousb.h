#ifndef IOUSB_H
#define IOUSB_H

#include <stdint.h>
#include <stdbool.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>

#ifndef kUSBHostReturnPipeStalled
#define kUSBHostReturnPipeStalled (IOReturn)0xe0005000
#endif

#ifdef IPHONEOS_ARM
extern
const mach_port_t kIOMasterPortDefault
__API_AVAILABLE(macCatalyst(1.0))
__API_DEPRECATED_WITH_REPLACEMENT("kIOMainPortDefault", macos(10.0, 12.0), ios(9.0, 17.0));
#endif

#define kDeviceDFUModeID        (0x1227)
#define kDeviceStage2ModeID     (0x1338)
#define kDevicePongoModeID      (0x4141)
#define kDeviceRecovery1ModeID  (0x1280)
#define kDeviceRecovery2ModeID  (0x1281)
#define kDeviceRecovery3ModeID  (0x1282)
#define kDeviceRecovery4ModeID  (0x1283)

#define kDeviceNotFoundMode     (0)
#define kDeviceDFUMode          (1 << 0)
#define kDeviceStage2Mode       (1 << 1)
#define kDevicePongoMode        (1 << 2)
#define kDeviceRecoveryMode     (1 << 3)

#define kDeviceYoloDFUMode      (1 << 8)
#define kDevicePwnedDFUMode     (1 << 9)


#define kDeviceUSBResetDevice   (1 << 0)
#define kDeviceUSBReEnumerate   (1 << 1)
#define kDeviceUSBPhysRePlug    (1 << 2)

#define DFU_DNLOAD              (1)
#define DFU_GET_STATUS          (3)
#define DFU_CLR_STATUS          (4)
#define DFU_MAX_TRANSFER_SZ     (0x800)
#define EP0_MAX_PACKET_SZ       (0x40)

extern unsigned char blank[DFU_MAX_TRANSFER_SZ];

typedef struct client_p client_t;

struct client_p
{
    IOUSBDeviceInterface245 **dev;
    IOUSBInterfaceInterface245 **handle;
    CFRunLoopSourceRef async_event_source;
    unsigned int cpid;
    unsigned int cprv;
    bool sn;
    uint64_t devmode;
};

typedef struct
{
    UInt32 wLenDone;
    IOReturn ret;
} transfer_t;

typedef transfer_t async_transfer_t;

typedef struct
{
    uint32_t endpoint, pad_0;
    uint64_t io_buffer;
    uint32_t status, io_len, ret_cnt, pad_1;
    uint64_t callback, next;
    uint64_t pad_2, pad_3;
} dfu_callback_t;

// ra1npoc
typedef struct
{
    // stage1
    unsigned char*  stage1;
    size_t          stage1_len;
    // pongoOS
    unsigned char*  pongoOS;
    size_t          pongoOS_len;
    // stage2
    unsigned char*  stage2;
    size_t          stage2_len;
    // overwrite
    uint64_t callback;
    uint64_t next;
} checkra1n_payload_t;

enum AUTOBOOT_STAGE
{
    NONE,
    SETUP_STAGE_FUSE,
    SETUP_STAGE_SEP,
    SEND_STAGE_KPF,
    SETUP_STAGE_KPF,
    SEND_STAGE_RAMDISK,
    SETUP_STAGE_RAMDISK,
    SEND_STAGE_OVERLAY,
    SETUP_STAGE_OVERLAY,
    SETUP_STAGE_KPF_FLAGS,
    SETUP_STAGE_CHECKRAIN_FLAGS,
    SETUP_STAGE_XARGS,
    SETUP_STAGE_ROOTDEV,
    BOOTUP_STAGE,
    USB_TRANSFER_ERROR,
};

void IOUSBClose(client_t *client);
int IOUSBConnect(client_t *client, uint16_t pid, int retry, int reset, unsigned long sec);
void IOUSBSendReboot(client_t *client);

IOReturn IOUSBAbortPipeZero(client_t *client);
transfer_t IOUSBControlTransfer(client_t *client,
                                uint8_t bm_request_type,
                                uint8_t b_request,
                                uint16_t w_value,
                                uint16_t w_index,
                                unsigned char *data,
                                uint16_t w_length);
transfer_t IOUSBControlTransferTO(client_t *client,
                                  uint8_t bm_request_type,
                                  uint8_t b_request,
                                  uint16_t w_value,
                                  uint16_t w_index,
                                  unsigned char *data,
                                  uint16_t w_length,
                                  unsigned int time);
transfer_t IOUSBAsyncControlTransfer(client_t *client,
                                     uint8_t bm_request_type,
                                     uint8_t b_request,
                                     uint16_t w_value,
                                     uint16_t w_index,
                                     unsigned char *data,
                                     uint16_t w_length,
                                     async_transfer_t* transfer,
                                     unsigned int timeout);
transfer_t IOUSBAsyncControlTransferNoTO(client_t *client,
                                         uint8_t bm_request_type,
                                         uint8_t b_request,
                                         uint16_t w_value,
                                         uint16_t w_index,
                                         unsigned char *data,
                                         uint16_t w_length,
                                         async_transfer_t* transfer);
UInt32 IOUSBAsyncControlTransferWithCancel(client_t *client,
                                           uint8_t bm_request_type,
                                           uint8_t b_request,
                                           uint16_t w_value,
                                           uint16_t w_index,
                                           unsigned char *data,
                                           uint16_t w_length,
                                           unsigned int timeout,
                                           unsigned int ns_time);
transfer_t IOUSBControlRequestTransfer(client_t *client,
                                       uint8_t bm_request_type,
                                       uint8_t b_request,
                                       uint16_t w_value,
                                       uint16_t w_index,
                                       unsigned char *data,
                                       uint16_t w_length);

transfer_t IOUSBBulkUpload(client_t *client, void *data, uint32_t len);

#endif
