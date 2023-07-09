
#include <mach/mach.h>
#include <io/iousb.h>
#include <common/log.h>
#include <common/common.h>

unsigned char blank[DFU_MAX_TRANSFER_SZ];

static const char *deviceClass = kIOUSBDeviceClassName;
static char serialstr[256];

#if defined(RA1NPOC_MODE)
RA1NPOC_STATIC_API static int nSleep(long nanoseconds)
{
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = nanoseconds;
    return nanosleep(&req, &rem);
}

RA1NPOC_STATIC_API static void IOUSBAsyncCallBack(void *refcon, IOReturn ret, void *arg0)
{
    async_transfer_t* transfer = refcon;
    
    if(transfer != NULL)
    {
        transfer->ret = ret;
        memcpy(&transfer->wLenDone, &arg0, sizeof(transfer->wLenDone));
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
}
#endif

RA1NPOC_STATIC_API static void CFDictionarySet16(CFMutableDictionaryRef dict, const void *key, SInt16 value)
{
    CFNumberRef numberRef;
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &value);
    if(numberRef)
    {
        CFDictionarySetValue(dict, key, numberRef);
        CFRelease(numberRef);
    }
}

RA1NPOC_STATIC_API static io_iterator_t IOUSBGetIteratorForPid(uint16_t pid)
{
    IOReturn result;
    io_iterator_t iterator;
    CFMutableDictionaryRef dict;
    
#ifdef IPHONEOS_ARM
    // Allows iOS to connect to iOS devices. // iOS 9.0 or high
    deviceClass = "IOUSBHostDevice";
#endif
    
    dict = IOServiceMatching(deviceClass);
    CFDictionarySet16(dict, CFSTR(kUSBVendorID),  kAppleVendorID);
    CFDictionarySet16(dict, CFSTR(kUSBProductID), pid);
    
    result = IOServiceGetMatchingServices(kIOMasterPortDefault, dict, &iterator);
    if(result != kIOReturnSuccess)
    {
        return IO_OBJECT_NULL;
    }
    
    return iterator;
}

RA1NPOC_STATIC_API static void IOUSBGetInfo(client_t *client, const char *str)
{
    char* strptr = NULL;
    strptr = strstr(str, "CPID:");
    if (strptr != NULL)
    {
        sscanf(strptr, "CPID:%x", (unsigned int *)&client->cpid);
    }
    strptr = strstr(str, "CPRV:");
    if (strptr != NULL)
    {
        sscanf(strptr, "CPRV:%x", (unsigned int *)&client->cprv);
    }
    strptr = strstr(str, "SRTG:");
    if(strptr != NULL)
    {
        client->devmode = kDeviceDFUMode;
    }
    strptr = strstr(str, "YOLO:checkra1n");
    if(strptr != NULL)
    {
        client->devmode |= kDeviceYoloDFUMode;
    }
    strptr = strstr(str, "PWND:");
    if(strptr != NULL)
    {
        client->devmode |= kDevicePwnedDFUMode;
    }
    client->sn = true;
}

RA1NPOC_STATIC_API static void IOUSBReleaseClient(client_t *client)
{
    client->dev = NULL;
    client->handle = NULL;
    client->async_event_source = NULL;
    client->cpid = 0;
    client->cprv = 0;
    client->sn = false;
    client->devmode = kDeviceNotFoundMode;
}

RA1NPOC_STATIC_API static IOReturn IOUSBReEnumerate(client_t *client)
{
    if(!client || !client->dev)
    {
        return kIOReturnError;
    }
    return (*client->dev)->USBDeviceReEnumerate(client->dev, 0);
}

RA1NPOC_STATIC_API static IOReturn IOUSBResetDevice(client_t *client)
{
    if(!client || !client->dev)
    {
        return kIOReturnError;
    }
    return (*client->dev)->ResetDevice(client->dev);
}

RA1NPOC_STATIC_API static void IOUSBReset(client_t *client, int reset)
{
    if(reset & kDeviceUSBResetDevice)
    {
        IOUSBResetDevice(client);
    }
    if(reset & kDeviceUSBReEnumerate)
    {
        IOUSBReEnumerate(client);
    }
}

RA1NPOC_API void IOUSBClose(client_t *client)
{
    if(client)
    {
        if (client->dev)
        {
            (*client->dev)->USBDeviceClose(client->dev);
            (*client->dev)->Release(client->dev);
            client->dev = NULL;
        }
        if (client->handle)
        {
            (*client->handle)->USBInterfaceClose(client->handle);
            (*client->handle)->Release(client->handle);
            client->handle = NULL;
        }
        if(client->async_event_source)
        {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), client->async_event_source, kCFRunLoopDefaultMode);
            CFRelease(client->async_event_source);
        }
        IOUSBReleaseClient(client);
    }
}

