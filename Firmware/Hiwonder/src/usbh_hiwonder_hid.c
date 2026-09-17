#include "usbh_hid.h"
#include "usbh_hid_parser.h"
#include "usbh_hid_gamepad.h"
#include "usbh_hiwonder_hid.h"

static USBH_StatusTypeDef USBH_HID_InterfaceInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_InterfaceDeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_ClassRequest(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_HID_SOFProcess(USBH_HandleTypeDef *phost);
static void  USBH_HID_ParseHIDDesc(HID_DescTypeDef *desc, uint8_t *buf);

extern USBH_StatusTypeDef USBH_HID_GamepadInit(USBH_HandleTypeDef *phost);

USBH_ClassTypeDef  HIWONDER_HID_CLASS = {
    "HID",
    USB_HID_CLASS,
    USBH_HID_InterfaceInit,
    USBH_HID_InterfaceDeInit,
    USBH_HID_ClassRequest,
    USBH_HID_Process,
    USBH_HID_SOFProcess,
    NULL,
};


/** @defgroup USBH_HID_CORE_Private_Functions
* @{
*/


/**
  * @brief  USBH_HID_InterfaceInit
  *         The function init the HID class.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_InterfaceInit(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status;
    HID_HandleTypeDef *hid;
    USBH_InterfaceDescTypeDef *itf;
    USBH_EpDescTypeDef *ep = NULL;
    uint8_t interface;
    uint8_t num;
    uint8_t max_ep;
    uint8_t is_new_receiver = USBH_HID_GamepadIs20BC(phost);

    USBH_HID_GamepadReset();
    if (is_new_receiver) {
        /* User-selected channel: USB interface NUMBER 0, alternate setting 0.
           All existing HID control helpers address interface 0 as well. */
        interface = USBH_FindInterfaceIndex(phost, 0U, 0U);
    } else {
        interface = USBH_FindInterface(phost, phost->pActiveClass->ClassCode, 0U, 0xFFU);
    }
    if ((interface == 0xFFU) || (interface >= USBH_MAX_NUM_INTERFACES)) {
        return USBH_FAIL;
    }
    itf = &phost->device.CfgDesc.Itf_Desc[interface];
    if ((itf->bInterfaceClass != USB_HID_CLASS) || (itf->bInterfaceSubClass != 0U) ||
        (is_new_receiver && (itf->bInterfaceProtocol != 0U))) {
        return USBH_FAIL;
    }
    if ((itf->bInterfaceProtocol != 0U) &&
        (itf->bInterfaceProtocol != HID_KEYBRD_BOOT_CODE) &&
        (itf->bInterfaceProtocol != HID_MOUSE_BOOT_CODE)) {
        return USBH_FAIL;
    }
    max_ep = (itf->bNumEndpoints <= USBH_MAX_NUM_ENDPOINTS) ?
             itf->bNumEndpoints : USBH_MAX_NUM_ENDPOINTS;
    for (num = 0U; num < max_ep; num++) {
        if ((itf->Ep_Desc[num].bEndpointAddress & 0x80U) &&
            ((itf->Ep_Desc[num].bmAttributes & 0x03U) == USB_EP_TYPE_INTR)) {
            ep = &itf->Ep_Desc[num];
            break;
        }
    }
    if ((ep == NULL) || (ep->wMaxPacketSize == 0U) ||
        (ep->wMaxPacketSize > GAMEPAD_USB_RX_BYTES)) {
        return USBH_FAIL;
    }
    status = USBH_SelectInterface(phost, interface);
    if (status != USBH_OK) {
        return status;
    }
    hid = (HID_HandleTypeDef *)USBH_malloc(sizeof(HID_HandleTypeDef));
    if (hid == NULL) {
        return USBH_FAIL;
    }
    USBH_memset(hid, 0, sizeof(HID_HandleTypeDef));
    phost->pActiveClass->pData = hid;
    if (itf->bInterfaceProtocol == HID_KEYBRD_BOOT_CODE) {
        hid->Init = USBH_HID_KeybdInit;
    } else if (itf->bInterfaceProtocol == HID_MOUSE_BOOT_CODE) {
        hid->Init = USBH_HID_MouseInit;
    } else {
        /* The selected report profile handles decoding; there is no VID allowlist. */
        hid->Init = USBH_HID_GamepadInit;
    }
    hid->state = USBH_HID_INIT;
    hid->ctl_state = USBH_HID_REQ_INIT;
    hid->ep_addr = ep->bEndpointAddress;
    hid->InEp = ep->bEndpointAddress;
    hid->length = ep->wMaxPacketSize;
    hid->poll = ep->bInterval;
    if (hid->poll < (is_new_receiver ? 1U : 50U)) {
        hid->poll = is_new_receiver ? 1U : 50U;
    }
    hid->InPipe = USBH_AllocPipe(phost, hid->InEp);
    if (hid->InPipe == 0xFFU) {
        USBH_free(hid);
        phost->pActiveClass->pData = NULL;
        return USBH_FAIL;
    }
    status = USBH_OpenPipe(phost, hid->InPipe, hid->InEp, phost->device.address,
                           phost->device.speed, USB_EP_TYPE_INTR, hid->length);
    if (status != USBH_OK) {
        USBH_FreePipe(phost, hid->InPipe);
        USBH_free(hid);
        phost->pActiveClass->pData = NULL;
        return status;
    }
    USBH_LL_SetToggle(phost, hid->InPipe, 0U);
    return USBH_OK;
}

