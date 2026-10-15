#include "usbh_hid_gamepad.h"
#include "usbh_hiwonder_hid.h"
#include <stdio.h>
#include <string.h>

/* Execute the production class and event callback against the actual ST types.
 * USB transport/control dependencies below only record requests and deliver
 * packets; they contain no gamepad decoding or class state machine. */
static unsigned int checks;
static unsigned int failures;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

typedef struct {
    unsigned int find_index_calls;
    unsigned int find_class_calls;
    uint8_t requested_number;
    uint8_t requested_alt;
    uint8_t selected_index;
    unsigned int open_calls;
    unsigned int close_calls;
    unsigned int close_by_channel[3];
    unsigned int free_calls;
    unsigned int free_by_channel[3];
    uint8_t opened_ep;
    uint16_t opened_size;
    uint8_t opened_type;
    unsigned int toggle_calls;
    unsigned int descriptor_calls;
    uint16_t descriptor_length;
    unsigned int idle_calls;
    unsigned int protocol_calls;
    unsigned int get_report_calls;
    uint8_t get_report_length;
    unsigned int active_events;
    unsigned int interrupt_calls;
    unsigned int interrupt_calls_by_channel[3];
    uint8_t *receive_buffer;
    uint8_t receive_length;
    uint8_t receive_pipe;
    USBH_URBStateTypeDef urb;
    uint32_t actual_length;
    unsigned int fifo_calls;
    USBH_StatusTypeDef descriptor_status;
    USBH_StatusTypeDef idle_status;
    USBH_StatusTypeDef protocol_status;
    USBH_StatusTypeDef report_status;
    USBH_StatusTypeDef open_status;
    uint8_t allocated_pipe;
    uint8_t fail_alloc_ep;
    uint8_t fail_open_ep;
    uint8_t dual;
    uint8_t *buffers[3];
    uint8_t lengths[3];
    USBH_URBStateTypeDef urbs[3];
    uint32_t actuals[3];
    unsigned int descriptors[2];
    unsigned int idles[2];
    uint16_t idle_values[2];
    uint16_t desc_lengths[2];
    uint8_t cleared_ep;
    unsigned int clear_calls;
    USBH_StatusTypeDef clear_status;
} Transport;

/* Auxiliary IF2 service counters; this interface never supplies vehicle input. */
extern volatile uint8_t gamepad_usb_aux_active;
extern volatile uint32_t gamepad_usb_aux_reports;
extern volatile uint32_t gamepad_usb_aux_submits;
extern volatile uint32_t gamepad_usb_aux_notready;
extern volatile uint32_t gamepad_usb_aux_stalls;
extern volatile uint32_t gamepad_usb_aux_errors;
extern volatile uint32_t gamepad_usb_aux_last_length;

static Transport transport;
static const uint8_t selected_report[8] = {
    1U, 0xC0U, 0x80U, 0x80U, 0x00U, 0x0FU, 0x22U, 0U
};
static const uint8_t neutral_0810[8] = {1U, 128U, 128U, 128U, 128U, 15U, 0U, 0U};
static const uint8_t official_report[9] = {
    0U, 0U, 0x0FU, 0x80U, 0x00U, 0x80U, 0x80U, 0U, 0U
};

uint8_t USBH_FindInterfaceIndex(USBH_HandleTypeDef *host, uint8_t number, uint8_t alt)
{
    uint8_t index;
    ++transport.find_index_calls;
    transport.requested_number = number;
    transport.requested_alt = alt;
    for (index = 0U; index < host->device.CfgDesc.bNumInterfaces; ++index) {
        USBH_InterfaceDescTypeDef *itf = &host->device.CfgDesc.Itf_Desc[index];
        if (itf->bInterfaceNumber == number && itf->bAlternateSetting == alt) {
            return index;
        }
    }
    return 0xFFU;
}

uint8_t USBH_FindInterface(USBH_HandleTypeDef *host, uint8_t class_code,
                         uint8_t subclass, uint8_t protocol)
{
    uint8_t index;
    ++transport.find_class_calls;
    for (index = 0U; index < host->device.CfgDesc.bNumInterfaces; ++index) {
        USBH_InterfaceDescTypeDef *itf = &host->device.CfgDesc.Itf_Desc[index];
        if (itf->bInterfaceClass == class_code && itf->bInterfaceSubClass == subclass &&
            (protocol == 0xFFU || itf->bInterfaceProtocol == protocol)) {
            return index;
        }
    }
    return 0xFFU;
}

USBH_StatusTypeDef USBH_SelectInterface(USBH_HandleTypeDef *host, uint8_t index)
{
    transport.selected_index = index;
    host->device.current_interface = index;
    return USBH_OK;
}

uint8_t USBH_AllocPipe(USBH_HandleTypeDef *host, uint8_t ep)
{
    (void)host;
    CHECK(ep == 0x81U || ep == 0x82U || ep == 0x83U);
    if (ep == transport.fail_alloc_ep || transport.allocated_pipe == 0xFFU) {
        return 0xFFU;
    }
    return (uint8_t)(transport.allocated_pipe + ep - 0x81U);
}

USBH_StatusTypeDef USBH_OpenPipe(USBH_HandleTypeDef *host, uint8_t pipe,
                               uint8_t ep, uint8_t address, uint8_t speed,
                               uint8_t type, uint16_t size)
{
    (void)host;
    CHECK(pipe >= transport.allocated_pipe && pipe <= transport.allocated_pipe + 2U);
    CHECK(address == 3U && speed == USBH_SPEED_FULL);
    ++transport.open_calls;
    transport.opened_ep = ep;
    transport.opened_size = size;
    transport.opened_type = type;
    return (ep == transport.fail_open_ep) ? USBH_FAIL : transport.open_status;
}

USBH_StatusTypeDef USBH_ClosePipe(USBH_HandleTypeDef *host, uint8_t pipe)
{
    (void)host;
    CHECK(pipe >= transport.allocated_pipe && pipe <= transport.allocated_pipe + 2U);
    ++transport.close_calls;
    ++transport.close_by_channel[pipe - transport.allocated_pipe];
    return USBH_OK;
}

USBH_StatusTypeDef USBH_FreePipe(USBH_HandleTypeDef *host, uint8_t pipe)
{
    (void)host;
    CHECK(pipe >= transport.allocated_pipe && pipe <= transport.allocated_pipe + 2U);
    ++transport.free_calls;
    ++transport.free_by_channel[pipe - transport.allocated_pipe];
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *host, uint8_t pipe, uint8_t toggle)
{
    (void)host;
    CHECK((pipe >= transport.allocated_pipe && pipe <= transport.allocated_pipe + 2U) && toggle == 0U);
    ++transport.toggle_calls;
    return USBH_OK;
}

USBH_DescHeader_t *USBH_GetNextDesc(uint8_t *buffer, uint16_t *position)
{
    *position = (uint16_t)(*position + buffer[0]);
    return (USBH_DescHeader_t *)(void *)(buffer + buffer[0]);
}

USBH_StatusTypeDef USBH_CtlReq(USBH_HandleTypeDef *host, uint8_t *buffer, uint16_t length)
{
    uint16_t index = host->Control.setup.b.wIndex.w;
    CHECK(index < 2U);
    if (index >= 2U) { return USBH_FAIL; }
    if (host->Control.setup.b.bRequest == USB_REQ_GET_DESCRIPTOR) {
        CHECK(host->Control.setup.b.bmRequestType == (USB_D2H | USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_STANDARD));
        CHECK(host->Control.setup.b.wValue.w == 0x2200U);
        CHECK(host->Control.setup.b.wLength.w == length && buffer == host->device.Data);
        ++transport.descriptor_calls;
        ++transport.descriptors[index];
        transport.desc_lengths[index] = length;
        transport.descriptor_length = length;
        return transport.descriptor_status;
    }
    CHECK(host->Control.setup.b.bRequest == USB_HID_SET_IDLE);
    CHECK(host->Control.setup.b.bmRequestType == (USB_H2D | USB_REQ_RECIPIENT_INTERFACE | USB_REQ_TYPE_CLASS));
    transport.idle_values[index] = host->Control.setup.b.wValue.w;
    CHECK(host->Control.setup.b.wLength.w == 0U && length == 0U && buffer == NULL);
    ++transport.idle_calls;
    ++transport.idles[index];
    return transport.idle_status;
}

USBH_StatusTypeDef USBH_HID_GetHIDReportDescriptor(USBH_HandleTypeDef *host, uint16_t length)
{
    CHECK(host->device.current_interface == 0U);
    ++transport.descriptor_calls;
    transport.descriptor_length = length;
    return transport.descriptor_status;
}

USBH_StatusTypeDef USBH_HID_SetIdle(USBH_HandleTypeDef *host, uint8_t duration, uint8_t report_id)
{
    (void)host;
    CHECK(duration == 50U && report_id == 0U);
    ++transport.idles[0];
    transport.idle_values[0] = (uint16_t)(((uint16_t)duration << 8) | report_id);
    ++transport.idle_calls;
    return transport.idle_status;
}

