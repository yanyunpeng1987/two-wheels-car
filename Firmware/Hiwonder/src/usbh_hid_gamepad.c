#include "usbh_hid_gamepad.h"

HID_GAMEPAD_Info_TypeDef gamepad_info;
/* Word-aligned storage also covers the receiver's 64-byte endpoint capacity. */
static uint32_t gamepad_rx_buffer[GAMEPAD_USB_RX_BYTES / sizeof(uint32_t)];
static enum GamepadReportProfile gamepad_profile = GAMEPAD_REPORT_LEGACY;
static uint8_t gamepad_report_valid;

volatile uint16_t gamepad_raw_buttons;
volatile uint32_t gamepad_reports_accepted;
volatile uint32_t gamepad_reports_rejected;

static void Gamepad_ClearInput(void)
{
    USBH_memset(&gamepad_info, 0, sizeof(gamepad_info));
    gamepad_report_valid = 0U;
    gamepad_raw_buttons = 0U;
    info = NULL;
}

uint8_t USBH_HID_GamepadIs20BC(const USBH_HandleTypeDef *phost)
{
    return (phost != NULL) && (phost->device.DevDesc.idVendor == 0x20BCU) &&
           (phost->device.DevDesc.idProduct == 0x5500U);
}

void USBH_HID_GamepadReset(void)
{
    Gamepad_ClearInput();
    gamepad_profile = GAMEPAD_REPORT_LEGACY;
    gamepad_reports_accepted = 0U;
    gamepad_reports_rejected = 0U;
}

USBH_StatusTypeDef USBH_HID_GamepadInit(USBH_HandleTypeDef *phost)
{
    HID_HandleTypeDef *hid = (HID_HandleTypeDef *)phost->pActiveClass->pData;
    USBH_HID_GamepadReset();
    if ((hid == NULL) || (hid->length == 0U) || (hid->length > GAMEPAD_USB_RX_BYTES)) {
        return USBH_FAIL;
    }
    gamepad_profile = USBH_HID_GamepadIs20BC(phost) ?
                      GAMEPAD_REPORT_20BC_5500 : GAMEPAD_REPORT_LEGACY;
    USBH_memset(gamepad_rx_buffer, 0, sizeof(gamepad_rx_buffer));
    hid->pData = (uint8_t *)(void *)gamepad_rx_buffer;
    return USBH_OK;
}

void USBH_HID_GamepadReceive(USBH_HandleTypeDef *phost, uint32_t length)
{
    HID_HandleTypeDef *hid = (HID_HandleTypeDef *)phost->pActiveClass->pData;
    if ((hid == NULL) || (hid->pData == NULL) || (length > hid->length) ||
        (length > GAMEPAD_USB_RX_BYTES) ||
        !Gamepad_DecodeReport(gamepad_profile, hid->pData, (uint16_t)length, &gamepad_info)) {
        Gamepad_ClearInput();
        gamepad_reports_rejected++;
        return;
    }
    gamepad_raw_buttons = (gamepad_profile == GAMEPAD_REPORT_20BC_5500) ?
                          ((uint16_t)hid->pData[0] | ((uint16_t)hid->pData[1] << 8)) :
                          gamepad_info.buttons;
    gamepad_report_valid = 1U;
    gamepad_reports_accepted++;
}

HID_GAMEPAD_Info_TypeDef *USBH_HID_GetGamepadInfo(USBH_HandleTypeDef *phost)
{
    if ((phost == NULL) || (phost->gState != HOST_CLASS) || !gamepad_report_valid) {
        return NULL;
    }
    return &gamepad_info;
}