RA1NPOC_STATIC_API static int IOUSBOpen(client_t *client, uint16_t pid)
{
    io_iterator_t iterator;
    
    if(!client)
    {
        ERR("No client");
        return -1;
    }
    
    IOUSBClose(client);
    
    iterator = IOUSBGetIteratorForPid(pid);
    
    if (iterator == IO_OBJECT_NULL)
    {
        // not found
        // DEVLOG("Failed IOServiceGetMatchingServices");
        return -1;
    }
    
    io_service_t usbDev = MACH_PORT_NULL;
    while((usbDev = IOIteratorNext(iterator)))
    {
        uint64_t regID;
        kern_return_t ret = IORegistryEntryGetRegistryEntryID(usbDev, &regID);
        if(ret != KERN_SUCCESS)
        {
            ERR("IORegistryEntryGetRegistryEntryID: %s", mach_error_string(ret));
            goto next;
        }
        
        SInt32 score = 0;
        IOCFPlugInInterface **plugin = NULL;
        ret = IOCreatePlugInInterfaceForService(usbDev, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
        if(ret != KERN_SUCCESS)
        {
            ERR("IOCreatePlugInInterfaceForService(usbDev): %s", mach_error_string(ret));
            goto next;
        }
        
        CFStringRef cfstr = IORegistryEntryCreateCFProperty(usbDev, CFSTR(kUSBSerialNumberString), kCFAllocatorDefault, kNilOptions);
        if(cfstr == NULL)
        {
            ERR("Failed IORegistryEntryCreateCFProperty");
        }
        else
        {
            memset(&serialstr, '\0', 256);
            CFStringGetCString(cfstr, serialstr, sizeof(serialstr), kCFStringEncodingUTF8);
            CFRelease(cfstr);
            IOUSBGetInfo(client, serialstr);
            memset(&serialstr, '\0', 256);
        }
        
        HRESULT result = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID*)&client->dev);
        (*plugin)->Release(plugin);
        if(result != 0)
        {
            ERR("QueryInterface(dev): 0x%x", result);
            goto next;
        }
        ret = (*client->dev)->USBDeviceOpenSeize(client->dev);
        if(ret != KERN_SUCCESS)
        {
            ERR("USBDeviceOpenSeize: %s", mach_error_string(ret));
        }
        else
        {
            ret = (*client->dev)->SetConfiguration(client->dev, 1);
            if(ret != KERN_SUCCESS)
            {
                ERR("SetConfiguration: %s", mach_error_string(ret));
            }
            else
            {
                ret = (*client->dev)->CreateDeviceAsyncEventSource(client->dev, &client->async_event_source);
                if (ret == kIOReturnSuccess)
                {
                    CFRunLoopAddSource(CFRunLoopGetCurrent(), client->async_event_source, kCFRunLoopDefaultMode);
                }
                
                IOUSBFindInterfaceRequest request =
                {
                    .bInterfaceClass    = kIOUSBFindInterfaceDontCare,
                    .bInterfaceSubClass = kIOUSBFindInterfaceDontCare,
                    .bInterfaceProtocol = kIOUSBFindInterfaceDontCare,
                    .bAlternateSetting  = kIOUSBFindInterfaceDontCare,
                };
                io_iterator_t iter = MACH_PORT_NULL;
                ret = (*client->dev)->CreateInterfaceIterator(client->dev, &request, &iter);
                if(ret != KERN_SUCCESS)
                {
                    ERR("CreateInterfaceIterator: %s", mach_error_string(ret));
                }
                else
                {
                    io_service_t usbIntf = MACH_PORT_NULL;
                    while((usbIntf = IOIteratorNext(iter)))
                    {
                        ret = IOCreatePlugInInterfaceForService(usbIntf, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
                        IOObjectRelease(usbIntf);
                        if(ret != KERN_SUCCESS)
                        {
                            ERR("IOCreatePlugInInterfaceForService(usbIntf): %s", mach_error_string(ret));
                            continue;
                        }
                        result = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (LPVOID*)&client->handle);
                        (*plugin)->Release(plugin);
                        if(result != 0)
                        {
                            ERR("QueryInterface(intf): 0x%x", result);
                            continue;
                        }
                        
                        ret = (*client->handle)->USBInterfaceOpen(client->handle);
                        if(ret != KERN_SUCCESS)
                        {
                            ERR("USBInterfaceOpen: %s", mach_error_string(ret));
                        }
                        else
                        {
                            while((usbIntf = IOIteratorNext(iter))) IOObjectRelease(usbIntf);
                            IOObjectRelease(iter);
                            while((usbDev = IOIteratorNext(iterator))) IOObjectRelease(usbDev);
                            IOObjectRelease(usbDev);
                            return 0;
                        }
                        (*client->handle)->Release(client->handle);
                        client->handle = NULL;
                    }
                    IOObjectRelease(iter);
                }
                if(client->async_event_source) {
                    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), client->async_event_source, kCFRunLoopDefaultMode);
                    CFRelease(client->async_event_source);
                }
            }
        }
        
    next:;
        if(client->dev)
        {
            (*client->dev)->USBDeviceClose(client->dev);
            (*client->dev)->Release(client->dev);
            client->dev = NULL;
        }
        IOObjectRelease(usbDev);
    }
    
    return -1;
}

