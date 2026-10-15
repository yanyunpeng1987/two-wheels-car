#include "usbh_hid.h"
#include "usbh_hid_parser.h"
#include "usbh_hid_gamepad.h"
#include "usbh_hiwonder_hid.h"

/* hid[0] is the first member: existing ST keyboard/mouse APIs keep their cast.
   Each gamepad endpoint owns its handle, pipe, receive buffer, URB and toggle. */
typedef struct {
    HID_HandleTypeDef hid[2];
    uint8_t interfaces[2];
    uint8_t count;
    uint8_t ctl_channel;
    uint8_t stall_channel;
    uint8_t allocated[2];
    uint8_t opened[2];
    /* This composite receiver needs IF2 serviced for gamepad reports to flow.
       Its auxiliary input is never decoded or published as vehicle controls. */
    HID_HandleTypeDef aux;
    uint32_t aux_buffer[GAMEPAD_USB_RX_BYTES / sizeof(uint32_t)];
    uint8_t aux_active;
    uint8_t aux_allocated;
    uint8_t aux_opened;
} HIWONDER_HID_Handle;

volatile uint8_t gamepad_usb_aux_active;
volatile uint32_t gamepad_usb_aux_reports;
volatile uint32_t gamepad_usb_aux_submits;
volatile uint32_t gamepad_usb_aux_notready;
volatile uint32_t gamepad_usb_aux_stalls;
volatile uint32_t gamepad_usb_aux_errors;
volatile uint32_t gamepad_usb_aux_last_length;

