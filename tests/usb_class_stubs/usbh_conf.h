#ifndef TEST_USBH_CONF_H
#define TEST_USBH_CONF_H

/* The actual ST USB/HID headers are used. Only target HAL configuration is
 * replaced; limits match Firmware/USB_HOST/Target/usbh_conf.h. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __IO volatile
#define USBH_MAX_NUM_ENDPOINTS 2U
#define USBH_MAX_NUM_INTERFACES 2U
#define USBH_MAX_NUM_CONFIGURATION 2U
#define USBH_KEEP_CFG_DESCRIPTOR 1U
#define USBH_MAX_NUM_SUPPORTED_CLASS 1U
#define USBH_MAX_SIZE_CONFIGURATION 512U
#define USBH_MAX_DATA_BUFFER 512U
#define USBH_DEBUG_LEVEL 0U
#define USBH_USE_OS 0U

#define USBH_malloc malloc
#define USBH_free free
#define USBH_memset memset
#define USBH_memcpy memcpy
#define USBH_UsrLog(...) do {} while (0)
#define USBH_ErrLog(...) do {} while (0)
#define USBH_DbgLog(...) do {} while (0)

#endif