USBH_StatusTypeDef USBH_HID_SetProtocol(USBH_HandleTypeDef *host, uint8_t protocol)
{
    (void)host;
    CHECK(protocol == 0U);
    ++transport.protocol_calls;
    return transport.protocol_status;
}

USBH_StatusTypeDef USBH_HID_GetReport(USBH_HandleTypeDef *host, uint8_t type,
                                  uint8_t report_id, uint8_t *buffer, uint8_t length)
{
    (void)host;
    CHECK(type == 1U && report_id == 0U && buffer != NULL);
    ++transport.get_report_calls;
    transport.get_report_length = length;
    return transport.report_status;
}

USBH_StatusTypeDef USBH_InterruptReceiveData(USBH_HandleTypeDef *host, uint8_t *buffer,
                                          uint8_t length, uint8_t pipe)
{
    (void)host;
    ++transport.interrupt_calls;
    ++transport.interrupt_calls_by_channel[pipe - transport.allocated_pipe];
    transport.receive_buffer = buffer;
    transport.receive_length = length;
    transport.receive_pipe = pipe;
    transport.buffers[pipe - transport.allocated_pipe] = buffer;
    transport.lengths[pipe - transport.allocated_pipe] = length;
    transport.urbs[pipe - transport.allocated_pipe] = USBH_URB_IDLE;
    transport.urb = USBH_URB_IDLE;
    return USBH_OK;
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *host, uint8_t pipe)
{
    (void)host;
    CHECK(pipe >= transport.allocated_pipe && pipe <= transport.allocated_pipe + 2U);
    return transport.dual ? transport.urbs[pipe - transport.allocated_pipe] : transport.urb;
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *host, uint8_t pipe)
{
    (void)host;
    CHECK(pipe >= transport.allocated_pipe && pipe <= transport.allocated_pipe + 2U);
    return transport.dual ? transport.actuals[pipe - transport.allocated_pipe] : transport.actual_length;
}

USBH_StatusTypeDef USBH_ClrFeature(USBH_HandleTypeDef *host, uint8_t ep)
{
    (void)host;
    CHECK(ep == 0x81U || ep == 0x82U);
    transport.cleared_ep = ep;
    ++transport.clear_calls;
    return transport.clear_status;
}

USBH_StatusTypeDef USBH_HID_KeybdInit(USBH_HandleTypeDef *host)
{
    (void)host;
    return USBH_OK;
}

USBH_StatusTypeDef USBH_HID_MouseInit(USBH_HandleTypeDef *host)
{
    (void)host;
    return USBH_OK;
}

uint16_t USBH_HID_FifoWrite(FIFO_TypeDef *fifo, void *buffer, uint16_t length)
{
    (void)fifo;
    (void)buffer;
    ++transport.fifo_calls;
    return length;
}

/* Mirror the production USB entry point's DWT safety checks around processing. */
static USBH_StatusTypeDef run_background(USBH_HandleTypeDef *host)
{
    USBH_StatusTypeDef status;
    USBH_HID_GamepadSafetyPoll(host, host->Timer * 84000U, 84000U);
    status = HIWONDER_HID_CLASS.BgndProcess(host);
    USBH_HID_GamepadSafetyPoll(host, host->Timer * 84000U, 84000U);
    return status;
}

static void user_event(USBH_HandleTypeDef *host, uint8_t event)
{
    (void)host;
    CHECK(event == HOST_USER_CLASS_ACTIVE);
    ++transport.active_events;
}

static HID_HandleTypeDef *hid_handle(USBH_HandleTypeDef *host)
{
    return (HID_HandleTypeDef *)host->pActiveClass->pData;
}

static void setup_host(USBH_HandleTypeDef *host, uint16_t vid, uint16_t pid,
                       uint16_t packet_size, uint8_t interval, uint16_t descriptor_length)
{
    USBH_InterfaceDescTypeDef *itf;
    const uint8_t raw_config[] = {
        9U, 2U, 34U, 0U, 1U, 1U, 0U, 0x80U, 50U,
        9U, 4U, 0U, 0U, 1U, 3U, 0U, 0U, 0U,
        9U, 0x21U, 0x10U, 1U, 0U, 1U, 0x22U, 0U, 0U,
        7U, 5U, 0x81U, 3U, 0U, 0U, 0U
    };
    CHECK(HIWONDER_HID_CLASS.pData == NULL);
    memset(host, 0, sizeof(*host));
    memset(&transport, 0, sizeof(transport));
    transport.allocated_pipe = 1U;
    transport.selected_index = 0xFFU;
    host->pActiveClass = &HIWONDER_HID_CLASS;
    host->pUser = user_event;
    host->gState = HOST_CLASS;
    host->device.is_connected = 1U;
    host->device.is_disconnected = 0U;
    host->device.PortEnabled = 1U;
    host->device.address = 3U;
    host->device.speed = USBH_SPEED_FULL;
    host->device.DevDesc.idVendor = vid;
    host->device.DevDesc.idProduct = pid;
    host->device.CfgDesc.bNumInterfaces = 1U;
    memcpy(host->device.CfgDesc_Raw, raw_config, sizeof(raw_config));
    host->device.CfgDesc_Raw[25] = (uint8_t)descriptor_length;
    host->device.CfgDesc_Raw[26] = (uint8_t)(descriptor_length >> 8);
    itf = &host->device.CfgDesc.Itf_Desc[0];
    itf->bInterfaceNumber = 0U;
    itf->bAlternateSetting = 0U;
    itf->bInterfaceClass = USB_HID_CLASS;
    itf->bNumEndpoints = 1U;
    itf->Ep_Desc[0].bEndpointAddress = 0x81U;
    itf->Ep_Desc[0].bmAttributes = USB_EP_TYPE_INTR;
    itf->Ep_Desc[0].wMaxPacketSize = packet_size;
    itf->Ep_Desc[0].bInterval = interval;
}

static void check_empty(USBH_HandleTypeDef *host)
{
    CHECK(info == NULL);
    CHECK(USBH_HID_GetGamepadInfo(host) == NULL);
    CHECK(gamepad_raw_buttons == 0U);
}

static void class_requests(USBH_HandleTypeDef *host, uint16_t expected_length,
                           unsigned int expected_protocol_calls, uint16_t expected_idle_value)
{
    unsigned int step;
    USBH_StatusTypeDef status = USBH_BUSY;
    for (step = 0U; step < 8U && status == USBH_BUSY; ++step) {
        status = HIWONDER_HID_CLASS.Requests(host);
    }
    CHECK(status == USBH_OK);
    CHECK(hid_handle(host)->ctl_state == USBH_HID_REQ_IDLE);
    CHECK(transport.descriptor_calls == 1U);
    CHECK(transport.descriptor_length == expected_length);
    CHECK(transport.idle_calls == 1U);
    CHECK(transport.idles[0] == 1U && transport.idles[1] == 0U);
    CHECK(transport.idle_values[0] == expected_idle_value);
    CHECK(transport.protocol_calls == expected_protocol_calls);
    CHECK(transport.active_events == 1U);
}

static void begin_input(USBH_HandleTypeDef *host, unsigned int expected_get_report)
{
    HID_HandleTypeDef *hid = hid_handle(host);
    CHECK(run_background(host) == USBH_OK);
    CHECK(hid->Init == USBH_HID_GamepadInit);
    CHECK(hid->pData != NULL);
    CHECK(((uintptr_t)hid->pData % sizeof(uint32_t)) == 0U);
    if (expected_get_report != 0U) {
        CHECK(hid->state == USBH_HID_IDLE);
        CHECK(run_background(host) == USBH_OK);
    }
    CHECK(hid->state == USBH_HID_SYNC);
    CHECK(transport.get_report_calls == expected_get_report);
    host->Timer = 1U;
    CHECK(run_background(host) == USBH_OK);
    CHECK(hid->state == USBH_HID_GET_DATA);
    CHECK(run_background(host) == USBH_OK);
    CHECK(hid->state == USBH_HID_POLL);
    CHECK(transport.receive_buffer == hid->pData);
    CHECK(transport.receive_length == hid->length);
    CHECK(transport.receive_pipe == hid->InPipe);
}

static void complete_packet(USBH_HandleTypeDef *host, const uint8_t *packet,
                            uint32_t actual_length, uint32_t time)
{
    HID_HandleTypeDef *hid = hid_handle(host);
    uint32_t copy_length = actual_length;
    host->Timer = time;
    if (hid->DataReady != 0U) {
        CHECK(HIWONDER_HID_CLASS.SOFProcess(host) == USBH_OK);
        CHECK(hid->state == USBH_HID_GET_DATA);
        CHECK(run_background(host) == USBH_OK);
    }
    CHECK(hid->state == USBH_HID_POLL);
    if (copy_length > transport.receive_length) {
        copy_length = transport.receive_length;
    }
    if (packet != NULL) {
        memcpy(transport.receive_buffer, packet, copy_length);
    }
    transport.actual_length = actual_length;
    transport.urb = USBH_URB_DONE;
    CHECK(run_background(host) == USBH_OK);
    CHECK(hid->DataReady == 1U);
}

