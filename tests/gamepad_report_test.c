#include "gamepad_report.h"
#include <stdio.h>
#include <string.h>

static int failures;
static unsigned int checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static const uint8_t captured_neutral[9] = {
    0x00U, 0x00U, 0x0FU, 0x80U, 0x80U, 0x80U, 0x80U, 0x00U, 0x00U
};

static void check_rejected(GamepadReportProfile profile, const uint8_t *data,
                           uint16_t length)
{
    HID_GAMEPAD_Info_TypeDef output;
    unsigned char before[sizeof(output)];
    memset(&output, 0xA5, sizeof(output));
    memcpy(before, &output, sizeof(output));
    CHECK(Gamepad_DecodeReport(profile, data, length, &output) == 0);
    CHECK(memcmp(before, &output, sizeof(output)) == 0);
}

static void test_captured_neutral(void)
{
    HID_GAMEPAD_Info_TypeDef output;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, captured_neutral,
                              sizeof(captured_neutral), &output) == 1);
    CHECK(output.buttons == 0U && output.hat == 0U);
    CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
}

static void test_axes(void)
{
    static const uint8_t inputs[] = {0U, 1U, 127U, 128U, 129U, 254U, 255U};
    static const int x_expected[] = {-127, -127, -1, 0, 1, 126, 127};
    static const int y_expected[] = {127, 127, 1, 0, -1, -126, -127};
    uint8_t packet[9];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int axis;
    unsigned int sample;

    for (axis = 0U; axis < 4U; ++axis) {
        for (sample = 0U; sample < sizeof(inputs); ++sample) {
            int values[4];
            unsigned int other;
            memcpy(packet, captured_neutral, sizeof(packet));
            packet[axis + 3U] = inputs[sample];
            CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet,
                                      sizeof(packet), &output) == 1);
            values[0] = output.lx;
            values[1] = output.ly;
            values[2] = output.rx;
            values[3] = output.ry;
            CHECK(values[axis] == ((axis == 1U || axis == 3U) ?
                                  y_expected[sample] : x_expected[sample]));
            for (other = 0U; other < 4U; ++other) {
                if (other != axis) {
                    CHECK(values[other] == 0);
                }
            }
        }
    }
}

static void test_hat(void)
{
    static const uint8_t expected[8] = {1U, 3U, 2U, 6U, 4U, 12U, 8U, 9U};
    uint8_t packet[9];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int hat;
    memcpy(packet, captured_neutral, sizeof(packet));
    for (hat = 0U; hat < 8U; ++hat) {
        packet[2] = (uint8_t)(0xA0U | hat);
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet,
                                  sizeof(packet), &output) == 1);
        CHECK(output.hat == expected[hat]);
    }
    packet[2] = 0xFFU;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet,
                              sizeof(packet), &output) == 1);
    CHECK(output.hat == GAMEPAD_HAT_CENTERED);
    for (hat = 8U; hat < 16U; ++hat) {
        packet[2] = (uint8_t)hat;
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet,
                                  sizeof(packet), &output) == 1);
        CHECK(output.hat == GAMEPAD_HAT_CENTERED);
    }
}

static void test_buttons(void)
{
    /* Independent expected project masks for each one-based USB Usage. */
    static const uint16_t expected[16] = {
        0U, 0U, 0U, 0U, 0x4000U, 0x8000U, 0x0001U, 0x0002U,
        0x0004U, 0x0008U, 0x0020U, 0x0040U, 0U, 0U, 0U, 0U
    };
    uint8_t packet[9];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int bit;
    for (bit = 0U; bit < 16U; ++bit) {
        uint16_t mask = (uint16_t)(1U << bit);
        memcpy(packet, captured_neutral, sizeof(packet));
        packet[0] = (uint8_t)mask;
        packet[1] = (uint8_t)(mask >> 8);
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet,
                                  sizeof(packet), &output) == 1);
        CHECK(output.buttons == expected[bit]);
        CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    }
    packet[0] = 0xFFU;
    packet[1] = 0xFFU;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet,
                              sizeof(packet), &output) == 1);
    CHECK(output.buttons == 0xC06FU);
    CHECK((output.buttons & (GAMEPAD_BUTTON_MASK_CROSS | GAMEPAD_BUTTON_MASK_CIRCLE |
                            GAMEPAD_BUTTON_MASK_SQUARE | GAMEPAD_BUTTON_MASK_TRIANGLE)) == 0U);

    memcpy(packet, captured_neutral, sizeof(packet));
    packet[7] = 0xFFU;
    packet[8] = 0xA5U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet,
                              sizeof(packet), &output) == 1);
    CHECK(output.buttons == 0U && output.hat == 0U);
    CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
}

static void test_legacy(void)
{
    uint8_t packet[33] = {0x01U, 0x00U, 0xFFU, 0xFFU, 0x00U, 0xF5U, 0x12U, 0x34U};
    HID_GAMEPAD_Info_TypeDef output;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_LEGACY, packet, 8U, &output) == 1);
    CHECK(output.buttons == 0x1234U && output.hat == 0x0AU);
    CHECK(output.lx == -128 && output.ly == -128 && output.rx == 127 && output.ry == 127);
    packet[1] = 128U;
    packet[2] = 128U;
    packet[3] = 128U;
    packet[4] = 128U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_LEGACY, packet, 32U, &output) == 1);
    CHECK(output.lx == 0 && output.ly == -1 && output.rx == 0 && output.ry == -1);
    packet[20] = 0x02U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_LEGACY, packet, 20U, &output) == 1);
    check_rejected(GAMEPAD_REPORT_LEGACY, packet, 21U);
    check_rejected(GAMEPAD_REPORT_LEGACY, packet, 32U);
    packet[20] = 0x01U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_LEGACY, packet, 21U, &output) == 1);
    check_rejected(GAMEPAD_REPORT_LEGACY, packet, 7U);
    check_rejected(GAMEPAD_REPORT_LEGACY, packet, 33U);
}

static void test_rejections(void)
{
    uint8_t packet[10] = {0U};
    unsigned int length;
    memcpy(packet, captured_neutral, sizeof(captured_neutral));
    for (length = 0U; length < 9U; ++length) {
        check_rejected(GAMEPAD_REPORT_20BC_5500, packet, (uint16_t)length);
    }
    check_rejected(GAMEPAD_REPORT_20BC_5500, packet, 10U);
    check_rejected(GAMEPAD_REPORT_20BC_5500, NULL, 9U);
    check_rejected(GAMEPAD_REPORT_LEGACY, NULL, 8U);
    check_rejected((GamepadReportProfile)99, packet, 9U);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet, 9U, NULL) == 0);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_LEGACY, packet, 8U, NULL) == 0);
}

int main(void)
{
    test_captured_neutral();
    test_axes();
    test_hat();
    test_buttons();
    test_legacy();
    test_rejections();
    if (failures != 0) {
        fprintf(stderr, "%d failures in %u checks\n", failures, checks);
        return 1;
    }
    printf("PASS: %u gamepad report checks\n", checks);
    return 0;
}
