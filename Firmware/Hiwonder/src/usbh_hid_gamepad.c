#include "usbh_hid_gamepad.h"
#include "gamepad_arbiter.h"
#include "usbh_hiwonder_hid.h"

/* USBProcess and gamepad_scan run in the same EXTI control IRQ. USB SOF
   has equal NVIC priority and cannot preempt these state/publication updates. */

HID_GAMEPAD_Info_TypeDef gamepad_info;
static uint32_t gamepad_rx_buffer[2][GAMEPAD_USB_RX_BYTES / sizeof(uint32_t)];
static enum GamepadReportProfile gamepad_profile = GAMEPAD_REPORT_LEGACY;
static uint8_t gamepad_report_valid[2];
static uint32_t gamepad_last_report_timer[2];
/* Independent of USB SOF: supplied immediately before/after host processing. */
static uint32_t gamepad_last_report_cycles[2];
static uint32_t gamepad_safety_cycles;
static uint32_t gamepad_safety_timeout_cycles;
static uint8_t gamepad_safety_clock_valid;

volatile uint16_t gamepad_raw_buttons;
volatile uint32_t gamepad_reports_accepted;
volatile uint32_t gamepad_reports_rejected;
volatile uint32_t gamepad_reports_ignored;
volatile uint32_t gamepad_reports_timed_out;
volatile uint8_t gamepad_active_channel = GAMEPAD_ARBITER_NO_CHANNEL;
volatile uint32_t gamepad_channel_accepted[2];
volatile uint32_t gamepad_channel_rejected[2];
volatile uint16_t gamepad_channel_raw_buttons[2];
volatile HID_GAMEPAD_Info_TypeDef gamepad_channel_info[2];
volatile uint8_t gamepad_channel_raw[2][8];
volatile uint8_t gamepad_usb_channels;
volatile uint32_t gamepad_usb_secondary_unavailable;

static uint8_t Gamepad_UsesArbiter(void)
{
    return (gamepad_profile == GAMEPAD_REPORT_0810_0001) ||
           (gamepad_profile == GAMEPAD_REPORT_20BC_5500_8BYTE) ||
           (gamepad_profile == GAMEPAD_REPORT_20BC_5500_9BYTE);
}

static void Gamepad_ClearPublished(void)
{
    USBH_memset(&gamepad_info, 0, sizeof(gamepad_info));
    gamepad_raw_buttons = 0U;
    info = NULL;
}

static uint8_t Gamepad_TransportReady(const USBH_HandleTypeDef *phost)
{
    return (phost != NULL) && (phost->gState == HOST_CLASS) &&
           phost->device.is_connected && !phost->device.is_disconnected &&
           phost->device.PortEnabled && (phost->pActiveClass != NULL) &&
           (phost->pActiveClass->pData != NULL) &&
           (HIWONDER_USBH_HID_GetDeviceType((USBH_HandleTypeDef *)phost) ==
            (HID_TypeTypeDef)HID_GAMEPAD);
}

static void Gamepad_InvalidateTransport(const USBH_HandleTypeDef *phost)
{
    uint8_t channel;
    uint32_t now = (phost != NULL) ? phost->Timer : 0U;
    for (channel = 0U; channel < 2U; ++channel) {
        gamepad_report_valid[channel] = 0U;
        if (Gamepad_UsesArbiter()) {
            GamepadArbiter_Invalidate(channel, now);
        }
    }
    gamepad_active_channel = GAMEPAD_ARBITER_NO_CHANNEL;
    Gamepad_ClearPublished();
}