static void disconnect_host(USBH_HandleTypeDef *host)
{
    CHECK(HIWONDER_HID_CLASS.DeInit(host) == USBH_OK);
    CHECK(host->pActiveClass->pData == NULL);
    CHECK(transport.close_calls == 1U && transport.free_calls == 1U);
    check_empty(host);
    CHECK(gamepad_reports_accepted == 0U && gamepad_reports_rejected == 0U);
    CHECK(gamepad_reports_ignored == 0U && gamepad_reports_timed_out == 0U);
    CHECK(HIWONDER_HID_CLASS.DeInit(host) == USBH_OK);
    CHECK(transport.close_calls == 1U && transport.free_calls == 1U);
}







static void test_existing_profiles(void)
{
    USBH_HandleTypeDef host;
    uint8_t legacy_report[32] = {0U, 128U, 0U, 128U, 127U, 15U, 0x40U, 0U};
    setup_host(&host, 0x20BCU, 0x5500U, 64U, 10U, 148U);
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(hid_handle(&host)->length == 64U && hid_handle(&host)->poll == 10U);
    CHECK(transport.opened_size == 64U);
    class_requests(&host, 148U, 0U, 0x3200U);
    begin_input(&host, 0U);
    complete_packet(&host, official_report, 9U, 100U);
    CHECK(info != NULL && info->ly == 127);
    CHECK(gamepad_reports_accepted == 1U);
    host.Timer = 10000U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(info != NULL && gamepad_reports_timed_out == 0U);
    CHECK(run_background(&host) == USBH_OK);
    complete_packet(&host, official_report, 8U, 10010U);
    check_empty(&host);
    CHECK(gamepad_reports_rejected == 1U); /* Nine USB bytes, not 64 or eight. */
    disconnect_host(&host);

    setup_host(&host, 0x1234U, 0x5678U, 32U, 1U, 64U);
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(hid_handle(&host)->length == 32U && hid_handle(&host)->poll == 50U);
    CHECK(transport.find_class_calls == 1U && transport.find_index_calls == 0U);
    class_requests(&host, 64U, 1U, 0x3200U);
    begin_input(&host, 1U);
    CHECK(transport.get_report_length == 32U);
    complete_packet(&host, legacy_report, sizeof(legacy_report), 100U);
    CHECK(info != NULL && info->ly == 127 && info->buttons == GAMEPAD_BUTTON_MASK_L1);
    CHECK(gamepad_reports_accepted == 1U);
    disconnect_host(&host);
}

static void test_initialization_failures(void)
{
    USBH_HandleTypeDef host;
    USBH_InterfaceDescTypeDef *itf;
    setup_host(&host, 0x0810U, 1U, 8U, 2U, 202U);
    itf = &host.device.CfgDesc.Itf_Desc[0];
    itf->bInterfaceNumber = 1U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.open_calls == 0U);
    itf->bInterfaceNumber = 0U;
    itf->bInterfaceProtocol = HID_KEYBRD_BOOT_CODE;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    itf->bInterfaceProtocol = 0U;
    itf->bInterfaceClass = 0xFFU;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    itf->bInterfaceClass = USB_HID_CLASS;
    itf->bInterfaceSubClass = HID_BOOT_CODE;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    itf->bInterfaceSubClass = 0U;
    itf->Ep_Desc[0].bEndpointAddress = 1U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    itf->Ep_Desc[0].bEndpointAddress = 0x81U;
    itf->Ep_Desc[0].bmAttributes = USB_EP_TYPE_BULK;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    itf->Ep_Desc[0].bmAttributes = USB_EP_TYPE_INTR;
    itf->Ep_Desc[0].wMaxPacketSize = 0U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    itf->Ep_Desc[0].wMaxPacketSize = 65U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.open_calls == 0U);
    itf->Ep_Desc[0].wMaxPacketSize = 8U;
    transport.allocated_pipe = 0xFFU;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.free_calls == 0U);
    transport.allocated_pipe = 1U;
    transport.open_status = USBH_FAIL;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.free_calls == 1U);
    CHECK(transport.close_calls == 0U);
}

static void test_request_retry_and_stall(void)
{
    USBH_HandleTypeDef host;
    setup_host(&host, 0x0810U, 1U, 8U, 2U, 202U);
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_BUSY);
    transport.descriptor_status = USBH_BUSY;
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_BUSY);
    CHECK(hid_handle(&host)->ctl_state == USBH_HID_REQ_GET_REPORT_DESC);
    transport.descriptor_status = USBH_OK;
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_BUSY);
    transport.idle_status = USBH_NOT_SUPPORTED;
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_BUSY);
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_OK);
    CHECK(transport.protocol_calls == 0U && transport.active_events == 1U);
    CHECK(transport.idles[0] == 1U && transport.idle_values[0] == 0x3200U);
    disconnect_host(&host);

    setup_host(&host, 0x1234U, 0x5678U, 32U, 1U, 64U);
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_BUSY);
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_BUSY);
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_BUSY);
    transport.protocol_status = USBH_NOT_SUPPORTED;
    CHECK(HIWONDER_HID_CLASS.Requests(&host) == USBH_FAIL);
    CHECK(transport.protocol_calls == 1U && transport.active_events == 0U);
    disconnect_host(&host);
}

static void setup_20bc8_class(USBH_HandleTypeDef *host)
{
    setup_host(host, 0x20BCU, 0x5500U, 8U, 5U, 90U);
    host->device.DevDesc.bcdDevice = 0x1003U;
}



static void test_20bc8_class_profile_boundaries(void)
{
    static const struct {
        uint16_t bcd;
        uint16_t endpoint;
        uint16_t descriptor;
    } old_cases[] = {
        {0x1002U, 8U, 90U}, {0x1004U, 8U, 90U},
        {0x1003U, 8U, 89U}, {0x1003U, 8U, 91U},
        {0x1003U, 9U, 90U}, {0x1003U, 64U, 90U}
    };
    static const uint8_t neutral[8] = {0U, 0U, 15U, 128U, 128U, 128U, 128U, 0U};
    USBH_HandleTypeDef host;
    unsigned int index;
    for (index = 0U; index < sizeof(old_cases) / sizeof(old_cases[0]); ++index) {
        setup_host(&host, 0x20BCU, 0x5500U, old_cases[index].endpoint,
                   5U, old_cases[index].descriptor);
        host.device.DevDesc.bcdDevice = old_cases[index].bcd;
        CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
        class_requests(&host, old_cases[index].descriptor, 0U, 0x3200U);
        begin_input(&host, 0U);
        complete_packet(&host, neutral, 8U, 100U);
        check_empty(&host);
        CHECK(gamepad_reports_rejected == 1U && gamepad_reports_accepted == 0U);
        if (old_cases[index].endpoint >= 9U) {
            complete_packet(&host, official_report, 9U, 105U);
            CHECK(info != NULL && info->ly == 127);
            host.Timer = 10000U;
            CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
            CHECK(info != NULL && gamepad_reports_timed_out == 0U);
        }
        disconnect_host(&host);
    }
    /* A valid interface 1 must not rescue an invalid selected interface 0. */
    setup_20bc8_class(&host);
    host.device.CfgDesc.bNumInterfaces = 2U;
    host.device.CfgDesc.Itf_Desc[1] = host.device.CfgDesc.Itf_Desc[0];
    host.device.CfgDesc.Itf_Desc[1].bInterfaceNumber = 1U;
    host.device.CfgDesc.Itf_Desc[1].Ep_Desc[0].bEndpointAddress = 0x82U;
    host.device.CfgDesc.Itf_Desc[0].Ep_Desc[0].bmAttributes = USB_EP_TYPE_BULK;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.open_calls == 0U);
}

static const uint8_t neutral8[8] = {0U,0U,15U,128U,128U,128U,128U,0U};
static const uint8_t forward8[8] = {0U,0U,15U,128U,0U,128U,128U,0U};
static const uint8_t right8[8] = {0U,0U,15U,128U,128U,128U,255U,0U};

static void setup_dual(USBH_HandleTypeDef *host)
{
    setup_20bc8_class(host);
    host->device.CfgDesc.bNumInterfaces = 2U;
    host->device.CfgDesc.Itf_Desc[1] = host->device.CfgDesc.Itf_Desc[0];
    host->device.CfgDesc.Itf_Desc[1].bInterfaceNumber = 1U;
    host->device.CfgDesc.Itf_Desc[1].Ep_Desc[0].bEndpointAddress = 0x82U;
    memcpy(host->device.CfgDesc_Raw + 34U, host->device.CfgDesc_Raw + 9U, 25U);
    host->device.CfgDesc_Raw[2] = 59U;
    host->device.CfgDesc_Raw[4] = 2U;
    host->device.CfgDesc_Raw[36] = 1U;
    host->device.CfgDesc_Raw[54] = 0x82U;
    transport.dual = 1U;
}

