#include "usbh_hid_gamepad.h"
#include "usbh_hiwonder_hid.h"

HID_GAMEPAD_Info_TypeDef *info = NULL;

void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
    /* Unknown HID and keyboard/mouse callbacks cannot publish vehicle controls. */
    if (HIWONDER_USBH_HID_GetDeviceType(phost) == (HID_TypeTypeDef)HID_GAMEPAD) {
        info = USBH_HID_GetGamepadInfo(phost);
    }
}