void USBH_HID_GamepadSafetyPoll(USBH_HandleTypeDef *phost, uint32_t now_cycles,
                               uint32_t cycles_per_ms)
{
    uint8_t channel;
    gamepad_safety_cycles = now_cycles;
    gamepad_safety_clock_valid = (cycles_per_ms != 0U) &&
        (cycles_per_ms <= UINT32_MAX / GAMEPAD_PERIODIC_TIMEOUT_MS);
    gamepad_safety_timeout_cycles = gamepad_safety_clock_valid ?
        cycles_per_ms * GAMEPAD_PERIODIC_TIMEOUT_MS : 0U;
    if (!Gamepad_TransportReady(phost) || !gamepad_safety_clock_valid) {
        /* A disabled port need not generate DISCONNECTION. Clear cached info
           and both arming states, but retain profile and diagnostic evidence. */
        Gamepad_InvalidateTransport(phost);
        return;
    }
    if (!Gamepad_UsesArbiter()) {
        /* Legacy/unmeasured profiles have no proven periodic report contract. */
        return;
    }
    for (channel = 0U; channel < 2U; ++channel) {
        if (gamepad_report_valid[channel] &&
            ((uint32_t)(now_cycles - gamepad_last_report_cycles[channel]) >=
             gamepad_safety_timeout_cycles)) {
            gamepad_report_valid[channel] = 0U;
            gamepad_reports_timed_out++;
            GamepadArbiter_Invalidate(channel, phost->Timer);
        }
    }
    gamepad_active_channel = GamepadArbiter_GetActiveChannel();
    if (gamepad_active_channel == GAMEPAD_ARBITER_NO_CHANNEL) {
        Gamepad_ClearPublished();
    }
}

uint8_t USBH_HID_GamepadIs20BC(const USBH_HandleTypeDef *phost)
{
    return (phost != NULL) && (phost->device.DevDesc.idVendor == 0x20BCU) &&
           (phost->device.DevDesc.idProduct == 0x5500U);
}

uint8_t USBH_HID_GamepadIs0810(const USBH_HandleTypeDef *phost)
{
    return (phost != NULL) && (phost->device.DevDesc.idVendor == 0x0810U) &&
           (phost->device.DevDesc.idProduct == 0x0001U);
}

uint8_t USBH_HID_GamepadUsesInterruptInput(const USBH_HandleTypeDef *phost)
{
    return USBH_HID_GamepadIs20BC(phost) || USBH_HID_GamepadIs0810(phost);
}

GamepadReportProfile USBH_HID_Gamepad20BCProfile(const USBH_HandleTypeDef *phost,
                                               uint16_t endpoint_bytes,
                                               uint16_t descriptor_bytes)
{
    if (!USBH_HID_GamepadIs20BC(phost)) {
        return GAMEPAD_REPORT_LEGACY;
    }
    if (phost->device.DevDesc.bcdDevice == 0x1003U) {
        if ((endpoint_bytes == 8U) && (descriptor_bytes == 90U)) {
            return GAMEPAD_REPORT_20BC_5500_8BYTE;
        }
        if ((endpoint_bytes == 64U) && (descriptor_bytes == 123U)) {
            return GAMEPAD_REPORT_20BC_5500_9BYTE;
        }
    }
    return GAMEPAD_REPORT_20BC_5500;
}

void USBH_HID_GamepadReset(void)
{
    uint8_t channel;
    Gamepad_ClearPublished();
    GamepadArbiter_Reset();
    gamepad_profile = GAMEPAD_REPORT_LEGACY;
    gamepad_reports_accepted = 0U;
    gamepad_reports_rejected = 0U;
    gamepad_reports_ignored = 0U;
    gamepad_reports_timed_out = 0U;
    gamepad_active_channel = GAMEPAD_ARBITER_NO_CHANNEL;
    gamepad_safety_cycles = 0U;
    gamepad_safety_timeout_cycles = 0U;
    gamepad_safety_clock_valid = 0U;
    for (channel = 0U; channel < 2U; ++channel) {
        gamepad_report_valid[channel] = 0U;
        gamepad_last_report_timer[channel] = 0U;
        gamepad_last_report_cycles[channel] = 0U;
        gamepad_channel_accepted[channel] = 0U;
        gamepad_channel_rejected[channel] = 0U;
        gamepad_channel_raw_buttons[channel] = 0U;
        USBH_memset((void *)&gamepad_channel_info[channel], 0,
                    sizeof(gamepad_channel_info[channel]));
        USBH_memset((void *)gamepad_channel_raw[channel], 0, 8U);
    }
}