static void requests_dual(USBH_HandleTypeDef *host)
{
    unsigned int step;
    USBH_StatusTypeDef status = USBH_BUSY;
    for (step=0U; step<16U && status==USBH_BUSY; ++step) {
        status = HIWONDER_HID_CLASS.Requests(host);
    }
    CHECK(status == USBH_OK);
    CHECK(transport.descriptors[0] == 1U && transport.descriptors[1] == 1U);
    CHECK(transport.desc_lengths[0] == 90U && transport.desc_lengths[1] == 90U);
    CHECK(transport.idles[0] == 1U && transport.idles[1] == 1U);
    CHECK(transport.idle_values[0] == 0x3200U && transport.idle_values[1] == 0x3200U);
    CHECK(transport.protocol_calls == 0U && transport.active_events == 1U);
}

static void begin_dual(USBH_HandleTypeDef *host)
{
    CHECK(run_background(host)==USBH_OK);
    host->Timer=1U;
    CHECK(run_background(host)==USBH_OK);
    CHECK(run_background(host)==USBH_OK);
    CHECK(transport.buffers[0] != NULL && transport.buffers[1] != NULL);
    CHECK(transport.buffers[0] != transport.buffers[1]);
    CHECK(transport.lengths[0] == 8U && transport.lengths[1] == 8U);
    CHECK(((uintptr_t)transport.buffers[0] % 4U)==0U);
    CHECK(((uintptr_t)transport.buffers[1] % 4U)==0U);
}

static void packet_dual(USBH_HandleTypeDef *host,uint8_t channel,const uint8_t *data,
                         uint32_t length,uint32_t now)
{
    host->Timer=now;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(host)==USBH_OK);
    CHECK(run_background(host)==USBH_OK);
    if (data!=NULL) { memcpy(transport.buffers[channel],data,length<=8U?length:8U); }
    transport.actuals[channel]=length;
    transport.urbs[channel]=USBH_URB_DONE;
    CHECK(run_background(host)==USBH_OK);
}

