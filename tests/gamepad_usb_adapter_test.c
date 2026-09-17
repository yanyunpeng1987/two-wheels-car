#include "usbh_hid_gamepad.h"
#include <stdio.h>
#include <string.h>

/* Production event callback owns this pointer; this test is its API caller. */
HID_GAMEPAD_Info_TypeDef *info;

static int failures;
static unsigned int checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

typedef struct {
    USBH_HandleTypeDef host;
    USBH_ClassTypeDef usb_class;
    HID_HandleTypeDef hid;
} TestHost;

static const uint8_t neutral_report[9] = {
    0x00U, 0x00U, 0x0FU, 0x80U, 0x80U, 0x80U, 0x80U, 0x00U, 0x00U
};

static void setup_host(TestHost *test, uint16_t vid, uint16_t pid, uint16_t endpoint_size)
{
    memset(test, 0, sizeof(*test));
    test->host.device.DevDesc.idVendor = vid;
    test->host.device.DevDesc.idProduct = pid;
    test->host.device.DevDesc.bcdDevice = 0x1003U;
    test->host.pActiveClass = &test->usb_class;
    test->host.gState = HOST_CLASS;
    test->usb_class.pData = &test->hid;
    test->hid.length = endpoint_size;
}

static void queue_neutral(TestHost *test)
{
    /* Tail bytes deliberately differ: endpoint size must not become report size. */
    memset(test->hid.pData, 0xA5, test->hid.length);
    memcpy(test->hid.pData, neutral_report, sizeof(neutral_report));
    USBH_HID_GamepadReceive(&test->host, sizeof(neutral_report));
    info = USBH_HID_GetGamepadInfo(&test->host);
}

static void check_no_input(TestHost *test)
{
    CHECK(info == NULL);
    CHECK(USBH_HID_GetGamepadInfo(&test->host) == NULL);
    CHECK(gamepad_raw_buttons == 0U);
}

static void test_new_endpoint_and_publication(void)
{
    TestHost test;
    unsigned int byte;
    setup_host(&test, 0x20BCU, 0x5500U, 64U);
    CHECK(USBH_HID_GamepadIs20BC(&test.host) != 0U);
    CHECK(USBH_HID_GamepadIs20BC(NULL) == 0U);
    CHECK(USBH_HID_GetGamepadInfo(NULL) == NULL);
    CHECK(USBH_HID_GamepadInit(&test.host) == USBH_OK);
    CHECK(test.hid.length == 64U);
    CHECK(test.hid.pData != NULL);
    CHECK(((uintptr_t)test.hid.pData % sizeof(uint32_t)) == 0U);
    for (byte = 0U; byte < 64U; ++byte) {
        CHECK(test.hid.pData[byte] == 0U);
    }
    check_no_input(&test);

    test.host.gState = HOST_CLASS_REQUEST;
    queue_neutral(&test);
    CHECK(info == NULL);
    CHECK(gamepad_reports_accepted == 1U && gamepad_reports_rejected == 0U);
    test.host.gState = HOST_CLASS;
    info = USBH_HID_GetGamepadInfo(&test.host);
    CHECK(info != NULL);
    if (info != NULL) {
        CHECK(info->buttons == 0U && info->hat == 0U);
        CHECK(info->lx == 0 && info->ly == 0 && info->rx == 0 && info->ry == 0);
    }

    /* Usage 10 (Start) and explicit left/up/right/down axis movement. */
    test.hid.pData[0] = 0x00U;
    test.hid.pData[1] = 0x02U;
    test.hid.pData[3] = 0U;
    test.hid.pData[4] = 0U;
    test.hid.pData[5] = 255U;
    test.hid.pData[6] = 255U;
    USBH_HID_GamepadReceive(&test.host, 9U);
    info = USBH_HID_GetGamepadInfo(&test.host);
    CHECK(info != NULL);
    if (info != NULL) {
        CHECK(info->buttons == GAMEPAD_BUTTON_MASK_START);
        CHECK(info->lx == -127 && info->ly == 127 && info->rx == 127 && info->ry == -127);
    }
    CHECK(gamepad_raw_buttons == 0x0200U);
    CHECK(gamepad_reports_accepted == 2U && gamepad_reports_rejected == 0U);
}

static void test_invalid_reports_clear_input(void)
{
    static const uint32_t bad_lengths[] = {0U, 8U, 10U, 64U, 65U, 65545U};
    TestHost test;
    unsigned int index;
    setup_host(&test, 0x20BCU, 0x5500U, 64U);
    CHECK(USBH_HID_GamepadInit(&test.host) == USBH_OK);
    for (index = 0U; index < sizeof(bad_lengths) / sizeof(bad_lengths[0]); ++index) {
        uint32_t accepted;
        uint32_t rejected;
        queue_neutral(&test);
        CHECK(info != NULL);
        accepted = gamepad_reports_accepted;
        rejected = gamepad_reports_rejected;
        USBH_HID_GamepadReceive(&test.host, bad_lengths[index]);
        check_no_input(&test);
        CHECK(gamepad_reports_accepted == accepted);
        CHECK(gamepad_reports_rejected == rejected + 1U);
    }

    /* A smaller endpoint capacity is checked before payload decoding. */
    test.hid.length = 8U;
    USBH_HID_GamepadReceive(&test.host, 9U);
    check_no_input(&test);
}