void USBH_HID_GamepadCheckTimeout(const USBH_HandleTypeDef *phost)
{
    uint8_t channel;
    if ((phost == NULL) || !Gamepad_UsesArbiter()) {
        return;
    }
    if (!Gamepad_TransportReady(phost) || !gamepad_safety_clock_valid) {
        Gamepad_InvalidateTransport(phost);
        return;
    }
    for (channel = 0U; channel < 2U; ++channel) {
        if (gamepad_report_valid[channel] &&
            ((uint32_t)(phost->Timer - gamepad_last_report_timer[channel]) >=
             GAMEPAD_PERIODIC_TIMEOUT_MS)) {
            gamepad_report_valid[channel] = 0U;
            gamepad_reports_timed_out++;
            GamepadArbiter_Invalidate(channel, phost->Timer);
        }
    }
    GamepadArbiter_Tick(phost->Timer);
    gamepad_active_channel = GamepadArbiter_GetActiveChannel();
    if (gamepad_active_channel == GAMEPAD_ARBITER_NO_CHANNEL) {
        Gamepad_ClearPublished();
    }
}

USBH_StatusTypeDef USBH_HID_GamepadInitChannel(USBH_HandleTypeDef *phost,
                                             HID_HandleTypeDef *hid, uint8_t channel)
{
    if ((phost == NULL) || (hid == NULL) || (channel > 1U) ||
        (hid->length == 0U) || (hid->length > GAMEPAD_USB_RX_BYTES)) {
        return USBH_FAIL;
    }
    if (channel == 0U) {
        if (USBH_HID_GamepadIs0810(phost)) {
            gamepad_profile = GAMEPAD_REPORT_0810_0001;
        } else {
            gamepad_profile = USBH_HID_Gamepad20BCProfile(phost, hid->length,
                                                         hid->HID_Desc.wItemLength);
        }
    } else if (((gamepad_profile != GAMEPAD_REPORT_20BC_5500_8BYTE) &&
                (gamepad_profile != GAMEPAD_REPORT_20BC_5500_9BYTE)) ||
               (USBH_HID_Gamepad20BCProfile(phost, hid->length,
                  hid->HID_Desc.wItemLength) != gamepad_profile)) {
        return USBH_FAIL;
    }
    USBH_memset(gamepad_rx_buffer[channel], 0, sizeof(gamepad_rx_buffer[channel]));
    hid->pData = (uint8_t *)(void *)gamepad_rx_buffer[channel];
    return USBH_OK;
}

USBH_StatusTypeDef USBH_HID_GamepadInit(USBH_HandleTypeDef *phost)
{
    HID_HandleTypeDef *hid;
    USBH_HID_GamepadReset();
    if ((phost == NULL) || (phost->pActiveClass == NULL)) {
        return USBH_FAIL;
    }
    hid = (HID_HandleTypeDef *)phost->pActiveClass->pData;
    return USBH_HID_GamepadInitChannel(phost, hid, 0U);
}

static void Gamepad_Reject(uint8_t channel, uint32_t now)
{
    gamepad_reports_rejected++;
    gamepad_channel_rejected[channel]++;
    gamepad_report_valid[channel] = 0U;
    if (Gamepad_UsesArbiter()) {
        GamepadArbiter_Invalidate(channel, now);
        gamepad_active_channel = GamepadArbiter_GetActiveChannel();
        if (gamepad_active_channel == GAMEPAD_ARBITER_NO_CHANNEL) {
            Gamepad_ClearPublished();
        }
    } else {
        Gamepad_ClearPublished();
    }
}