static void test_dual_interfaces(void)
{
    USBH_HandleTypeDef host;
    HID_GAMEPAD_Info_TypeDef saved;
    unsigned int i;
    setup_dual(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    CHECK(transport.open_calls==2U && transport.toggle_calls==2U);
    CHECK(gamepad_usb_channels==2U && gamepad_usb_secondary_unavailable==0U);
    requests_dual(&host);
    begin_dual(&host);
    packet_dual(&host,0U,neutral8,8U,10U);
    packet_dual(&host,1U,neutral8,8U,15U);
    check_empty(&host);
    packet_dual(&host,0U,forward8,8U,20U);
    CHECK(info!=NULL && info->ly==127 && info->rx==0);
    CHECK(gamepad_active_channel==0U);
    saved=*info;
    packet_dual(&host,1U,right8,8U,25U);
    CHECK(info!=NULL && memcmp(info,&saved,sizeof(saved))==0);
    CHECK(gamepad_channel_info[1].rx==127);
    /* The nonowner's bad transfer never clears the owner's output. */
    packet_dual(&host,1U,right8,7U,30U);
    CHECK(info!=NULL && info->ly==127 && gamepad_active_channel==0U);
    CHECK(gamepad_channel_rejected[1]==1U && gamepad_channel_rejected[0]==0U);
    /* Both URBs finish before one foreground pass: owner neutral must publish
       zero even when the other endpoint has a held request in the same pass. */
    host.Timer=35U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(run_background(&host)==USBH_OK);
    memcpy(transport.buffers[0],neutral8,8U);
    memcpy(transport.buffers[1],right8,8U);
    transport.actuals[0]=8U;transport.actuals[1]=8U;
    transport.urbs[0]=USBH_URB_DONE;transport.urbs[1]=USBH_URB_DONE;
    CHECK(run_background(&host)==USBH_OK);
    check_empty(&host);
    packet_dual(&host,1U,right8,8U,40U);
    packet_dual(&host,1U,right8,8U,45U);
    check_empty(&host); /* Held loser cannot acquire after the 10ms window. */
    packet_dual(&host,1U,neutral8,8U,50U);
    packet_dual(&host,1U,right8,8U,55U);
    CHECK(info!=NULL && info->rx==127 && info->ly==0 && gamepad_active_channel==1U);
    /* Channel 0 keeps reporting but cannot extend channel 1's deadline. */
    for(i=0U;i<4U;++i) { packet_dual(&host,0U,forward8,8U,155U+100U*i); }
    host.Timer=554U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(info!=NULL && gamepad_active_channel==1U);
    host.Timer=555U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    check_empty(&host);
    CHECK(gamepad_reports_timed_out>=1U);
    CHECK(gamepad_channel_accepted[0]>0U && gamepad_channel_accepted[1]>0U);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
    CHECK(transport.close_calls==2U && transport.free_calls==2U);
    CHECK(gamepad_usb_channels==0U && gamepad_reports_accepted==0U);
    setup_dual(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    requests_dual(&host);begin_dual(&host);
    packet_dual(&host,1U,right8,8U,10U);
    check_empty(&host); /* Reconnection cannot recover a held old command. */
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
}

static void test_0810_both_ids(void)
{
    USBH_HandleTypeDef host;
    uint8_t neutral2[8];
    uint8_t moving2[8];
    uint8_t unknown[8];
    setup_host(&host,0x0810U,1U,8U,2U,202U);
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    class_requests(&host,202U,0U, 0x3200U);begin_input(&host,0U);
    memcpy(neutral2,neutral_0810,8U);neutral2[0]=2U;
    memcpy(moving2,selected_report,8U);moving2[0]=2U;
    complete_packet(&host,neutral_0810,8U,10U);
    complete_packet(&host,neutral2,8U,12U);
    complete_packet(&host,moving2,8U,14U);
    CHECK(info!=NULL && info->ly==127 && info->rx==64 && gamepad_active_channel==1U);
    CHECK(gamepad_raw_buttons==0x0220U && gamepad_channel_raw_buttons[1]==0x0220U);
    complete_packet(&host,selected_report,8U,16U);
    CHECK(info!=NULL && gamepad_active_channel==1U);
    complete_packet(&host,selected_report,7U,18U);
    CHECK(info!=NULL && gamepad_active_channel==1U);
    memcpy(unknown,selected_report,8U);unknown[0]=3U;
    complete_packet(&host,unknown,8U,20U);
    CHECK(info!=NULL && gamepad_active_channel==1U);
    complete_packet(&host,neutral2,8U,22U);
    check_empty(&host);
    complete_packet(&host,selected_report,8U,24U);
    check_empty(&host);
    complete_packet(&host,neutral_0810,8U,34U);
    complete_packet(&host,selected_report,8U,36U);
    CHECK(info!=NULL && gamepad_active_channel==0U);
    CHECK(gamepad_channel_accepted[0]>0U && gamepad_channel_accepted[1]>0U);
    disconnect_host(&host);
}

static void test_dual_failures_and_stall(void)
{
    USBH_HandleTypeDef host;
    setup_dual(&host);transport.fail_alloc_ep=0x82U;
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData==NULL && transport.close_calls==1U && transport.free_calls==1U);
    setup_dual(&host);transport.fail_open_ep=0x82U;
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData==NULL && transport.close_calls==1U && transport.free_calls==2U);
    setup_dual(&host);host.device.CfgDesc_Raw[50]=89U;
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    CHECK(gamepad_usb_channels==1U && gamepad_usb_secondary_unavailable==1U);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
    setup_dual(&host);host.device.CfgDesc.Itf_Desc[1].bInterfaceProtocol=1U;
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    CHECK(gamepad_usb_channels==1U && gamepad_usb_secondary_unavailable==1U);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
    setup_dual(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    requests_dual(&host);begin_dual(&host);
    transport.urbs[1]=USBH_URB_STALL;transport.clear_status=USBH_BUSY;
    host.Timer=10U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(run_background(&host)==USBH_OK);
    CHECK(transport.cleared_ep==0x82U && transport.clear_calls==1U);
    transport.urbs[0]=USBH_URB_STALL;
    CHECK(run_background(&host)==USBH_OK);
    CHECK(transport.cleared_ep==0x82U && transport.clear_calls==2U); /* Serialized control pipe. */
    transport.clear_status=USBH_OK;
    CHECK(run_background(&host)==USBH_OK);
    CHECK(transport.cleared_ep==0x82U && transport.toggle_calls==3U);
    CHECK(run_background(&host)==USBH_OK);
    CHECK(transport.cleared_ep==0x81U && transport.toggle_calls==4U);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
}

static void test_publication_gate(void)
{
    USBH_HandleTypeDef host;
    USBH_ClassTypeDef foreign;
    static const uint8_t neutral[9] = {0U,0U,15U,128U,128U,128U,128U,0U,0U};
    setup_host(&host,0x20BCU,0x5500U,64U,5U,123U);
    host.device.DevDesc.bcdDevice = 0x1003U;
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    class_requests(&host,123U,0U,0x3200U);begin_input(&host,0U);
    complete_packet(&host,neutral,9U,5U);
    complete_packet(&host,official_report,9U,10U);
    CHECK(info!=NULL && info->ly==127);
    host.Timer=12U;host.gState=HOST_CLASS_REQUEST;
    USBH_HID_GamepadSafetyPoll(&host,host.Timer*84000U,84000U);
    USBH_HID_EventCallback(&host);check_empty(&host);
    host.gState=HOST_CLASS;
    USBH_HID_GamepadSafetyPoll(&host,host.Timer*84000U,84000U);
    USBH_HID_EventCallback(&host);check_empty(&host);
    complete_packet(&host,official_report,9U,15U);check_empty(&host);
    complete_packet(&host,neutral,9U,20U);check_empty(&host);
    complete_packet(&host,official_report,9U,25U);
    CHECK(info!=NULL && info->ly==127); /* Recovery requires neutral and a fresh action. */
    info=NULL;foreign=HIWONDER_HID_CLASS;host.pActiveClass=&foreign;
    USBH_HID_EventCallback(&host);CHECK(info==NULL);
    host.pActiveClass=&HIWONDER_HID_CLASS;hid_handle(&host)->Init=USBH_HID_KeybdInit;
    USBH_HID_EventCallback(&host);CHECK(info==NULL);
    hid_handle(&host)->Init=USBH_HID_MouseInit;USBH_HID_EventCallback(&host);CHECK(info==NULL);
    hid_handle(&host)->Init=USBH_HID_GamepadInit;USBH_HID_EventCallback(&host);CHECK(info!=NULL);
    disconnect_host(&host);
}

static void test_dual_index_order_and_raw_buttons(void)
{
    USBH_HandleTypeDef host;
    USBH_InterfaceDescTypeDef tmp;
    uint8_t start[8],face[8];
    setup_dual(&host);
    tmp=host.device.CfgDesc.Itf_Desc[0];
    host.device.CfgDesc.Itf_Desc[0]=host.device.CfgDesc.Itf_Desc[1];
    host.device.CfgDesc.Itf_Desc[1]=tmp;
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    CHECK(transport.selected_index==1U); /* Requests use interface number, not index. */
    requests_dual(&host);begin_dual(&host);
    memcpy(start,neutral8,8U);start[1]=0x82U;
    memcpy(face,neutral8,8U);face[0]=10U;
    packet_dual(&host,0U,neutral8,8U,10U);
    packet_dual(&host,1U,neutral8,8U,15U);
    packet_dual(&host,1U,start,8U,20U);
    CHECK(info!=NULL && info->buttons==GAMEPAD_BUTTON_MASK_START);
    CHECK(gamepad_raw_buttons==0x0200U && gamepad_channel_raw_buttons[1]==0x0200U);
    packet_dual(&host,0U,face,8U,25U);
    CHECK(info!=NULL && info->buttons==GAMEPAD_BUTTON_MASK_START && gamepad_raw_buttons==0x0200U);
    CHECK(gamepad_channel_raw_buttons[0]==10U && gamepad_channel_raw[0][0]==10U);
    packet_dual(&host,1U,neutral8,8U,30U);check_empty(&host);
    packet_dual(&host,0U,neutral8,8U,35U);
    packet_dual(&host,0U,forward8,8U,39U);check_empty(&host); /* 9ms cannot acquire. */
    packet_dual(&host,0U,neutral8,8U,40U);
    packet_dual(&host,0U,forward8,8U,45U);
    CHECK(info!=NULL && info->ly==127 && gamepad_active_channel==0U);
    /* A completed transfer survives SOF until the class consumes its length. */
    host.Timer=50U;CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(run_background(&host)==USBH_OK);
    memcpy(transport.buffers[1],right8,8U);transport.actuals[1]=7U;transport.urbs[1]=USBH_URB_DONE;
    host.Timer=55U;CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(run_background(&host)==USBH_OK);
    CHECK(gamepad_channel_rejected[1]==1U && info!=NULL && info->ly==127);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
    CHECK(transport.close_calls==2U && transport.free_calls==2U);
}

static const uint8_t neutral9[9] = {0U,0U,15U,128U,128U,128U,128U,0U};
static const uint8_t forward9[9] = {0U,0U,15U,128U,0U,128U,128U,0U};
static const uint8_t right9[9] = {0U,0U,15U,128U,128U,255U,128U,0U,0U};

static void setup_dual9(USBH_HandleTypeDef *host)
{
    setup_host(host, 0x20BCU, 0x5500U, 64U, 5U, 123U);
    host->device.DevDesc.bcdDevice = 0x1003U;
    host->device.CfgDesc.bNumInterfaces = 2U;
    host->device.CfgDesc.Itf_Desc[1] = host->device.CfgDesc.Itf_Desc[0];
    host->device.CfgDesc.Itf_Desc[1].bInterfaceNumber = 1U;
    host->device.CfgDesc.Itf_Desc[1].Ep_Desc[0].bEndpointAddress = 0x82U;
    memcpy(host->device.CfgDesc_Raw + 34U, host->device.CfgDesc_Raw + 9U, 25U);
    host->device.CfgDesc_Raw[2] = 59U;
    host->device.CfgDesc_Raw[4] = 2U;
    host->device.CfgDesc_Raw[36] = 1U;
    host->device.CfgDesc_Raw[54] = 0x82U;
    transport.dual = 1U;
}

static void requests_dual9(USBH_HandleTypeDef *host)
{
    unsigned int step;
    USBH_StatusTypeDef status = USBH_BUSY;
    for (step=0U; step<16U && status==USBH_BUSY; ++step) {
        status = HIWONDER_HID_CLASS.Requests(host);
    }
    CHECK(status == USBH_OK);
    CHECK(transport.descriptors[0] == 1U && transport.descriptors[1] == 1U);
    CHECK(transport.desc_lengths[0] == 123U && transport.desc_lengths[1] == 123U);
    CHECK(transport.idles[0] == 1U && transport.idles[1] == 1U);
    CHECK(transport.idle_values[0] == 0x3200U && transport.idle_values[1] == 0x3200U);
    CHECK(transport.protocol_calls == 0U && transport.active_events == 1U);
}

static void begin_dual9(USBH_HandleTypeDef *host)
{
    CHECK(run_background(host)==USBH_OK);
    host->Timer=1U;
    CHECK(run_background(host)==USBH_OK);
    CHECK(run_background(host)==USBH_OK);
    CHECK(transport.buffers[0] != NULL && transport.buffers[1] != NULL);
    CHECK(transport.buffers[0] != transport.buffers[1]);
    CHECK(transport.lengths[0] == 64U && transport.lengths[1] == 64U);
    CHECK(((uintptr_t)transport.buffers[0] % 4U)==0U);
    CHECK(((uintptr_t)transport.buffers[1] % 4U)==0U);
}

static void packet_dual9(USBH_HandleTypeDef *host,uint8_t channel,const uint8_t *data,
                         uint32_t length,uint32_t now)
{
    host->Timer=now;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(host)==USBH_OK);
    CHECK(run_background(host)==USBH_OK);
    if (data!=NULL) { memcpy(transport.buffers[channel],data,length<=9U?length:9U); }
    transport.actuals[channel]=length;
    transport.urbs[channel]=USBH_URB_DONE;
    CHECK(run_background(host)==USBH_OK);
}