static void test_legacy_receiver(void)
{
    static const uint16_t product_ids[] = {0x0526U, 0x0575U};
    static const uint8_t report[8] = {1U, 0U, 255U, 255U, 0U, 0xF5U, 0x12U, 0x34U};
    TestHost test;
    unsigned int product;
    for (product = 0U; product < sizeof(product_ids) / sizeof(product_ids[0]); ++product) {
        setup_host(&test, 0x2563U, product_ids[product], 32U);
        CHECK(USBH_HID_GamepadIs20BC(&test.host) == 0U);
        CHECK(USBH_HID_GamepadInit(&test.host) == USBH_OK);
        memcpy(test.hid.pData, report, sizeof(report));
        USBH_HID_GamepadReceive(&test.host, 32U);
        info = USBH_HID_GetGamepadInfo(&test.host);
        CHECK(info != NULL);
        if (info != NULL) {
            CHECK(info->buttons == 0x1234U && info->hat == 0x0AU);
            CHECK(info->lx == -128 && info->ly == -128 && info->rx == 127 && info->ry == 127);
        }
        CHECK(gamepad_raw_buttons == 0x1234U);

        test.hid.pData[20] = 2U;
        USBH_HID_GamepadReceive(&test.host, 32U);
        check_no_input(&test);
        CHECK(gamepad_reports_accepted == 1U && gamepad_reports_rejected == 1U);
        USBH_HID_GamepadReceive(&test.host, 20U);
        info = USBH_HID_GetGamepadInfo(&test.host);
        CHECK(info != NULL);
        CHECK(gamepad_reports_accepted == 2U && gamepad_reports_rejected == 1U);
        USBH_HID_GamepadReceive(&test.host, 7U);
        check_no_input(&test);
        CHECK(gamepad_reports_rejected == 2U);
    }
}

static void test_disconnect_and_reconnect(void)
{
    TestHost test;
    unsigned int byte;
    setup_host(&test, 0x20BCU, 0x5500U, 64U);
    CHECK(USBH_HID_GamepadInit(&test.host) == USBH_OK);
    queue_neutral(&test);
    CHECK(info != NULL);
    USBH_HID_GamepadReceive(&test.host, 8U);
    queue_neutral(&test);
    CHECK(gamepad_reports_accepted == 2U && gamepad_reports_rejected == 1U);
    USBH_HID_GamepadReset();
    check_no_input(&test);
    CHECK(gamepad_reports_accepted == 0U && gamepad_reports_rejected == 0U);
    CHECK(USBH_HID_GamepadInit(&test.host) == USBH_OK);
    check_no_input(&test);
    for (byte = 0U; byte < 64U; ++byte) {
        CHECK(test.hid.pData[byte] == 0U);
    }
    queue_neutral(&test);
    CHECK(info != NULL);
    CHECK(gamepad_reports_accepted == 1U && gamepad_reports_rejected == 0U);
}

static void test_invalid_storage(void)
{
    TestHost test;
    setup_host(&test, 0x20BCU, 0x5500U, 0U);
    CHECK(USBH_HID_GamepadInit(&test.host) == USBH_FAIL);
    check_no_input(&test);
    test.hid.length = 65U;
    CHECK(USBH_HID_GamepadInit(&test.host) == USBH_FAIL);
    test.usb_class.pData = NULL;
    CHECK(USBH_HID_GamepadInit(&test.host) == USBH_FAIL);
    USBH_HID_GamepadReceive(&test.host, 9U);
    CHECK(gamepad_reports_rejected == 1U);
    check_no_input(&test);

    test.usb_class.pData = &test.hid;
    test.hid.length = 64U;
    CHECK(USBH_HID_GamepadInit(&test.host) == USBH_OK);
    test.hid.pData = NULL;
    USBH_HID_GamepadReceive(&test.host, 9U);
    CHECK(gamepad_reports_rejected == 1U);
    check_no_input(&test);
}

int main(void)
{
    test_new_endpoint_and_publication();
    test_invalid_reports_clear_input();
    test_legacy_receiver();
    test_disconnect_and_reconnect();
    test_invalid_storage();
    if (failures != 0) {
        fprintf(stderr, "%d failures in %u checks\n", failures, checks);
        return 1;
    }
    printf("PASS: %u gamepad USB adapter checks\n", checks);
    return 0;
}
