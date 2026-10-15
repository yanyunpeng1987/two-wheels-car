#ifndef __USBH_HID_GAMEPAD_H
#define __USBH_HID_GAMEPAD_H

#include "usbh_hid.h"
#include "gamepad_report.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAMEPAD_USB_RX_BYTES 64U
#define GAMEPAD_PERIODIC_TIMEOUT_MS 500U
#define GAMEPAD_0810_TIMEOUT_MS GAMEPAD_PERIODIC_TIMEOUT_MS

extern HID_GAMEPAD_Info_TypeDef *info;
/* Read-only debugger observations; no logging in the control interrupt. */
extern volatile uint16_t gamepad_raw_buttons;
extern volatile uint32_t gamepad_reports_accepted;
extern volatile uint32_t gamepad_reports_rejected;
extern volatile uint32_t gamepad_reports_ignored;
extern volatile uint32_t gamepad_reports_timed_out;
extern volatile uint8_t gamepad_active_channel;
extern volatile uint32_t gamepad_channel_accepted[2];
extern volatile uint32_t gamepad_channel_rejected[2];
extern volatile uint16_t gamepad_channel_raw_buttons[2];
extern volatile HID_GAMEPAD_Info_TypeDef gamepad_channel_info[2];
extern volatile uint8_t gamepad_channel_raw[2][8];
/* Number of independently opened USB interrupt pipes; 0810 uses one pipe
   carrying both report IDs. A failed optional interface remains single-channel. */
extern volatile uint8_t gamepad_usb_channels;
extern volatile uint32_t gamepad_usb_secondary_unavailable;

uint8_t USBH_HID_GamepadIs20BC(const USBH_HandleTypeDef *phost);
uint8_t USBH_HID_GamepadIs0810(const USBH_HandleTypeDef *phost);
uint8_t USBH_HID_GamepadUsesInterruptInput(const USBH_HandleTypeDef *phost);
/* Shared by interface discovery and decoder initialization. Unmatched 20BC
   metadata keeps the historical single-channel profile; other IDs are legacy. */
GamepadReportProfile USBH_HID_Gamepad20BCProfile(const USBH_HandleTypeDef *phost,
                                               uint16_t endpoint_bytes,
                                               uint16_t descriptor_bytes);
USBH_StatusTypeDef USBH_HID_GamepadInit(USBH_HandleTypeDef *phost);
USBH_StatusTypeDef USBH_HID_GamepadInitChannel(USBH_HandleTypeDef *phost,
                                             HID_HandleTypeDef *hid, uint8_t channel);
void USBH_HID_GamepadReset(void);
void USBH_HID_GamepadCheckTimeout(const USBH_HandleTypeDef *phost);
/* Call before and after USBH_Process in the control context. Raw unsigned
   cycle deltas, not HAL_GetTick()/USB SOF, enforce each periodic report lease.
   The pre-call timestamps packets consumed in that host-processing iteration.
   Invalid transport or clock clears output/arming without resetting counters. */
void USBH_HID_GamepadSafetyPoll(USBH_HandleTypeDef *phost, uint32_t now_cycles,
                               uint32_t cycles_per_ms);
void USBH_HID_GamepadReceive(USBH_HandleTypeDef *phost, uint32_t length);
void USBH_HID_GamepadReceiveChannel(USBH_HandleTypeDef *phost,
                                   HID_HandleTypeDef *hid, uint8_t channel,
                                   uint32_t length);
HID_GAMEPAD_Info_TypeDef *USBH_HID_GetGamepadInfo(USBH_HandleTypeDef *phost);

#ifdef __cplusplus
}
#endif
#endif /* __USBH_HID_GAMEPAD_H */