static void test_dual9_interfaces(void)
{
    USBH_HandleTypeDef host;
    HID_GAMEPAD_Info_TypeDef saved;
    unsigned int i;
    setup_dual9(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    CHECK(transport.open_calls==2U && transport.toggle_calls==2U);
    CHECK(gamepad_usb_channels==2U && gamepad_usb_secondary_unavailable==0U);
    requests_dual9(&host);
    begin_dual9(&host);
    packet_dual9(&host,0U,neutral9,9U,10U);
    packet_dual9(&host,1U,neutral9,9U,15U);
    check_empty(&host);
    packet_dual9(&host,0U,forward9,9U,20U);
    CHECK(info!=NULL && info->ly==127 && info->rx==0);
    CHECK(gamepad_active_channel==0U);
    saved=*info;
    packet_dual9(&host,1U,right9,9U,25U);
    CHECK(info!=NULL && memcmp(info,&saved,sizeof(saved))==0);
    CHECK(gamepad_channel_info[1].rx==127);
    /* The nonowner's bad transfer never clears the owner's output. */
    packet_dual9(&host,1U,right9,7U,30U);
    CHECK(info!=NULL && info->ly==127 && gamepad_active_channel==0U);
    CHECK(gamepad_channel_rejected[1]==1U && gamepad_channel_rejected[0]==0U);
    /* Both URBs finish before one foreground pass: owner neutral must publish
       zero even when the other endpoint has a held request in the same pass. */
    host.Timer=35U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(run_background(&host)==USBH_OK);
    memcpy(transport.buffers[0],neutral9,9U);
    memcpy(transport.buffers[1],right9,9U);
    transport.actuals[0]=9U;transport.actuals[1]=9U;
    transport.urbs[0]=USBH_URB_DONE;transport.urbs[1]=USBH_URB_DONE;
    CHECK(run_background(&host)==USBH_OK);
    check_empty(&host);
    packet_dual9(&host,1U,right9,9U,40U);
    packet_dual9(&host,1U,right9,9U,45U);
    check_empty(&host); /* Held loser cannot acquire after the 10ms window. */
    packet_dual9(&host,1U,neutral9,9U,50U);
    packet_dual9(&host,1U,right9,9U,55U);
    CHECK(info!=NULL && info->rx==127 && info->ly==0 && gamepad_active_channel==1U);
    /* Channel 0 keeps reporting but cannot extend channel 1's deadline. */
    for(i=0U;i<4U;++i) { packet_dual9(&host,0U,forward9,9U,155U+100U*i); }
    host.Timer=554U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(info!=NULL && gamepad_active_channel==1U);
    host.Timer=555U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    check_empty(&host);
    CHECK(gamepad_reports_timed_out>=1U);
    CHECK(gamepad_channel_accepted[0]>0U && gamepad_channel_accepted[1]>0U);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
    CHECK(transport.close_calls==2U && transport.free_calls==2U);
    CHECK(gamepad_usb_channels==0U && gamepad_reports_accepted==0U);
    setup_dual9(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    requests_dual9(&host);begin_dual9(&host);
    packet_dual9(&host,1U,right9,9U,10U);
    check_empty(&host); /* Reconnection cannot recover a held old command. */
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
}

static void test_dual9_index_order_and_raw_buttons(void)
{
    USBH_HandleTypeDef host;
    USBH_InterfaceDescTypeDef tmp;
    uint8_t start[9],face[9];
    setup_dual9(&host);
    tmp=host.device.CfgDesc.Itf_Desc[0];
    host.device.CfgDesc.Itf_Desc[0]=host.device.CfgDesc.Itf_Desc[1];
    host.device.CfgDesc.Itf_Desc[1]=tmp;
    CHECK(HIWONDER_HID_CLASS.Init(&host)==USBH_OK);
    CHECK(transport.selected_index==1U); /* Requests use interface number, not index. */
    requests_dual9(&host);begin_dual9(&host);
    memcpy(start,neutral9,9U);start[1]=0x88U;
    memcpy(face,neutral9,9U);face[0]=10U;
    packet_dual9(&host,0U,neutral9,9U,10U);
    packet_dual9(&host,1U,neutral9,9U,15U);
    packet_dual9(&host,1U,start,9U,20U);
    CHECK(info!=NULL && info->buttons==GAMEPAD_BUTTON_MASK_START);
    CHECK(gamepad_raw_buttons==0x0800U && gamepad_channel_raw_buttons[1]==0x0800U);
    packet_dual9(&host,0U,face,9U,25U);
    CHECK(info!=NULL && info->buttons==GAMEPAD_BUTTON_MASK_START && gamepad_raw_buttons==0x0800U);
    CHECK(gamepad_channel_raw_buttons[0]==10U && gamepad_channel_raw[0][0]==10U);
    packet_dual9(&host,1U,neutral9,9U,30U);check_empty(&host);
    packet_dual9(&host,0U,neutral9,9U,35U);
    packet_dual9(&host,0U,forward9,9U,39U);check_empty(&host); /* 9ms cannot acquire. */
    packet_dual9(&host,0U,neutral9,9U,40U);
    packet_dual9(&host,0U,forward9,9U,45U);
    CHECK(info!=NULL && info->ly==127 && gamepad_active_channel==0U);
    /* A completed transfer survives SOF until the class consumes its length. */
    host.Timer=50U;CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(run_background(&host)==USBH_OK);
    memcpy(transport.buffers[1],right9,9U);transport.actuals[1]=7U;transport.urbs[1]=USBH_URB_DONE;
    host.Timer=55U;CHECK(HIWONDER_HID_CLASS.SOFProcess(&host)==USBH_OK);
    CHECK(run_background(&host)==USBH_OK);
    CHECK(gamepad_channel_rejected[1]==1U && info!=NULL && info->ly==127);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host)==USBH_OK);
    CHECK(transport.close_calls==2U && transport.free_calls==2U);
}

static void test_dual9_exact_profile_and_secondary_fallback(void)
{
    static const struct { uint16_t bcd, endpoint, descriptor; } old_cases[] = {
        {0x1002U, 64U, 123U}, {0x1004U, 64U, 123U},
        {0x1003U, 64U, 122U}, {0x1003U, 64U, 124U},
        {0x1003U, 63U, 123U}, {0x1003U, 9U, 123U},
        {0x1003U, 64U, 95U}, {0x1003U, 64U, 90U}
    };
    USBH_HandleTypeDef host;
    unsigned int index;
    for (index = 0U; index < sizeof(old_cases) / sizeof(old_cases[0]); ++index) {
        setup_dual9(&host);
        host.device.DevDesc.bcdDevice = old_cases[index].bcd;
        host.device.CfgDesc.Itf_Desc[0].Ep_Desc[0].wMaxPacketSize = old_cases[index].endpoint;
        host.device.CfgDesc_Raw[25] = (uint8_t)old_cases[index].descriptor;
        CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
        CHECK(gamepad_usb_channels == 1U && transport.open_calls == 1U);
        class_requests(&host, old_cases[index].descriptor, 0U, 0x3200U);
        begin_input(&host, 0U);
        packet_dual9(&host, 0U, forward9, 9U, 10U);
        CHECK(info != NULL && info->ly == 127); /* Old profile1 has no neutral gate. */
        host.Timer = 10000U;
        CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
        CHECK(info != NULL && gamepad_reports_timed_out == 0U);
        disconnect_host(&host);
    }
    for (index = 0U; index < 6U; ++index) {
        setup_dual9(&host);
        switch (index) {
        case 0U: host.device.CfgDesc_Raw[50] = 122U; break;
        case 1U: host.device.CfgDesc.Itf_Desc[1].Ep_Desc[0].wMaxPacketSize = 63U; break;
        case 2U: host.device.CfgDesc.Itf_Desc[1].bInterfaceProtocol = 1U; break;
        case 3U: host.device.CfgDesc.Itf_Desc[1].Ep_Desc[0].bmAttributes = USB_EP_TYPE_BULK; break;
        case 4U:
            host.device.CfgDesc.Itf_Desc[1].Ep_Desc[0].wMaxPacketSize = 8U;
            host.device.CfgDesc_Raw[50] = 90U; /* Valid old8 cannot share new9 schema. */
            break;
        default: host.device.CfgDesc.bNumInterfaces = 1U; break;
        }
        CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
        CHECK(gamepad_usb_channels == 1U && gamepad_usb_secondary_unavailable == 1U);
        CHECK(transport.open_calls == 1U);
        class_requests(&host, 123U, 0U, 0x3200U);
        begin_input(&host, 0U);
        packet_dual9(&host, 0U, forward9, 9U, 10U); check_empty(&host);
        packet_dual9(&host, 0U, neutral9, 9U, 15U);
        packet_dual9(&host, 0U, forward9, 9U, 20U);
        CHECK(info != NULL && info->ly == 127 && gamepad_active_channel == 0U);
        disconnect_host(&host);
    }
    setup_dual9(&host); transport.fail_alloc_ep = 0x82U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.close_calls == 1U && transport.free_calls == 1U);
    setup_dual9(&host); transport.fail_open_ep = 0x82U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.close_calls == 1U && transport.free_calls == 2U);
}