/**
  * @brief  USBH_HID_InterfaceDeInit
  *         The function DeInit the Pipes used for the HID class.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_InterfaceDeInit(USBH_HandleTypeDef *phost)
{
    HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

    USBH_HID_GamepadReset();
    if (HID_Handle == NULL) {
        return USBH_OK;
    }
    if (HID_Handle->InPipe != 0x00U) {
        USBH_ClosePipe(phost, HID_Handle->InPipe);
        USBH_FreePipe(phost, HID_Handle->InPipe);
        HID_Handle->InPipe = 0U;     /* Reset the pipe as Free */
    }

    if (HID_Handle->OutPipe != 0x00U) {
        USBH_ClosePipe(phost, HID_Handle->OutPipe);
        USBH_FreePipe(phost, HID_Handle->OutPipe);
        HID_Handle->OutPipe = 0U;     /* Reset the pipe as Free */
    }

    if (phost->pActiveClass->pData) {
        USBH_free(phost->pActiveClass->pData);
        phost->pActiveClass->pData = 0U;
    }

    return USBH_OK;
}

/**
  * @brief  USBH_HID_ClassRequest
  *         The function is responsible for handling Standard requests
  *         for HID class.
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_ClassRequest(USBH_HandleTypeDef *phost)
{

    USBH_StatusTypeDef status         = USBH_BUSY;
    USBH_StatusTypeDef classReqStatus = USBH_BUSY;
    HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

    /* Switch HID state machine */
    switch (HID_Handle->ctl_state) {
        case USBH_HID_REQ_INIT:
        case USBH_HID_REQ_GET_HID_DESC:

            USBH_HID_ParseHIDDesc(&HID_Handle->HID_Desc, phost->device.CfgDesc_Raw);

            HID_Handle->ctl_state = USBH_HID_REQ_GET_REPORT_DESC;

            break;
        case USBH_HID_REQ_GET_REPORT_DESC:

            /* Get Report Desc */
            classReqStatus = USBH_HID_GetHIDReportDescriptor(phost, HID_Handle->HID_Desc.wItemLength);
            if (classReqStatus == USBH_OK) {
                /* The descriptor is available in phost->device.Data */
                HID_Handle->ctl_state = USBH_HID_REQ_SET_IDLE;

            } else if (classReqStatus == USBH_NOT_SUPPORTED) {
                USBH_ErrLog("Control error: HID: Device Get Report Descriptor request failed");
                status = USBH_FAIL;
            } else {
                /* .. */
            }

            break;

        case USBH_HID_REQ_SET_IDLE:
            /* set Idle */
            classReqStatus = USBH_HID_SetIdle(phost, 50U, 0U);
            if (classReqStatus == USBH_OK) {
                HID_Handle->ctl_state = USBH_HID_REQ_SET_PROTOCOL;
            } else {
                if (classReqStatus == USBH_NOT_SUPPORTED) {
                    HID_Handle->ctl_state = USBH_HID_REQ_SET_PROTOCOL;
                }
            }
            break;

        case USBH_HID_REQ_SET_PROTOCOL:
            /* 20BC:5500 is non-Boot HID: it already uses report protocol.
               SET_PROTOCOL is a Boot-subclass request and may STALL here. */
            classReqStatus = USBH_HID_GamepadIs20BC(phost) ? USBH_OK :
                             USBH_HID_SetProtocol(phost, 0U);

            if (classReqStatus == USBH_OK) {
                HID_Handle->ctl_state = USBH_HID_REQ_IDLE;

                /* all requests performed*/
                phost->pUser(phost, HOST_USER_CLASS_ACTIVE);
                status = USBH_OK;
            } else if (classReqStatus == USBH_NOT_SUPPORTED) {
                USBH_ErrLog("Control error: HID: Device Set protocol request failed");
                status = USBH_FAIL;
            } else {
                /* .. */
            }
            break;

        case USBH_HID_REQ_IDLE:
        default:
            break;
    }

    return status;
}

