#ifndef TEST_STUB_USBH_HID_H
#define TEST_STUB_USBH_HID_H

/* Only the transport-independent fields used by usbh_hid_gamepad.c. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define USBH_memset memset

typedef enum {
    USBH_OK = 0,
    USBH_BUSY,
    USBH_FAIL,
    USBH_NOT_SUPPORTED
} USBH_StatusTypeDef;

typedef enum {
    HOST_IDLE = 0,
    HOST_CLASS_REQUEST,
    HOST_CLASS
} HOST_StateTypeDef;

typedef struct {
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
} USBH_DeviceDescTypeDef;

typedef struct {
    USBH_DeviceDescTypeDef DevDesc;
} USBH_DeviceTypeDef;

typedef struct {
    void *pData;
} USBH_ClassTypeDef;

typedef struct {
    USBH_DeviceTypeDef device;
    USBH_ClassTypeDef *pActiveClass;
    HOST_StateTypeDef gState;
} USBH_HandleTypeDef;

typedef struct {
    uint16_t length;
    uint8_t *pData;
} HID_HandleTypeDef;

#endif /* TEST_STUB_USBH_HID_H */