static void test_dual9_actual_lengths_and_padding(void)
{
    USBH_HandleTypeDef host;
    uint8_t padding[9];
    unsigned int index, byte;
    static const uint32_t bad_lengths[] = {0U, 8U, 10U, 63U, 64U, 65U, 65545U, UINT32_MAX};
    setup_dual9(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    requests_dual9(&host); begin_dual9(&host);
    for (byte = 0U; byte < 64U; ++byte) {
        CHECK(transport.buffers[0][byte] == 0U && transport.buffers[1][byte] == 0U);
    }
    memset(transport.buffers[0], 0xA5, 64U);
    memset(transport.buffers[1], 0x5A, 64U);
    memcpy(padding, neutral9, 9U); padding[1] = 0x80U;
    packet_dual9(&host, 0U, padding, 9U, 10U);
    packet_dual9(&host, 1U, neutral9, 9U, 15U);
    CHECK(gamepad_channel_raw_buttons[0] == 0U);
    packet_dual9(&host, 0U, forward9, 9U, 20U);
    CHECK(info != NULL && info->ly == 127);
    for (index = 0U; index < sizeof(bad_lengths) / sizeof(bad_lengths[0]); ++index) {
        packet_dual9(&host, 1U, right9, bad_lengths[index], 25U + 5U * index);
        CHECK(info != NULL && info->ly == 127 && gamepad_active_channel == 0U);
        CHECK(gamepad_channel_rejected[1] == index + 1U);
    }
    CHECK(gamepad_channel_rejected[0] == 0U);
    for (byte = 9U; byte < 64U; ++byte) {
        CHECK(transport.buffers[0][byte] == 0xA5U && transport.buffers[1][byte] == 0x5AU);
    }
    packet_dual9(&host, 0U, forward9, 64U, 70U); check_empty(&host);
    CHECK(gamepad_channel_rejected[0] == 1U);
    packet_dual9(&host, 0U, forward9, 9U, 80U); check_empty(&host);
    packet_dual9(&host, 0U, padding, 9U, 85U);
    packet_dual9(&host, 0U, forward9, 9U, 90U);
    CHECK(info != NULL && info->ly == 127);
    packet_dual9(&host, 0U, padding, 9U, 95U); check_empty(&host);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host) == USBH_OK);
    CHECK(transport.close_calls == 2U && transport.free_calls == 2U);
}

static void test_dual9_ten_ms_and_three_raw_interfaces(void)
{
    USBH_HandleTypeDef host;
    unsigned int tick;
    setup_dual9(&host);
    /* New receiver metadata: bInterval10 and three raw interfaces/84 bytes.
       ST stores only the first two parsed interfaces. Third HID details are
       synthetic: its system collection must never open a control channel. */
    host.device.CfgDesc.Itf_Desc[0].Ep_Desc[0].bInterval = 10U;
    host.device.CfgDesc.Itf_Desc[1].Ep_Desc[0].bInterval = 10U;
    host.device.CfgDesc_Raw[33] = 10U;
    host.device.CfgDesc_Raw[58] = 10U;
    memcpy(host.device.CfgDesc_Raw + 59U, host.device.CfgDesc_Raw + 34U, 25U);
    host.device.CfgDesc_Raw[61] = 2U;
    host.device.CfgDesc_Raw[79] = 0x83U;
    host.device.CfgDesc_Raw[2] = 84U;
    host.device.CfgDesc_Raw[4] = 3U;
    host.device.CfgDesc.wTotalLength = 84U;
    host.device.CfgDesc.bNumInterfaces = 3U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(gamepad_usb_channels == 2U && transport.open_calls == 2U);
    CHECK(hid_handle(&host)->poll == 10U);
    requests_dual9(&host); begin_dual9(&host);
    CHECK(transport.interrupt_calls == 2U);
    CHECK(transport.interrupt_calls_by_channel[0] == 1U);
    CHECK(transport.interrupt_calls_by_channel[1] == 1U);
    host.Timer = 2U;
    memcpy(transport.buffers[0], neutral9, 9U);
    memcpy(transport.buffers[1], neutral9, 9U);
    transport.actuals[0] = 9U; transport.actuals[1] = 9U;
    transport.urbs[0] = USBH_URB_DONE; transport.urbs[1] = USBH_URB_DONE;
    CHECK(run_background(&host) == USBH_OK);
    CHECK(gamepad_channel_accepted[0] == 1U && gamepad_channel_accepted[1] == 1U);
    for (tick = 3U; tick <= 10U; ++tick) {
        host.Timer = tick;
        CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
        CHECK(run_background(&host) == USBH_OK);
        CHECK(transport.interrupt_calls == 2U);
    }
    host.Timer = 11U; /* Exactly 10ms since both submissions at Timer1. */
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(transport.interrupt_calls == 4U);
    CHECK(transport.interrupt_calls_by_channel[0] == 2U);
    CHECK(transport.interrupt_calls_by_channel[1] == 2U);
    CHECK(transport.lengths[0] == 64U && transport.lengths[1] == 64U);
    host.Timer = 20U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(transport.interrupt_calls == 4U);
    host.Timer = 21U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(transport.interrupt_calls_by_channel[0] == 3U);
    CHECK(transport.interrupt_calls_by_channel[1] == 3U);
    CHECK(transport.descriptor_calls == 2U && transport.idle_calls == 2U);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host) == USBH_OK);
    CHECK(transport.close_calls == 2U && transport.free_calls == 2U);
}

static void setup_dual9_aux(USBH_HandleTypeDef *host)
{
    setup_dual9(host);
    host->device.CfgDesc.Itf_Desc[0].Ep_Desc[0].bInterval = 10U;
    host->device.CfgDesc.Itf_Desc[1].Ep_Desc[0].bInterval = 10U;
    host->device.CfgDesc_Raw[30] = 3U;
    host->device.CfgDesc_Raw[31] = 64U;
    host->device.CfgDesc_Raw[33] = 10U;
    host->device.CfgDesc_Raw[55] = 3U;
    host->device.CfgDesc_Raw[56] = 64U;
    host->device.CfgDesc_Raw[58] = 10U;
    memcpy(host->device.CfgDesc_Raw + 59U, host->device.CfgDesc_Raw + 34U, 25U);
    host->device.CfgDesc_Raw[61] = 2U;
    host->device.CfgDesc_Raw[75] = 101U;
    host->device.CfgDesc_Raw[79] = 0x83U;
    host->device.CfgDesc_Raw[2] = 84U;
    host->device.CfgDesc_Raw[4] = 3U;
    host->device.CfgDesc.wTotalLength = 84U;
    host->device.CfgDesc.bNumInterfaces = 3U;
}

static void begin_aux(USBH_HandleTypeDef *host)
{
    begin_dual9(host);
    CHECK(transport.buffers[2] != NULL);
    CHECK(transport.buffers[2] != transport.buffers[0] && transport.buffers[2] != transport.buffers[1]);
    CHECK((uintptr_t)transport.buffers[2] % 4U == 0U);
    CHECK(transport.lengths[2] == 64U);
    CHECK(transport.interrupt_calls_by_channel[2] == 1U);
}

static void complete_aux(USBH_HandleTypeDef *host, const uint8_t *data,
                           uint32_t length, uint32_t now)
{
    host->Timer = now;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(host) == USBH_OK);
    CHECK(run_background(host) == USBH_OK);
    memcpy(transport.buffers[2], data, length < 64U ? length : 64U);
    transport.actuals[2] = length;
    transport.urbs[2] = USBH_URB_DONE;
    CHECK(run_background(host) == USBH_OK);
}