static USBH_StatusTypeDef USBH_HID_InterfaceInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_InterfaceDeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_ClassRequest(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_SOFProcess(USBH_HandleTypeDef *phost);

USBH_ClassTypeDef HIWONDER_HID_CLASS = {
    "HID", USB_HID_CLASS, USBH_HID_InterfaceInit, USBH_HID_InterfaceDeInit,
    USBH_HID_ClassRequest, USBH_HID_Process, USBH_HID_SOFProcess, NULL
};

static uint8_t ParseInterfaceHID(HID_DescTypeDef *desc, USBH_HandleTypeDef *host,
                                  uint8_t number)
{
    const uint8_t *raw = host->device.CfgDesc_Raw;
    uint16_t size = LE16(raw + 2U);
    uint16_t offset = USB_LEN_CFG_DESC;
    uint8_t match = 0U;
    if ((size < USB_LEN_CFG_DESC) || (size > sizeof(host->device.CfgDesc_Raw))) {
        return 0U;
    }
    while ((uint16_t)(offset + 2U) <= size) {
        uint8_t length = raw[offset];
        uint8_t type = raw[offset + 1U];
        if ((length < 2U) || ((uint32_t)offset + length > size)) {
            return 0U;
        }
        if (type == USB_DESC_TYPE_INTERFACE) {
            if (length < USB_LEN_IF_DESC) {
                return 0U;
            }
            match = (raw[offset + 2U] == number) && (raw[offset + 3U] == 0U);
        } else if ((type == USB_DESC_TYPE_HID) && match) {
            if ((length < 9U) || (raw[offset + 5U] != 1U) ||
                (raw[offset + 6U] != 0x22U)) {
                return 0U;
            }
            desc->bLength = length;
            desc->bDescriptorType = type;
            desc->bcdHID = LE16(raw + offset + 2U);
            desc->bCountryCode = raw[offset + 4U];
            desc->bNumDescriptors = raw[offset + 5U];
            desc->bReportDescriptorType = raw[offset + 6U];
            desc->wItemLength = LE16(raw + offset + 7U);
            return desc->wItemLength != 0U;
        }
        offset = (uint16_t)(offset + length);
    }
    return 0U;
}

static USBH_EpDescTypeDef *FindInputEndpoint(USBH_InterfaceDescTypeDef *itf,
                                           uint8_t dedicated)
{
    uint8_t index;
    uint8_t max_ep;
    if ((itf->bInterfaceClass != USB_HID_CLASS) || (itf->bInterfaceSubClass != 0U) ||
        (dedicated && (itf->bInterfaceProtocol != 0U)) ||
        ((itf->bInterfaceProtocol != 0U) &&
         (itf->bInterfaceProtocol != HID_KEYBRD_BOOT_CODE) &&
         (itf->bInterfaceProtocol != HID_MOUSE_BOOT_CODE))) {
        return NULL;
    }
    max_ep = (itf->bNumEndpoints <= USBH_MAX_NUM_ENDPOINTS) ?
             itf->bNumEndpoints : USBH_MAX_NUM_ENDPOINTS;
    for (index = 0U; index < max_ep; ++index) {
        USBH_EpDescTypeDef *ep = &itf->Ep_Desc[index];
        if ((ep->bEndpointAddress & 0x80U) &&
            ((ep->bmAttributes & 3U) == USB_EP_TYPE_INTR) &&
            (ep->wMaxPacketSize > 0U) && (ep->wMaxPacketSize <= GAMEPAD_USB_RX_BYTES)) {
            return ep;
        }
    }
    return NULL;
}

/* No IF2 control requests: only inspect the already preserved raw config. */
static uint8_t FindAuxiliaryEndpoint(USBH_HandleTypeDef *host,
                                           USBH_EpDescTypeDef *ep)
{
    const uint8_t *raw = host->device.CfgDesc_Raw;
    uint16_t size = LE16(raw + 2U);
    uint16_t offset = USB_LEN_CFG_DESC;
    uint8_t match = 0U;
    HID_DescTypeDef desc;
    if ((size != 84U) || (size > sizeof(host->device.CfgDesc_Raw)) ||
        !ParseInterfaceHID(&desc, host, 2U) || (desc.wItemLength != 101U)) {
        return 0U;
    }
    while ((uint16_t)(offset + 2U) <= size) {
        uint8_t length = raw[offset];
        uint8_t type = raw[offset + 1U];
        if ((length < 2U) || ((uint32_t)offset + length > size)) {
            return 0U;
        }
        if (type == USB_DESC_TYPE_INTERFACE) {
            if (length < USB_LEN_IF_DESC) {
                return 0U;
            }
            match = (raw[offset + 2U] == 2U) && (raw[offset + 3U] == 0U) &&
                    (raw[offset + 4U] == 1U) && (raw[offset + 5U] == USB_HID_CLASS) &&
                    (raw[offset + 6U] == 0U) && (raw[offset + 7U] == 0U);
        } else if ((type == USB_DESC_TYPE_ENDPOINT) && match) {
            if ((length != 7U) || (raw[offset + 2U] != 0x83U) ||
                (raw[offset + 3U] != USB_EP_TYPE_INTR) ||
                (LE16(raw + offset + 4U) != 64U) || (raw[offset + 6U] != 10U)) {
                return 0U;
            }
            USBH_memset(ep, 0, sizeof(*ep));
            ep->bEndpointAddress = 0x83U;
            ep->bmAttributes = USB_EP_TYPE_INTR;
            ep->wMaxPacketSize = 64U;
            ep->bInterval = 10U;
            return 1U;
        }
        offset = (uint16_t)(offset + length);
    }
    return 0U;
}

static void ReleasePipes(USBH_HandleTypeDef *host, HIWONDER_HID_Handle *handle)
{
    uint8_t channel;
    if (handle->aux_opened) {
        USBH_ClosePipe(host, handle->aux.InPipe);
    }
    if (handle->aux_allocated) {
        USBH_FreePipe(host, handle->aux.InPipe);
    }
    for (channel = 0U; channel < handle->count; ++channel) {
        if (handle->opened[channel]) {
            USBH_ClosePipe(host, handle->hid[channel].InPipe);
        }
        if (handle->allocated[channel]) {
            USBH_FreePipe(host, handle->hid[channel].InPipe);
        }
    }
}

static USBH_StatusTypeDef USBH_HID_InterfaceInit(USBH_HandleTypeDef *host)
{
    HIWONDER_HID_Handle *handle;
    HID_DescTypeDef desc;
    GamepadReportProfile dual_profile = GAMEPAD_REPORT_LEGACY;
    USBH_StatusTypeDef status;
    USBH_EpDescTypeDef *ep[2];
    USBH_EpDescTypeDef aux_ep;
    uint8_t aux_enabled = 0U;
    uint8_t indices[2];
    uint8_t channel;
    uint8_t dedicated = USBH_HID_GamepadUsesInterruptInput(host);
    uint8_t count = 1U;
    USBH_HID_GamepadReset();
    gamepad_usb_channels = 0U;
    gamepad_usb_secondary_unavailable = 0U;
    gamepad_usb_aux_active = 0U;
    gamepad_usb_aux_reports = 0U;
    gamepad_usb_aux_submits = 0U;
    gamepad_usb_aux_notready = 0U;
    gamepad_usb_aux_stalls = 0U;
    gamepad_usb_aux_errors = 0U;
    gamepad_usb_aux_last_length = 0U;
    indices[0] = dedicated ? USBH_FindInterfaceIndex(host, 0U, 0U) :
                 USBH_FindInterface(host, host->pActiveClass->ClassCode, 0U, 0xFFU);
    if ((indices[0] == 0xFFU) || (indices[0] >= USBH_MAX_NUM_INTERFACES)) {
        return USBH_FAIL;
    }
    ep[0] = FindInputEndpoint(&host->device.CfgDesc.Itf_Desc[indices[0]], dedicated);
    if (ep[0] == NULL) {
        return USBH_FAIL;
    }
    /* Only explicitly identified schemas may open a second gamepad pipe.
       Unmatched historical 20BC and generic HID stay single-channel; interface
       2 is the system collection and must never provide vehicle controls. */
    if (USBH_HID_GamepadIs20BC(host) && ParseInterfaceHID(&desc, host, 0U)) {
        dual_profile = USBH_HID_Gamepad20BCProfile(host, ep[0]->wMaxPacketSize,
                                                   desc.wItemLength);
    }
    if ((dual_profile == GAMEPAD_REPORT_20BC_5500_8BYTE) ||
        (dual_profile == GAMEPAD_REPORT_20BC_5500_9BYTE)) {
        indices[1] = USBH_FindInterfaceIndex(host, 1U, 0U);
        ep[1] = (indices[1] < USBH_MAX_NUM_INTERFACES) ?
                FindInputEndpoint(&host->device.CfgDesc.Itf_Desc[indices[1]], 1U) : NULL;
        if ((ep[1] != NULL) &&
            (ep[1]->bEndpointAddress != ep[0]->bEndpointAddress) &&
            ParseInterfaceHID(&desc, host, 1U) &&
            (USBH_HID_Gamepad20BCProfile(host, ep[1]->wMaxPacketSize,
                                       desc.wItemLength) == dual_profile)) {
            count = 2U;
        } else {
            /* Explicit single-channel fallback when the second interface does
               not match the measured gamepad. Visible through the debugger. */
            gamepad_usb_secondary_unavailable = 1U;
        }
    }
    if ((dual_profile == GAMEPAD_REPORT_20BC_5500_9BYTE) &&
        FindAuxiliaryEndpoint(host, &aux_ep)) {
        aux_enabled = 1U;
    }
    status = USBH_SelectInterface(host, indices[0]);
    if (status != USBH_OK) {
        return status;
    }
    handle = (HIWONDER_HID_Handle *)USBH_malloc(sizeof(*handle));
    if (handle == NULL) {
        return USBH_FAIL;
    }
    USBH_memset(handle, 0, sizeof(*handle));
    handle->count = count;
    handle->stall_channel = 0xFFU;
    host->pActiveClass->pData = handle;
    for (channel = 0U; channel < count; ++channel) {
        HID_HandleTypeDef *hid = &handle->hid[channel];
        USBH_InterfaceDescTypeDef *itf = &host->device.CfgDesc.Itf_Desc[indices[channel]];
        handle->interfaces[channel] = itf->bInterfaceNumber;
        hid->Init = (itf->bInterfaceProtocol == HID_KEYBRD_BOOT_CODE) ? USBH_HID_KeybdInit :
                    (itf->bInterfaceProtocol == HID_MOUSE_BOOT_CODE) ? USBH_HID_MouseInit :
                    USBH_HID_GamepadInit;
        hid->state = USBH_HID_INIT;
        hid->ctl_state = USBH_HID_REQ_INIT;
        hid->InEp = ep[channel]->bEndpointAddress;
        hid->ep_addr = hid->InEp;
        hid->length = ep[channel]->wMaxPacketSize;
        hid->poll = ep[channel]->bInterval;
        if (hid->poll < (dedicated ? 1U : 50U)) {
            hid->poll = dedicated ? 1U : 50U;
        }
        hid->InPipe = USBH_AllocPipe(host, hid->InEp);
        if (hid->InPipe == 0xFFU) {
            status = USBH_FAIL;
            break;
        }
        handle->allocated[channel] = 1U;
        status = USBH_OpenPipe(host, hid->InPipe, hid->InEp, host->device.address,
                               host->device.speed, USB_EP_TYPE_INTR, hid->length);
        if (status != USBH_OK) {
            break;
        }
        handle->opened[channel] = 1U;
        USBH_LL_SetToggle(host, hid->InPipe, 0U);
    }
    if ((status == USBH_OK) && aux_enabled) {
        HID_HandleTypeDef *hid = &handle->aux;
        hid->InEp = aux_ep.bEndpointAddress;
        hid->ep_addr = hid->InEp;
        hid->length = aux_ep.wMaxPacketSize;
        hid->poll = aux_ep.bInterval;
        hid->pData = (uint8_t *)(void *)handle->aux_buffer;
        hid->state = USBH_HID_SYNC;
        hid->InPipe = USBH_AllocPipe(host, hid->InEp);
        if (hid->InPipe == 0xFFU) {
            status = USBH_FAIL;
        } else {
            handle->aux_allocated = 1U;
            status = USBH_OpenPipe(host, hid->InPipe, hid->InEp, host->device.address,
                                   host->device.speed, USB_EP_TYPE_INTR, hid->length);
            if (status == USBH_OK) {
                handle->aux_opened = 1U;
                handle->aux_active = 1U;
                USBH_LL_SetToggle(host, hid->InPipe, 0U);
            }
        }
    }
    if (status != USBH_OK) {
        ReleasePipes(host, handle);
        USBH_free(handle);
        host->pActiveClass->pData = NULL;
        return status;
    }
    gamepad_usb_channels = count;
    gamepad_usb_aux_active = handle->aux_active;
    return USBH_OK;
}

static USBH_StatusTypeDef USBH_HID_InterfaceDeInit(USBH_HandleTypeDef *host)
{
    HIWONDER_HID_Handle *handle = (HIWONDER_HID_Handle *)host->pActiveClass->pData;
    USBH_HID_GamepadReset();
    gamepad_usb_channels = 0U;
    gamepad_usb_aux_active = 0U;
    if (handle != NULL) {
        ReleasePipes(host, handle);
        USBH_free(handle);
        host->pActiveClass->pData = NULL;
    }
    return USBH_OK;
}

/* Dedicated requests address the actual interface number. ST's shared HID
   helpers hard-code wIndex=0, so keep this fix local to this custom class. */
static USBH_StatusTypeDef GamepadControl(USBH_HandleTypeDef *host, uint8_t number,
                                        uint8_t report_descriptor, uint16_t length)
{
    if (report_descriptor && (length > sizeof(host->device.Data))) {
        return USBH_NOT_SUPPORTED;
    }
    host->Control.setup.b.bmRequestType = USB_REQ_RECIPIENT_INTERFACE |
        (report_descriptor ? (USB_D2H | USB_REQ_TYPE_STANDARD) : (USB_H2D | USB_REQ_TYPE_CLASS));
    host->Control.setup.b.bRequest = report_descriptor ? USB_REQ_GET_DESCRIPTOR : USB_HID_SET_IDLE;
    host->Control.setup.b.wValue.w = report_descriptor ? 0x2200U : 0x3200U;
    host->Control.setup.b.wIndex.w = number;
    host->Control.setup.b.wLength.w = report_descriptor ? length : 0U;
    return USBH_CtlReq(host, report_descriptor ? host->device.Data : NULL,
                       report_descriptor ? length : 0U);
}

static USBH_StatusTypeDef USBH_HID_ClassRequest(USBH_HandleTypeDef *host)
{
    HIWONDER_HID_Handle *handle = (HIWONDER_HID_Handle *)host->pActiveClass->pData;
    HID_HandleTypeDef *hid = &handle->hid[handle->ctl_channel];
    USBH_StatusTypeDef status;
    uint8_t dedicated = USBH_HID_GamepadUsesInterruptInput(host);
    uint8_t number = handle->interfaces[handle->ctl_channel];
    switch (hid->ctl_state) {
    case USBH_HID_REQ_INIT:
    case USBH_HID_REQ_GET_HID_DESC:
        if (!ParseInterfaceHID(&hid->HID_Desc, host, number)) {
            return USBH_FAIL;
        }
        hid->ctl_state = USBH_HID_REQ_GET_REPORT_DESC;
        break;
    case USBH_HID_REQ_GET_REPORT_DESC:
        status = dedicated ? GamepadControl(host, number, 1U, hid->HID_Desc.wItemLength) :
                             USBH_HID_GetHIDReportDescriptor(host, hid->HID_Desc.wItemLength);
        if (status == USBH_OK) {
            hid->ctl_state = USBH_HID_REQ_SET_IDLE;
        } else if ((status == USBH_NOT_SUPPORTED) || (status == USBH_FAIL)) {
            return USBH_FAIL;
        }
        break;
    case USBH_HID_REQ_SET_IDLE:
        status = dedicated ? GamepadControl(host, number, 0U, 0U) : USBH_HID_SetIdle(host, 50U, 0U);
        if ((status == USBH_OK) || (status == USBH_NOT_SUPPORTED)) {
            hid->ctl_state = USBH_HID_REQ_SET_PROTOCOL;
        } else if (status == USBH_FAIL) {
            return USBH_FAIL;
        }
        break;
    case USBH_HID_REQ_SET_PROTOCOL:
        status = dedicated ? USBH_OK : USBH_HID_SetProtocol(host, 0U);
        if (status == USBH_OK) {
            hid->ctl_state = USBH_HID_REQ_IDLE;
            if ((uint8_t)(handle->ctl_channel + 1U) < handle->count) {
                handle->ctl_channel++;
            } else {
                host->pUser(host, HOST_USER_CLASS_ACTIVE);
                return USBH_OK;
            }
        } else if ((status == USBH_NOT_SUPPORTED) || (status == USBH_FAIL)) {
            return USBH_FAIL;
        }
        break;
    default:
        break;
    }
    return USBH_BUSY;
}

static USBH_StatusTypeDef ProcessChannel(USBH_HandleTypeDef *host,
                                         HIWONDER_HID_Handle *handle, uint8_t channel)
{
    HID_HandleTypeDef *hid = &handle->hid[channel];
    USBH_StatusTypeDef status;
    USBH_URBStateTypeDef urb;
    uint32_t length;
    switch (hid->state) {
    case USBH_HID_INIT:
        status = channel ? USBH_HID_GamepadInitChannel(host, hid, channel) : hid->Init(host);
        if (status != USBH_OK) {
            hid->state = USBH_HID_ERROR;
            return status;
        }
        hid->state = USBH_HID_GamepadUsesInterruptInput(host) ? USBH_HID_SYNC : USBH_HID_IDLE;
        break;
    case USBH_HID_IDLE:
        status = USBH_HID_GetReport(host, 1U, 0U, hid->pData, (uint8_t)hid->length);
        if ((status == USBH_OK) || (status == USBH_NOT_SUPPORTED)) {
            hid->state = USBH_HID_SYNC;
        } else if (status != USBH_BUSY) {
            hid->state = USBH_HID_ERROR;
            return USBH_FAIL;
        }
        break;
    case USBH_HID_SYNC:
        if (host->Timer & 1U) {
            hid->state = USBH_HID_GET_DATA;
        }
        break;
    case USBH_HID_GET_DATA:
        status = USBH_InterruptReceiveData(host, hid->pData, (uint8_t)hid->length, hid->InPipe);
        if (status == USBH_OK) {
            hid->state = USBH_HID_POLL;
            hid->timer = host->Timer;
            hid->DataReady = 0U;
        } else if (status != USBH_BUSY) {
            return status;
        }
        break;
    case USBH_HID_POLL:
        urb = USBH_LL_GetURBState(host, hid->InPipe);
        if ((urb == USBH_URB_DONE) && !hid->DataReady) {
            length = USBH_LL_GetLastXferSize(host, hid->InPipe);
            hid->DataReady = 1U;
            if (hid->Init == USBH_HID_GamepadInit) {
                USBH_HID_GamepadReceiveChannel(host, hid, channel, length);
                USBH_HID_EventCallback(host);
            } else if (length != 0U) {
                USBH_HID_FifoWrite(&hid->fifo, hid->pData, hid->length);
                USBH_HID_EventCallback(host);
            }
        } else if ((urb == USBH_URB_STALL) &&
                   ((handle->stall_channel == 0xFFU) || (handle->stall_channel == channel))) {
            handle->stall_channel = channel;
            status = USBH_ClrFeature(host, hid->InEp);
            if (status == USBH_OK) {
                USBH_LL_SetToggle(host, hid->InPipe, 0U);
                hid->state = USBH_HID_GET_DATA;
                handle->stall_channel = 0xFFU;
            } else if (status != USBH_BUSY) {
                handle->stall_channel = 0xFFU;
                return USBH_FAIL;
            }
        }
        break;
    default:
        break;
    }
    return USBH_OK;
}

/* Never decode IF2, publish it, emit application callbacks, or renew a gamepad
   deadline. On STALL, leave this pipe disabled until device re-enumeration.
   This avoids competing with gamepad halt recovery on the shared EP0; the
   independent gamepad deadline still expires if auxiliary service stops. */
static void ProcessAuxiliaryInput(USBH_HandleTypeDef *host, HIWONDER_HID_Handle *handle)
{
    HID_HandleTypeDef *hid = &handle->aux;
    USBH_URBStateTypeDef urb;
    USBH_StatusTypeDef status;
    if (!handle->aux_active) {
        return;
    }
    switch (hid->state) {
    case USBH_HID_SYNC:
        if (host->Timer & 1U) {
            hid->state = USBH_HID_GET_DATA;
        }
        break;
    case USBH_HID_GET_DATA:
        status = USBH_InterruptReceiveData(host, hid->pData, (uint8_t)hid->length, hid->InPipe);
        if (status == USBH_OK) {
            gamepad_usb_aux_submits++;
            hid->state = USBH_HID_POLL;
            hid->timer = host->Timer;
            hid->DataReady = 0U;
        } else if (status != USBH_BUSY) {
            gamepad_usb_aux_errors++;
        }
        break;
    case USBH_HID_POLL:
        urb = USBH_LL_GetURBState(host, hid->InPipe);
        if ((urb == USBH_URB_DONE) && !hid->DataReady) {
            gamepad_usb_aux_last_length = USBH_LL_GetLastXferSize(host, hid->InPipe);
            gamepad_usb_aux_reports++;
            hid->DataReady = 1U;
        } else if (urb == USBH_URB_STALL) {
            gamepad_usb_aux_stalls++;
            handle->aux_active = 0U;
            gamepad_usb_aux_active = 0U;
        } else if (!hid->DataReady && (urb == USBH_URB_NOTREADY)) {
            gamepad_usb_aux_notready++;
            hid->DataReady = 1U;
        } else if (!hid->DataReady && (urb == USBH_URB_ERROR)) {
            gamepad_usb_aux_errors++;
            hid->DataReady = 1U;
        }
        break;
    default:
        break;
    }
}

static USBH_StatusTypeDef USBH_HID_Process(USBH_HandleTypeDef *host)
{
    HIWONDER_HID_Handle *handle = (HIWONDER_HID_Handle *)host->pActiveClass->pData;
    uint8_t channel;
    for (channel = 0U; channel < handle->count; ++channel) {
        USBH_StatusTypeDef status = ProcessChannel(host, handle, channel);
        if (status != USBH_OK) {
            return status;
        }
    }
    ProcessAuxiliaryInput(host, handle);
    return USBH_OK;
}

static USBH_StatusTypeDef USBH_HID_SOFProcess(USBH_HandleTypeDef *host)
{
    HIWONDER_HID_Handle *handle = (HIWONDER_HID_Handle *)host->pActiveClass->pData;
    uint8_t channel;
    USBH_HID_GamepadCheckTimeout(host);
    for (channel = 0U; channel < handle->count; ++channel) {
        HID_HandleTypeDef *hid = &handle->hid[channel];
        if ((hid->state == USBH_HID_POLL) && (handle->stall_channel != channel) &&
            ((uint32_t)(host->Timer - hid->timer) >= hid->poll)) {
            USBH_URBStateTypeDef urb = USBH_LL_GetURBState(host, hid->InPipe);
            /* Let the foreground consume a completed packet or recover a
               stalled pipe before resubmitting this endpoint's next token. */
            if ((urb != USBH_URB_STALL) &&
                !((urb == USBH_URB_DONE) && !hid->DataReady)) {
                hid->state = USBH_HID_GET_DATA;
            }
        }
    }
    if (handle->aux_active) {
        HID_HandleTypeDef *hid = &handle->aux;
        if ((hid->state == USBH_HID_POLL) &&
            ((uint32_t)(host->Timer - hid->timer) >= hid->poll)) {
            USBH_URBStateTypeDef urb = USBH_LL_GetURBState(host, hid->InPipe);
            if ((urb != USBH_URB_STALL) &&
                !((urb == USBH_URB_DONE) && !hid->DataReady)) {
                hid->state = USBH_HID_GET_DATA;
            }
        }
    }
    return USBH_OK;
}

HID_TypeTypeDef HIWONDER_USBH_HID_GetDeviceType(USBH_HandleTypeDef *host)
{
    HID_HandleTypeDef *hid;
    if ((host == NULL) || (host->gState != HOST_CLASS) ||
        (host->pActiveClass != &HIWONDER_HID_CLASS) || (host->pActiveClass->pData == NULL)) {
        return HID_UNKNOWN;
    }
    hid = (HID_HandleTypeDef *)host->pActiveClass->pData;
    if (hid->Init == USBH_HID_GamepadInit) {
        return (HID_TypeTypeDef)HID_GAMEPAD;
    }
    if (hid->Init == USBH_HID_KeybdInit) {
        return HID_KEYBOARD;
    }
    if (hid->Init == USBH_HID_MouseInit) {
        return HID_MOUSE;
    }
    return HID_UNKNOWN;
}