RA1NPOC_API int IOUSBConnect(client_t *client, uint16_t pid, int retry, int reset, unsigned long sec)
{
    if(!client)
    {
        ERR("No client");
        return -1;
    }
    
    if(client)
    {
        IOUSBReset(client, reset);
        IOUSBClose(client);
    }
    
    usleep(sec);
    
    for(int i=0; i<retry; i++)
    {
        if(!IOUSBOpen(client, pid))
            return 0;
        IOUSBClose(client);
        sleep(1);
    }
    return -1;
}

RA1NPOC_API IOReturn IOUSBAbortPipeZero(client_t *client)
{
    if(!client->dev) return kIOReturnError;
    return (*client->dev)->USBDeviceAbortPipeZero(client->dev);
}

RA1NPOC_API transfer_t IOUSBControlTransfer(client_t *client,
                                            uint8_t bm_request_type,
                                            uint8_t b_request,
                                            uint16_t w_value,
                                            uint16_t w_index,
                                            unsigned char *data,
                                            uint16_t w_length)
{
    transfer_t result;
    IOUSBDevRequest req;
    
    memset(&result, '\0', sizeof(transfer_t));
    memset(&req, '\0', sizeof(req));
    
    req.bmRequestType     = bm_request_type;
    req.bRequest          = b_request;
    req.wValue            = OSSwapLittleToHostInt16(w_value);
    req.wIndex            = OSSwapLittleToHostInt16(w_index);
    req.wLength           = OSSwapLittleToHostInt16(w_length);
    req.pData             = data;
    
    result.ret = (*client->dev)->DeviceRequest(client->dev, &req);
    result.wLenDone = req.wLenDone;
    
    return result;
}

RA1NPOC_API transfer_t IOUSBControlTransferTO(client_t *client,
                                              uint8_t bm_request_type,
                                              uint8_t b_request,
                                              uint16_t w_value,
                                              uint16_t w_index,
                                              unsigned char *data,
                                              uint16_t w_length,
                                              unsigned int time)
{
    transfer_t result;
    IOUSBDevRequestTO req;
    
    memset(&result, '\0', sizeof(transfer_t));
    memset(&req, '\0', sizeof(req));
    
    req.bmRequestType     = bm_request_type;
    req.bRequest          = b_request;
    req.wValue            = OSSwapLittleToHostInt16(w_value);
    req.wIndex            = OSSwapLittleToHostInt16(w_index);
    req.wLength           = OSSwapLittleToHostInt16(w_length);
    req.pData             = data;
    req.noDataTimeout     = time;
    req.completionTimeout = time;
    
    result.ret = (*client->dev)->DeviceRequestTO(client->dev, &req);
    result.wLenDone = req.wLenDone;
    
    return result;
}

