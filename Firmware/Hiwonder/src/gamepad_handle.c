#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_hid.h"
#include "usbh_hid_gamepad.h"
#include "ccd.h"
#include <stdio.h>
#include <string.h>


extern UART_HandleTypeDef huart1;

HID_GAMEPAD_Info_TypeDef *info = NULL;


void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
    static HID_GAMEPAD_Info_TypeDef last_info;
    static char last_direction_msg = 'I';
    static char last_button = 'R';

    switch(USBH_HID_GetDeviceType(phost)) {
        case 0xFF: {/* 手柄数据 */
                info = USBH_HID_GetGamepadInfo(phost);
								
                if(info == NULL) {
                    break;
                }else{
								  char buffer[10];

									int len = sprintf(buffer, "%d ", info->lx);
//									HAL_UART_Transmit(&huart1, (uint8_t *)buffer, len, HAL_MAX_DELAY);

									
								}
                break;
            }
        default:
            break;
    }
}

//const HID_GAMEPAD_Info_TypeDef* get_gamepad_info(void)
//{
//    return info;
//}