static void test_aux_discard_ownership_and_timeout(void)
{
    USBH_HandleTypeDef host;
    uint8_t fake_motion[64] = {0U};
    uint32_t accepted, rejected, timed_out;
    unsigned int index, submissions;
    static const uint32_t lengths[] = {0U, 8U, 9U, 10U, 64U, 65U, UINT32_MAX};
    setup_dual9_aux(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(gamepad_usb_channels == 2U && transport.open_calls == 3U);
    CHECK(gamepad_usb_aux_active == 1U);
    CHECK(gamepad_usb_secondary_unavailable == 0U);
    requests_dual9(&host); begin_aux(&host);
    memcpy(fake_motion, right9, 9U);
    fake_motion[1] = 0x08U; /* A valid gamepad START+turn payload is still discarded. */
    complete_aux(&host, fake_motion, 9U, 11U);
    check_empty(&host);
    CHECK(gamepad_reports_accepted == 0U && gamepad_reports_rejected == 0U);
    packet_dual9(&host, 0U, neutral9, 9U, 21U);
    packet_dual9(&host, 1U, neutral9, 9U, 31U);
    packet_dual9(&host, 0U, forward9, 9U, 41U);
    CHECK(info != NULL && info->ly == 127 && info->rx == 0 && gamepad_active_channel == 0U);
    accepted = gamepad_reports_accepted; rejected = gamepad_reports_rejected;
    for (index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        complete_aux(&host, fake_motion, lengths[index], 51U + 10U * index);
        CHECK(info != NULL && info->ly == 127 && info->rx == 0 && info->buttons == 0U);
        CHECK(gamepad_active_channel == 0U && gamepad_raw_buttons == 0U);
        CHECK(gamepad_reports_accepted == accepted && gamepad_reports_rejected == rejected);
        CHECK(gamepad_channel_accepted[0] == 2U && gamepad_channel_accepted[1] == 1U);
        CHECK(gamepad_usb_aux_reports == index + 2U);
        CHECK(gamepad_usb_aux_last_length == lengths[index]);
    }
    complete_aux(&host, fake_motion, 9U, 530U);
    timed_out = gamepad_reports_timed_out;
    CHECK(info != NULL && info->ly == 127);
    host.Timer = 540U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(info != NULL && info->ly == 127); /* 499ms since actual owner frame. */
    complete_aux(&host, fake_motion, 9U, 541U);
    check_empty(&host);
    CHECK(gamepad_reports_timed_out >= timed_out + 1U);
    CHECK(gamepad_reports_accepted == accepted && gamepad_reports_rejected == rejected);
    CHECK(transport.descriptor_calls == 2U && transport.idle_calls == 2U);
    CHECK(transport.protocol_calls == 0U && transport.get_report_calls == 0U);
    CHECK(transport.clear_calls == 0U);
    /* NAK belongs to the next submitted token, not the completed DONE token. */
    host.Timer = 551U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    submissions = transport.interrupt_calls_by_channel[2];
    transport.urbs[2] = USBH_URB_NOTREADY;
    host.Timer = 552U;
    CHECK(run_background(&host) == USBH_OK);
    host.Timer = 560U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(transport.interrupt_calls_by_channel[2] == submissions);
    host.Timer = 561U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(transport.interrupt_calls_by_channel[2] == submissions + 1U);
    CHECK(gamepad_usb_aux_notready > 0U);
    CHECK(gamepad_usb_aux_submits == transport.interrupt_calls_by_channel[2]);
    CHECK(transport.clear_calls == 0U);
    transport.urbs[2] = USBH_URB_STALL;
    CHECK(run_background(&host) == USBH_OK);
    submissions = transport.interrupt_calls_by_channel[2];
    CHECK(gamepad_usb_aux_active == 0U && gamepad_usb_aux_stalls == 1U);
    host.Timer = 1000U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(transport.interrupt_calls_by_channel[2] == submissions);
    CHECK(transport.clear_calls == 0U && transport.descriptor_calls == 2U && transport.idle_calls == 2U);
    /* Stale DONE or additional elapsed time cannot revive the stalled service. */
    accepted = gamepad_usb_aux_reports;
    transport.urbs[2] = USBH_URB_DONE;
    transport.actuals[2] = 9U;
    memcpy(transport.buffers[2], fake_motion, 9U);
    host.Timer = 1010U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(gamepad_usb_aux_active == 0U && gamepad_usb_aux_reports == accepted);
    CHECK(transport.interrupt_calls_by_channel[2] == submissions && transport.clear_calls == 0U);
    CHECK(gamepad_reports_accepted == 3U && gamepad_reports_rejected == 0U);
    check_empty(&host);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host) == USBH_OK);
    CHECK(transport.close_calls == 3U && transport.free_calls == 3U);
    CHECK(transport.close_by_channel[2] == 1U && transport.free_by_channel[2] == 1U);
    CHECK(gamepad_usb_aux_active == 0U);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host) == USBH_OK);
    CHECK(transport.close_calls == 3U && transport.free_calls == 3U);
    /* Only a new enumeration (DeInit/Init) enables the auxiliary pipe again. */
    setup_dual9_aux(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(gamepad_usb_aux_active == 1U && gamepad_usb_aux_stalls == 0U);
    requests_dual9(&host); begin_aux(&host);
    complete_aux(&host, fake_motion, 9U, 11U);
    CHECK(gamepad_usb_aux_reports == 1U && gamepad_usb_aux_active == 1U);
    CHECK(gamepad_reports_accepted == 0U && gamepad_reports_rejected == 0U);
    check_empty(&host);
    CHECK(transport.clear_calls == 0U && transport.idle_calls == 2U && transport.descriptor_calls == 2U);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host) == USBH_OK);
    CHECK(transport.close_calls == 3U && transport.free_calls == 3U);
}

static void test_aux_exact_raw_metadata(void)
{
    static const struct { unsigned int offset; uint8_t value; } bad_raw[] = {
        {61U,3U}, {62U,1U}, {63U,0U}, {63U,2U}, {64U,0xFFU}, {65U,1U}, {66U,1U},
        {75U,100U}, {75U,102U}, {76U,1U}, {79U,0x84U}, {79U,3U},
        {80U,2U}, {81U,63U}, {81U,65U}, {82U,1U}, {83U,9U}, {83U,11U},
        {59U,1U}, {68U,8U}, {73U,2U}, {74U,0x23U}, {77U,6U}, {2U,83U}
    };
    USBH_HandleTypeDef host;
    unsigned int index;
    for (index = 0U; index < sizeof(bad_raw) / sizeof(bad_raw[0]); ++index) {
        setup_dual9_aux(&host);
        host.device.CfgDesc_Raw[bad_raw[index].offset] = bad_raw[index].value;
        CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
        CHECK(gamepad_usb_channels == 2U && transport.open_calls == 2U);
        requests_dual9(&host); begin_dual9(&host);
        CHECK(transport.buffers[2] == NULL && transport.interrupt_calls_by_channel[2] == 0U);
        CHECK(HIWONDER_HID_CLASS.DeInit(&host) == USBH_OK);
        CHECK(transport.close_calls == 2U && transport.free_calls == 2U);
    }
    /* Exact IF2 alone never enables aux for old8 or unverified old9 metadata. */
    setup_dual9_aux(&host);
    host.device.DevDesc.bcdDevice = 0x1002U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(gamepad_usb_channels == 1U && transport.open_calls == 1U);
    class_requests(&host, 123U, 0U, 0x3200U);
    begin_input(&host, 0U);
    CHECK(transport.buffers[2] == NULL);
    disconnect_host(&host);
    setup_dual9_aux(&host);
    host.device.CfgDesc.Itf_Desc[0].Ep_Desc[0].wMaxPacketSize = 8U;
    host.device.CfgDesc.Itf_Desc[1].Ep_Desc[0].wMaxPacketSize = 8U;
    host.device.CfgDesc_Raw[25] = 90U; host.device.CfgDesc_Raw[50] = 90U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(gamepad_usb_channels == 2U && transport.open_calls == 2U);
    requests_dual(&host); begin_dual(&host);
    CHECK(transport.buffers[2] == NULL);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host) == USBH_OK);
    CHECK(transport.close_calls == 2U && transport.free_calls == 2U);
}

static void test_aux_allocation_and_error_retry(void)
{
    USBH_HandleTypeDef host;
    unsigned int submissions;
    setup_dual9_aux(&host); transport.fail_alloc_ep = 0x83U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.open_calls == 2U);
    CHECK(transport.close_calls == 2U && transport.free_calls == 2U);
    CHECK(gamepad_usb_aux_active == 0U);
    setup_dual9_aux(&host); transport.fail_open_ep = 0x83U;
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_FAIL);
    CHECK(HIWONDER_HID_CLASS.pData == NULL && transport.open_calls == 3U);
    CHECK(transport.close_calls == 2U && transport.free_calls == 3U);
    CHECK(gamepad_usb_aux_active == 0U);
    setup_dual9_aux(&host);
    CHECK(HIWONDER_HID_CLASS.Init(&host) == USBH_OK);
    CHECK(gamepad_usb_aux_reports == 0U && gamepad_usb_aux_submits == 0U);
    CHECK(gamepad_usb_aux_stalls == 0U && gamepad_usb_aux_errors == 0U);
    requests_dual9(&host); begin_aux(&host);
    submissions = transport.interrupt_calls_by_channel[2];
    transport.urbs[2] = USBH_URB_ERROR;
    host.Timer = 2U;
    CHECK(run_background(&host) == USBH_OK);
    CHECK(gamepad_usb_aux_active == 1U && gamepad_usb_aux_errors > 0U);
    host.Timer = 10U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(transport.interrupt_calls_by_channel[2] == submissions);
    host.Timer = 11U;
    CHECK(HIWONDER_HID_CLASS.SOFProcess(&host) == USBH_OK);
    CHECK(run_background(&host) == USBH_OK);
    CHECK(transport.interrupt_calls_by_channel[2] == submissions + 1U);
    CHECK(transport.clear_calls == 0U && transport.idle_calls == 2U && transport.descriptor_calls == 2U);
    CHECK(gamepad_reports_accepted == 0U && gamepad_reports_rejected == 0U);
    check_empty(&host);
    CHECK(HIWONDER_HID_CLASS.DeInit(&host) == USBH_OK);
    CHECK(gamepad_usb_aux_active == 0U && transport.close_calls == 3U && transport.free_calls == 3U);
}

int main(void)
{
    test_aux_allocation_and_error_retry();
    test_aux_discard_ownership_and_timeout();
    test_aux_exact_raw_metadata();
    test_dual9_ten_ms_and_three_raw_interfaces();
    test_dual9_interfaces();
    test_dual9_index_order_and_raw_buttons();
    test_dual9_exact_profile_and_secondary_fallback();
    test_dual9_actual_lengths_and_padding();
    test_0810_both_ids();
    test_dual_interfaces();
    test_dual_index_order_and_raw_buttons();
    test_dual_failures_and_stall();
    test_publication_gate();
    test_existing_profiles();
    test_initialization_failures();
    test_request_retry_and_stall();
    test_20bc8_class_profile_boundaries();
    if (failures != 0U) {
        fprintf(stderr, "%u of %u USB class checks failed\n", failures, checks);
        return 1;
    }
    printf("gamepad USB class: %u checks passed\n", checks);
    return 0;
}