#if defined(RA1NPOC_MODE)
RA1NPOC_API transfer_t IOUSBAsyncControlTransfer(client_t *client,
                                                 uint8_t bm_request_type,
                                                 uint8_t b_request,
                                                 uint16_t w_value,
                                                 uint16_t w_index,
                                                 unsigned char *data,
                                                 uint16_t w_length,
                                                 async_transfer_t* transfer,
                                                 unsigned int timeout)
{
    transfer_t result;
    //IOUSBDevRequest req;
    IOUSBDevRequestTO req;
    
    memset(&result, '\0', sizeof(transfer_t));
    memset(&req, '\0', sizeof(req));
    
    req.bmRequestType     = bm_request_type;
    req.bRequest          = b_request;
    req.wValue            = OSSwapLittleToHostInt16(w_value);
    req.wIndex            = OSSwapLittleToHostInt16(w_index);
    req.wLength           = OSSwapLittleToHostInt16(w_length);
    req.pData             = data;
    req.completionTimeout = timeout;
    
    result.ret = (*client->dev)->DeviceRequestAsyncTO(client->dev, &req, IOUSBAsyncCallBack, transfer);
    result.wLenDone = req.wLenDone;
    
    return result;
}

RA1NPOC_API transfer_t IOUSBAsyncControlTransferNoTO(client_t *client,
                                                     uint8_t bm_request_type,
                                                     uint8_t b_request,
                                                     uint16_t w_value,
                                                     uint16_t w_index,
                                                     unsigned char *data,
                                                     uint16_t w_length,
                                                     async_transfer_t* transfer)
{
    transfer_t result;
    IOUSBDevRequest req;
    
    memset(&result, '\0', sizeof(transfer_t));
    memset(&req, '\0', sizeof(req));
    
    req.bmRequestType     = bm_request_type;
    req.bRequest          = b_request;
    req.wValue            = OSSwapLittleToHostInt16(w_value);
    req.wIndex            = OSSwapLittleToHostInt16(w_index);
    req.wLength           = OSSwapLittleToHostInt16(w_length);
    req.pData             = data;
    
    result.ret = (*client->dev)->DeviceRequestAsync(client->dev, &req, IOUSBAsyncCallBack, transfer);
    result.wLenDone = req.wLenDone;
    
    return result;
}

RA1NPOC_API UInt32 IOUSBAsyncControlTransferWithCancel(client_t *client,
                                                       uint8_t bm_request_type,
                                                       uint8_t b_request,
                                                       uint16_t w_value,
                                                       uint16_t w_index,
                                                       unsigned char *data,
                                                       uint16_t w_length,
                                                       unsigned int timeout,
                                                       unsigned int ns_time)
{
    transfer_t result;
    IOReturn error;
    async_transfer_t transfer;
    
    memset(&transfer, '\0', sizeof(async_transfer_t));
    
    result = IOUSBAsyncControlTransfer(client, bm_request_type, b_request, w_value, w_index, data, w_length, &transfer, timeout);
    if(result.ret != kIOReturnSuccess)
    {
        return result.ret;
    }
    nSleep(ns_time);
    
    error = IOUSBAbortPipeZero(client);
    if(error != kIOReturnSuccess)
    {
        return -1;
    }
    
    while(transfer.ret != kIOReturnAborted)
    {
        CFRunLoopRun();
    }
    
    return transfer.wLenDone;
}
#endif

RA1NPOC_API transfer_t IOUSBBulkUpload(client_t *client, void *data, uint32_t len)
{
    transfer_t result;
    result.ret = (*client->handle)->WritePipe(client->handle, 2, data, len);
    result.wLenDone = 0;
    
    return result;
}

RA1NPOC_API transfer_t IOUSBControlRequestTransfer(client_t *client,
                                                   uint8_t bm_request_type,
                                                   uint8_t b_request,
                                                   uint16_t w_value,
                                                   uint16_t w_index,
                                                   unsigned char *data,
                                                   uint16_t w_length)
{
    transfer_t result;
    IOUSBDevRequest req;
    
    memset(&result, '\0', sizeof(transfer_t));
    memset(&req, '\0', sizeof(req));
    
    req.bmRequestType     = bm_request_type;
    req.bRequest          = b_request;
    req.wValue            = OSSwapLittleToHostInt16(w_value);
    req.wIndex            = OSSwapLittleToHostInt16(w_index);
    req.wLength           = OSSwapLittleToHostInt16(w_length);
    req.pData             = data;
    
    result.ret = (*client->handle)->ControlRequest(client->handle, 0, &req);
    result.wLenDone = req.wLenDone;
    
    return result;
}


RA1NPOC_API void IOUSBSendReboot(client_t *client)
{
    IOUSBControlTransfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char *)"setenv auto-boot true\x00", sizeof("setenv auto-boot true\x00"));
    IOUSBControlTransfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char *)"saveenv\x00", sizeof("saveenv\x00"));
    IOUSBControlTransfer(client, 0x40, 0, 0x0000, 0x0000, (unsigned char *)"reboot\x00", sizeof("reboot\x00"));
}