void USBH_HID_GamepadReceiveChannel(USBH_HandleTypeDef *phost,
                                   HID_HandleTypeDef *hid, uint8_t channel,
                                   uint32_t length)
{
    HID_GAMEPAD_Info_TypeDef decoded;
    uint16_t raw_buttons;
    uint8_t byte;
    if ((phost == NULL) || (channel > 1U)) {
        return;
    }
    if (!Gamepad_TransportReady(phost) ||
        (Gamepad_UsesArbiter() && !gamepad_safety_clock_valid)) {
        Gamepad_InvalidateTransport(phost);
        return;
    }
    /* This PC identity multiplexes two report IDs on one endpoint. Unknown
       IDs have no controller identity and cannot invalidate another owner. */
    if (gamepad_profile == GAMEPAD_REPORT_0810_0001) {
        if ((hid == NULL) || (hid->pData == NULL) || (length == 0U) ||
            ((hid->pData[0] != 1U) && (hid->pData[0] != 2U))) {
            gamepad_reports_rejected++;
            return;
        }
        channel = (uint8_t)(hid->pData[0] - 1U);
    }
    if ((hid == NULL) || (hid->pData == NULL) || (length > hid->length) ||
        (length > GAMEPAD_USB_RX_BYTES) ||
        !Gamepad_DecodeReport(gamepad_profile, hid->pData, (uint16_t)length, &decoded)) {
        Gamepad_Reject(channel, phost->Timer);
        return;
    }
    if (gamepad_profile == GAMEPAD_REPORT_0810_0001) {
        raw_buttons = (uint16_t)(((uint16_t)hid->pData[5] >> 4) |
                                 ((uint16_t)hid->pData[6] << 4));
    } else if ((gamepad_profile == GAMEPAD_REPORT_20BC_5500_8BYTE) ||
               (gamepad_profile == GAMEPAD_REPORT_20BC_5500_9BYTE)) {
        raw_buttons = (uint16_t)(((uint16_t)hid->pData[0] |
                                  ((uint16_t)hid->pData[1] << 8)) & 0x7FFFU);
    } else {
        raw_buttons = (gamepad_profile == GAMEPAD_REPORT_20BC_5500) ?
                      ((uint16_t)hid->pData[0] | ((uint16_t)hid->pData[1] << 8)) :
                      decoded.buttons;
    }
    gamepad_channel_info[channel] = decoded;
    gamepad_channel_raw_buttons[channel] = raw_buttons;
    for (byte = 0U; byte < 8U; ++byte) {
        gamepad_channel_raw[channel][byte] = (byte < length) ? hid->pData[byte] : 0U;
    }
    gamepad_last_report_timer[channel] = phost->Timer;
    gamepad_last_report_cycles[channel] = gamepad_safety_cycles;
    gamepad_report_valid[channel] = 1U;
    gamepad_reports_accepted++;
    gamepad_channel_accepted[channel]++;
    if (Gamepad_UsesArbiter()) {
        GamepadArbiter_Update(channel, &decoded, raw_buttons, phost->Timer);
        gamepad_active_channel = GamepadArbiter_GetActiveChannel();
        if (gamepad_active_channel == GAMEPAD_ARBITER_NO_CHANNEL) {
            Gamepad_ClearPublished();
        }
    } else {
        gamepad_info = decoded;
        gamepad_raw_buttons = raw_buttons;
    }
}

void USBH_HID_GamepadReceive(USBH_HandleTypeDef *phost, uint32_t length)
{
    HID_HandleTypeDef *hid = ((phost != NULL) && (phost->pActiveClass != NULL)) ?
        (HID_HandleTypeDef *)phost->pActiveClass->pData : NULL;
    USBH_HID_GamepadReceiveChannel(phost, hid, 0U, length);
}

HID_GAMEPAD_Info_TypeDef *USBH_HID_GetGamepadInfo(USBH_HandleTypeDef *phost)
{
    if (!Gamepad_TransportReady(phost) ||
        (Gamepad_UsesArbiter() && !gamepad_safety_clock_valid)) {
        Gamepad_InvalidateTransport(phost);
        return NULL;
    }
    if (Gamepad_UsesArbiter()) {
        uint8_t active = GamepadArbiter_GetActiveChannel();
        gamepad_active_channel = active;
        if ((active >= 2U) || !GamepadArbiter_GetOutput(&gamepad_info)) {
            Gamepad_ClearPublished();
            return NULL;
        }
        gamepad_raw_buttons = gamepad_channel_raw_buttons[active];
    } else if (!gamepad_report_valid[0]) {
        return NULL;
    }
    return &gamepad_info;
}