/**
  * @brief  USBH_HID_Process
  *         The function is for managing state machine for HID data transfers
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_Process(USBH_HandleTypeDef *phost)
{
    USBH_StatusTypeDef status = USBH_OK;
    HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;
    uint32_t XferSize;
    USBH_URBStateTypeDef urb;
    switch (HID_Handle->state) {
        case USBH_HID_INIT:
            status = HID_Handle->Init(phost);
            if (status != USBH_OK) {
                HID_Handle->state = USBH_HID_ERROR;
                return status;
            }
            /* This receiver sends its 9-byte input reports through interrupt IN.
               Do not depend on a startup control GET_REPORT request. */
            HID_Handle->state = USBH_HID_GamepadIs20BC(phost) ? USBH_HID_SYNC : USBH_HID_IDLE;

#if (USBH_USE_OS == 1U)
            phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
            (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
            (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
            break;

        case USBH_HID_IDLE:
            status = USBH_HID_GetReport(phost, 0x01U, 0U, HID_Handle->pData, (uint8_t)HID_Handle->length);
            if (status == USBH_OK) {
                HID_Handle->state = USBH_HID_SYNC;
            } else if (status == USBH_BUSY) {
                HID_Handle->state = USBH_HID_IDLE;
                status = USBH_OK;
            } else if (status == USBH_NOT_SUPPORTED) {
                HID_Handle->state = USBH_HID_SYNC;
                status = USBH_OK;
            } else {
                HID_Handle->state = USBH_HID_ERROR;
                status = USBH_FAIL;
            }

#if (USBH_USE_OS == 1U)
            phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
            (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
            (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
            break;

        case USBH_HID_SYNC:
            /* Sync with start of Even Frame */
            if (phost->Timer & 1U) {
                HID_Handle->state = USBH_HID_GET_DATA;
            }

#if (USBH_USE_OS == 1U)
            phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
            (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
            (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
            break;

        case USBH_HID_GET_DATA:
            USBH_InterruptReceiveData(phost, HID_Handle->pData,
                                      (uint8_t)HID_Handle->length,
                                      HID_Handle->InPipe);

            HID_Handle->state = USBH_HID_POLL;
            HID_Handle->timer = phost->Timer;
            HID_Handle->DataReady = 0U;
            break;

        case USBH_HID_POLL:
            urb = USBH_LL_GetURBState(phost, HID_Handle->InPipe) ;
            if (urb == USBH_URB_DONE) {
                XferSize = USBH_LL_GetLastXferSize(phost, HID_Handle->InPipe);

                if (HID_Handle->DataReady == 0U) {
                    HID_Handle->DataReady = 1U;
                    if (HID_Handle->Init == USBH_HID_GamepadInit) {
                        /* Decode the actual packet, not endpoint capacity or FIFO padding. */
                        USBH_HID_GamepadReceive(phost, XferSize);
                        USBH_HID_EventCallback(phost);
                    } else if (XferSize != 0U) {
                        USBH_HID_FifoWrite(&HID_Handle->fifo, HID_Handle->pData, HID_Handle->length);
                        USBH_HID_EventCallback(phost);
                    }

#if (USBH_USE_OS == 1U)
                    phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
                    (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
                    (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
                }
            } else {
                /* IN Endpoint Stalled */
                if (USBH_LL_GetURBState(phost, HID_Handle->InPipe) == USBH_URB_STALL) {
                    /* Issue Clear Feature on interrupt IN endpoint */
                    if (USBH_ClrFeature(phost, HID_Handle->ep_addr) == USBH_OK) {
                        /* Change state to issue next IN token */
                        HID_Handle->state = USBH_HID_GET_DATA;
                    }
                }
            }
            break;

        default:
            break;
    }

    return status;
}

/**
  * @brief  USBH_HID_SOFProcess
  *         The function is for managing the SOF Process
  * @param  phost: Host handle
  * @retval USBH Status
  */
static USBH_StatusTypeDef USBH_HID_SOFProcess(USBH_HandleTypeDef *phost)
{
    HID_HandleTypeDef *HID_Handle = (HID_HandleTypeDef *) phost->pActiveClass->pData;

    if (HID_Handle->state == USBH_HID_POLL) {
        if ((phost->Timer - HID_Handle->timer) >= HID_Handle->poll) {
            HID_Handle->state = USBH_HID_GET_DATA;

#if (USBH_USE_OS == 1U)
            phost->os_msg = (uint32_t)USBH_URB_EVENT;
#if (osCMSIS < 0x20000U)
            (void)osMessagePut(phost->os_event, phost->os_msg, 0U);
#else
            (void)osMessageQueuePut(phost->os_event, &phost->os_msg, 0U, NULL);
#endif
#endif
        }
    }
    return USBH_OK;
}


/**
  * @brief  USBH_ParseHIDDesc
  *         This function Parse the HID descriptor
  * @param  desc: HID Descriptor
  * @param  buf: Buffer where the source descriptor is available
  * @retval None
  */
static void  USBH_HID_ParseHIDDesc(HID_DescTypeDef *desc, uint8_t *buf)
{
    USBH_DescHeader_t *pdesc = (USBH_DescHeader_t *)buf;
    uint16_t CfgDescLen;
    uint16_t ptr;

    CfgDescLen = LE16(buf + 2U);

    if (CfgDescLen > USB_CONFIGURATION_DESC_SIZE) {
        ptr = USB_LEN_CFG_DESC;

        while (ptr < CfgDescLen) {
            pdesc = USBH_GetNextDesc((uint8_t *)pdesc, &ptr);

            if (pdesc->bDescriptorType == USB_DESC_TYPE_HID) {
                desc->bLength = *(uint8_t *)((uint8_t *)pdesc + 0U);
                desc->bDescriptorType = *(uint8_t *)((uint8_t *)pdesc + 1U);
                desc->bcdHID = LE16((uint8_t *)pdesc + 2U);
                desc->bCountryCode = *(uint8_t *)((uint8_t *)pdesc + 4U);
                desc->bNumDescriptors = *(uint8_t *)((uint8_t *)pdesc + 5U);
                desc->bReportDescriptorType = *(uint8_t *)((uint8_t *)pdesc + 6U);
                desc->wItemLength = LE16((uint8_t *)pdesc + 7U);
                break;
            }
        }
    }
}



/**
  * @brief  HIWONDER_USBH_HID_GetDeviceType
  *         Return Device function.
  * @param  phost: Host handle
  * @retval HID function: HID_MOUSE / HID_KEYBOARD
  */
HID_TypeTypeDef HIWONDER_USBH_HID_GetDeviceType(USBH_HandleTypeDef *phost)
{
    HID_HandleTypeDef *hid;
    if ((phost == NULL) || (phost->gState != HOST_CLASS) ||
        (phost->pActiveClass != &HIWONDER_HID_CLASS) || (phost->pActiveClass->pData == NULL)) {
        return HID_UNKNOWN;
    }
    hid = (HID_HandleTypeDef *)phost->pActiveClass->pData;
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
