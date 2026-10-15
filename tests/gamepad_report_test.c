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

/* Captured USB report: its leading 01 is an on-wire Report ID. */
static const uint8_t captured_0810_neutral[8] = {
    GAMEPAD_0810_SELECTED_REPORT_ID, 0x80U, 0x80U, 0x80U, 0x80U, 0x0FU, 0x00U, 0x00U
};

static void test_0810_neutral_and_axes(void)
{
    static const uint8_t offsets[4] = {3U, 4U, 1U, 2U};
    static const uint8_t inputs[7] = {0U, 1U, 127U, 128U, 129U, 254U, 255U};
    static const int x_expected[7] = {-127, -127, -1, 0, 1, 126, 127};
    static const int y_expected[7] = {127, 127, 1, 0, -1, -126, -127};
    uint8_t packet[8];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int axis;
    unsigned int sample;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, captured_0810_neutral,
                              sizeof(captured_0810_neutral), &output) == 1);
    CHECK(output.buttons == 0U && output.hat == 0U);
    CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    for (axis = 0U; axis < 4U; ++axis) {
        for (sample = 0U; sample < 7U; ++sample) {
            int axes[4];
            unsigned int other;
            memcpy(packet, captured_0810_neutral, sizeof(packet));
            packet[offsets[axis]] = inputs[sample];
            CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet,
                                      sizeof(packet), &output) == 1);
            axes[0] = output.lx;
            axes[1] = output.ly;
            axes[2] = output.rx;
            axes[3] = output.ry;
            if (axis == 2U && x_expected[sample] >= -10 && x_expected[sample] <= 10) {
                CHECK(axes[axis] == 0); /* Native turn axis is centered inside the deadzone. */
            } else {
                CHECK(axes[axis] == ((axis == 1U || axis == 3U) ?
                                    y_expected[sample] : x_expected[sample]));
            }
            for (other = 0U; other < 4U; ++other) {
                if (other != axis) {
                    CHECK(axes[other] == 0);
                }
            }
        }
    }
}

static void test_0810_hat_buttons_and_vendor(void)
{
    static const uint8_t hat_expected[16] = {1U, 3U, 2U, 6U, 4U, 12U, 8U, 9U,
                                           0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    static const uint16_t button_expected[12] = {
        0U, 0U, 0U, 0U, 0x4000U, 0x8000U, 0x0001U, 0x0002U,
        0x0004U, 0x0008U, 0x0020U, 0x0040U
    };
    uint8_t packet[8];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int index;
    for (index = 0U; index < 16U; ++index) {
        memcpy(packet, captured_0810_neutral, sizeof(packet));
        packet[5] = (uint8_t)(0xF0U | index); /* face bits cannot change the hat */
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet,
                                  sizeof(packet), &output) == 1);
        CHECK(output.hat == hat_expected[index]);
        CHECK(output.buttons == 0U);
    }
    for (index = 0U; index < 12U; ++index) {
        uint16_t raw = (uint16_t)(1U << index);
        memcpy(packet, captured_0810_neutral, sizeof(packet));
        packet[5] |= (uint8_t)((raw & 0x0FU) << 4);
        packet[6] = (uint8_t)(raw >> 4);
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet,
                                  sizeof(packet), &output) == 1);
        CHECK(output.buttons == button_expected[index]);
        CHECK(output.hat == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.ry == 0);
        CHECK(output.rx == ((raw & GAMEPAD_0810_DIGITAL_RIGHT_MASK) ? 127 :
                            ((raw & GAMEPAD_0810_DIGITAL_LEFT_MASK) ? -127 : 0)));
    }
    packet[5] = 0xFFU;
    packet[6] = 0xFFU;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet, 8U, &output) == 1);
    CHECK(output.buttons == 0xC06FU && output.hat == 0U);
    CHECK((output.buttons & (GAMEPAD_BUTTON_MASK_CROSS | GAMEPAD_BUTTON_MASK_CIRCLE |
                            GAMEPAD_BUTTON_MASK_SQUARE | GAMEPAD_BUTTON_MASK_TRIANGLE)) == 0U);
    for (index = 0U; index < 256U; ++index) {
        memcpy(packet, captured_0810_neutral, sizeof(packet));
        packet[7] = (uint8_t)index;
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet, 8U, &output) == 1);
        CHECK(output.buttons == 0U && output.hat == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    }
}

static void test_0810_rejections(void)
{
    uint8_t packet[64] = {0U};
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int length;
    memcpy(packet, captured_0810_neutral, sizeof(captured_0810_neutral));
    for (length = 0U; length <= 64U; ++length) {
        if (length != 8U) {
            check_rejected(GAMEPAD_REPORT_0810_0001, packet, (uint16_t)length);
        }
    }
    packet[0] = 0U;
    check_rejected(GAMEPAD_REPORT_0810_0001, packet, 8U);
    packet[0] = GAMEPAD_0810_OTHER_REPORT_ID;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet, 8U, &output) == 1);
    CHECK(output.buttons == 0U && output.hat == 0U);
    CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    packet[0] = 3U;
    check_rejected(GAMEPAD_REPORT_0810_0001, packet, 8U);
    check_rejected(GAMEPAD_REPORT_0810_0001, NULL, 8U);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, captured_0810_neutral,
                              8U, NULL) == 0);
    /* No Windows-only prefix is accepted, nor a report from a different profile. */
    check_rejected(GAMEPAD_REPORT_0810_0001, captured_neutral, sizeof(captured_neutral));
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, captured_0810_neutral,
                              8U, &output) == 0);
}

static void test_0810_digital_turn_and_analog_priority(void)
{
    static const uint8_t digital_fields[8] = {
        0x2FU, 0x8FU, 0x3FU, 0x6FU, 0x9FU, 0xCFU, 0xAFU, 0xFFU
    };
    static const int digital_expected[8] = {127, -127, 127, 127, -127, -127, 0, 0};
    static const uint8_t analog_inputs[6] = {117U, 118U, 128U, 138U, 139U, 255U};
    static const int analog_expected[6] = {-11, -10, 0, 10, 11, 127};
    uint8_t packet[8];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int index;
    unsigned int direction;
    CHECK(GAMEPAD_0810_DIGITAL_RIGHT_USAGE == 2U);
    CHECK(GAMEPAD_0810_DIGITAL_LEFT_USAGE == 4U);
    CHECK(GAMEPAD_0810_ANALOG_TURN_DEADZONE == 10);
    for (index = 0U; index < 8U; ++index) {
        memcpy(packet, captured_0810_neutral, sizeof(packet));
        packet[5] = digital_fields[index];
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet, 8U, &output) == 1);
        CHECK(output.rx == digital_expected[index]);
        CHECK(output.buttons == 0U); /* Shared face keys never edit PID or midpoint. */
        CHECK(output.lx == 0 && output.ly == 0 && output.ry == 0);
    }
    /* Up, down and up+down alone cannot turn. */
    for (index = 0U; index < 3U; ++index) {
        static const uint8_t vertical_fields[3] = {0x1FU, 0x4FU, 0x5FU};
        memcpy(packet, captured_0810_neutral, sizeof(packet));
        packet[5] = vertical_fields[index];
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet, 8U, &output) == 1);
        CHECK(output.rx == 0 && output.buttons == 0U);
    }
    /* Exercise -11/-10/+10/+11 against left, right, both and no digital input. */
    for (index = 0U; index < 6U; ++index) {
        for (direction = 0U; direction < 4U; ++direction) {
            static const uint8_t horizontal_fields[4] = {0x0FU, 0x2FU, 0x8FU, 0xAFU};
            static const int centered_expected[4] = {0, 127, -127, 0};
            int expected = analog_expected[index];
            memcpy(packet, captured_0810_neutral, sizeof(packet));
            packet[1] = analog_inputs[index];
            packet[5] = horizontal_fields[direction];
            packet[3] = 0U;
            packet[4] = 255U;
            packet[2] = 0U;
            if (expected >= -10 && expected <= 10) {
                expected = centered_expected[direction];
            }
            CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet, 8U, &output) == 1);
            CHECK(output.rx == expected);
            CHECK(output.lx == -127 && output.ly == -127 && output.ry == 127);
            CHECK(output.buttons == 0U);
        }
    }
    memcpy(packet, captured_0810_neutral, sizeof(packet));
    packet[0] = GAMEPAD_0810_OTHER_REPORT_ID;
    packet[5] = 0x2FU;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, packet, 8U, &output) == 1);
    CHECK(output.rx == 127 && output.buttons == 0U);
}

static void test_0810_measured_controls(void)
{
    /* User-labelled PC captures, 2026-10-08. These literal fixtures protect
       the measured physical mapping independently of the configurable masks. */
    static const struct {
        uint8_t packet[8];
        int ly;
        int turn;
        uint16_t buttons;
    } fixtures[] = {
        {{1, 128, 128, 128, 128, 0x0F, 0x00, 0},    0,    0, 0x0000},
        {{1, 128, 128, 128,   0, 0x0F, 0x00, 0},  127,    0, 0x0000},
        {{1, 128, 128, 128, 255, 0x0F, 0x00, 0}, -127,    0, 0x0000},
        {{1, 128, 128, 128, 128, 0x8F, 0x00, 0},    0, -127, 0x0000},
        {{1, 128, 128, 128, 128, 0x2F, 0x00, 0},    0,  127, 0x0000},
        {{1, 128, 128, 128, 128, 0x0F, 0x01, 0},    0,    0, 0x4000},
        {{1, 128, 128, 128, 128, 0x0F, 0x04, 0},    0,    0, 0x0001},
        {{1, 128, 128, 128, 128, 0x0F, 0x02, 0},    0,    0, 0x8000},
        {{1, 128, 128, 128, 128, 0x0F, 0x08, 0},    0,    0, 0x0002},
        {{1, 128, 128, 128, 128, 0x0F, 0x20, 0},    0,    0, 0x0008},
        {{1, 128, 128, 128, 128, 0x0F, 0x10, 0},    0,    0, 0x0004}
    };
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int index;
    for (index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]); ++index) {
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_0810_0001, fixtures[index].packet,
                                  8U, &output) == 1);
        CHECK(output.ly == fixtures[index].ly);
        CHECK(output.rx == fixtures[index].turn);
        CHECK(output.buttons == fixtures[index].buttons);
        CHECK(output.lx == 0);
        CHECK(output.ry == 0);
        CHECK(output.hat == 0U);
    }
}

/* STM32 capture: 20BC:5500/bcd1003, eight USB bytes with no Report ID. */
static const uint8_t captured_20bc_8byte_neutral[8] = {
    0x00U, 0x00U, 0x0FU, 0x80U, 0x80U, 0x80U, 0x80U, 0x00U
};

static void test_20bc_8byte_neutral_and_axes(void)
{
    static const uint8_t offsets[4] = {3U, 4U, 6U, 5U};
    static const uint8_t inputs[11] = {
        0U, 1U, 117U, 118U, 127U, 128U, 129U, 138U, 139U, 254U, 255U
    };
    static const int x_expected[11] = {
        -127, -127, -11, -10, -1, 0, 1, 10, 11, 126, 127
    };
    static const int y_expected[11] = {
        127, 127, 11, 10, 1, 0, -1, -10, -11, -126, -127
    };
    uint8_t packet[8];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int axis;
    unsigned int sample;
    CHECK(GAMEPAD_REPORT_20BC_5500_8BYTE == 3);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                              captured_20bc_8byte_neutral, 8U, &output) == 1);
    CHECK(output.buttons == 0U && output.hat == 0U);
    CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    for (axis = 0U; axis < 4U; ++axis) {
        for (sample = 0U; sample < 11U; ++sample) {
            int axes[4];
            int expected;
            unsigned int other;
            memcpy(packet, captured_20bc_8byte_neutral, sizeof(packet));
            packet[offsets[axis]] = inputs[sample];
            CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                                      packet, 8U, &output) == 1);
            axes[0] = output.lx;
            axes[1] = output.ly;
            axes[2] = output.rx;
            axes[3] = output.ry;
            expected = (axis == 1U || axis == 3U) ? y_expected[sample] : x_expected[sample];
            CHECK(axes[axis] == expected);
            CHECK(output.buttons == 0U && output.hat == 0U);
            for (other = 0U; other < 4U; ++other) {
                if (other != axis) {
                    CHECK(axes[other] == 0);
                }
            }
        }
    }
}

static void test_20bc_8byte_hat_buttons_and_vendor(void)
{
    static const uint8_t hat_expected[16] = {
        1U, 3U, 2U, 6U, 4U, 12U, 8U, 9U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    /* Literal expected masks keep the decoder's table out of the oracle. */
    static const uint16_t button_expected[16] = {
        0U, 0U, 0U, 0U, 0x4000U, 0x8000U, 0x0001U, 0x0002U,
        0x0004U, 0x0008U, 0x0020U, 0x0040U, 0U, 0U, 0U, 0U
    };
    uint8_t packet[8];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int index;
    for (index = 0U; index < 16U; ++index) {
        memcpy(packet, captured_20bc_8byte_neutral, sizeof(packet));
        packet[2] = (uint8_t)(0xA0U | index);
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                                  packet, 8U, &output) == 1);
        CHECK(output.hat == hat_expected[index]);
        CHECK(output.buttons == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    }
    /* Usages 1..15 and the constant padding bit 15 are independently checked. */
    for (index = 0U; index < 16U; ++index) {
        uint16_t raw = (uint16_t)(1U << index);
        memcpy(packet, captured_20bc_8byte_neutral, sizeof(packet));
        packet[0] = (uint8_t)raw;
        packet[1] = (uint8_t)(raw >> 8);
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                                  packet, 8U, &output) == 1);
        CHECK(output.buttons == button_expected[index]);
        CHECK(output.hat == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.ry == 0);
        CHECK(output.rx == 0);
    }
    /* All pressed, both with and without the constant padding bit. */
    for (index = 0U; index < 2U; ++index) {
        memcpy(packet, captured_20bc_8byte_neutral, sizeof(packet));
        packet[0] = 0xFFU;
        packet[1] = (index == 0U) ? 0x7FU : 0xFFU;
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                                  packet, 8U, &output) == 1);
        CHECK(output.buttons == 0xC06FU);
        CHECK(output.rx == 0 && output.hat == 0U);
        CHECK((output.buttons & (GAMEPAD_BUTTON_MASK_CROSS | GAMEPAD_BUTTON_MASK_CIRCLE |
                                GAMEPAD_BUTTON_MASK_SQUARE | GAMEPAD_BUTTON_MASK_TRIANGLE)) == 0U);
    }
    /* Byte 7 contains both unused vendor nibbles; all pairs must be inert. */
    for (index = 0U; index < 256U; ++index) {
        memcpy(packet, captured_20bc_8byte_neutral, sizeof(packet));
        packet[7] = (uint8_t)index;
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                                  packet, 8U, &output) == 1);
        CHECK(output.buttons == 0U && output.hat == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    }
}

static void test_20bc_8byte_face_keys_and_native_turn_axis(void)
{
    /* Unlike 0810, this hardware exposes a native turn axis in byte 6.
       Face usages 2/4 cannot turn, including when the native axis is centered. */
    static const uint8_t analog_inputs[9] = {
        0U, 117U, 118U, 127U, 128U, 129U, 138U, 139U, 255U
    };
    static const int analog_expected[9] = {-127, -11, -10, -1, 0, 1, 10, 11, 127};
    uint8_t packet[8];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int direction;
    unsigned int sample;
    for (direction = 0U; direction < 16U; ++direction) {
        memcpy(packet, captured_20bc_8byte_neutral, sizeof(packet));
        packet[0] = (uint8_t)direction;
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                                  packet, 8U, &output) == 1);
        CHECK(output.rx == 0);
        CHECK(output.buttons == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.ry == 0 && output.hat == 0U);
        for (sample = 0U; sample < 9U; ++sample) {
            memcpy(packet, captured_20bc_8byte_neutral, sizeof(packet));
            packet[0] = (uint8_t)direction;
            packet[1] = 0x80U; /* Padding cannot act as a button or affect the native axis. */
            packet[3] = 0U;
            packet[4] = 255U;
            packet[5] = 0U;
            packet[6] = analog_inputs[sample];
            CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                                      packet, 8U, &output) == 1);
            CHECK(output.rx == analog_expected[sample]);
            CHECK(output.lx == -127 && output.ly == -127 && output.ry == 127);
            CHECK(output.buttons == 0U && output.hat == 0U);
        }
    }
    /* Shoulder and system actions coexist with native right/left movement. */
    memcpy(packet, captured_20bc_8byte_neutral, sizeof(packet));
    packet[0] = 0xF2U;
    packet[1] = 0x0FU;
    packet[6] = 255U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                              packet, 8U, &output) == 1);
    CHECK(output.rx == 127 && output.buttons == 0xC06FU);
    packet[0] = 0xF8U;
    packet[6] = 0U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                              packet, 8U, &output) == 1);
    CHECK(output.rx == -127 && output.buttons == 0xC06FU);
}

static void test_20bc_8byte_measured_controls(void)
{
    /* User-labelled STM32 captures keep physical axes independent of the map. */
    static const struct {
        uint8_t packet[8];
        int forward;
        int turn;
    } fixtures[] = {
        {{0, 0, 0x0F, 128, 128, 128, 128, 0},    0,    0},
        {{0, 0, 0x0F, 128, 255, 128, 128, 0}, -127,    0},
        {{0, 0, 0x0F, 128, 128, 128,   0, 0},    0, -127},
        {{0, 0, 0x0F, 128, 128, 128, 255, 0},    0,  127}
    };
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int index;
    for (index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]); ++index) {
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE,
                                  fixtures[index].packet, 8U, &output) == 1);
        CHECK(output.ly == fixtures[index].forward);
        CHECK(output.rx == fixtures[index].turn);
        CHECK(output.lx == 0 && output.ry == 0);
        CHECK(output.buttons == 0U && output.hat == 0U);
    }
}

static void test_20bc_8byte_rejections(void)
{
    uint8_t packet[64] = {0U};
    uint8_t prefixed[9] = {1U};
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int length;
    memcpy(packet, captured_20bc_8byte_neutral, sizeof(captured_20bc_8byte_neutral));
    for (length = 0U; length <= 64U; ++length) {
        if (length != 8U) {
            check_rejected(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, (uint16_t)length);
        }
    }
    check_rejected(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, UINT16_MAX);
    check_rejected(GAMEPAD_REPORT_20BC_5500_8BYTE, NULL, 8U);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, 8U, NULL) == 0);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE, NULL, 8U, NULL) == 0);
    check_rejected((GamepadReportProfile)99, packet, 8U);
    check_rejected((GamepadReportProfile)-1, packet, 8U);
    /* The old nine-byte profile and the new eight-byte profile remain distinct. */
    check_rejected(GAMEPAD_REPORT_20BC_5500, packet, 8U);
    check_rejected(GAMEPAD_REPORT_20BC_5500_8BYTE, captured_neutral, 9U);
    memcpy(prefixed + 1U, captured_20bc_8byte_neutral, 8U);
    check_rejected(GAMEPAD_REPORT_20BC_5500_8BYTE, prefixed, 9U);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, captured_neutral,
                              9U, &output) == 1);
    CHECK(output.buttons == 0U && output.hat == 0U);
    CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    /* Byte zero is a button field here, not a required or rejected Report ID. */
    packet[0] = 0U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, 8U, &output) == 1);
    packet[0] = 1U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, 8U, &output) == 1);
    CHECK(output.rx == 0 && output.buttons == 0U);
    packet[0] = 2U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, 8U, &output) == 1);
    CHECK(output.rx == 0 && output.buttons == 0U);
}

/* Synthetic profile-4 contract fixtures; physical calibration is recorded separately. */
static const uint8_t synthetic_20bc_9byte_neutral[9] = {
    0U, 0U, 15U, 128U, 128U, 128U, 128U, 0U, 0U
};

static void test_20bc_9byte_neutral_and_axes(void)
{
    static const uint8_t offsets[4] = {3U, 4U, 5U, 6U};
    static const uint8_t inputs[11] = {
        0U, 1U, 117U, 118U, 127U, 128U, 129U, 138U, 139U, 254U, 255U
    };
    static const int x_expected[11] = {
        -127, -127, -11, -10, -1, 0, 1, 10, 11, 126, 127
    };
    static const int y_expected[11] = {
        127, 127, 11, 10, 1, 0, -1, -10, -11, -126, -127
    };
    uint8_t packet[9];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int axis;
    unsigned int sample;
    CHECK(GAMEPAD_REPORT_20BC_5500_9BYTE == 4);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                              synthetic_20bc_9byte_neutral, 9U, &output) == 1);
    CHECK(output.buttons == 0U && output.hat == 0U);
    CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    for (axis = 0U; axis < 4U; ++axis) {
        for (sample = 0U; sample < 11U; ++sample) {
            int axes[4];
            int expected;
            unsigned int other;
            memcpy(packet, synthetic_20bc_9byte_neutral, sizeof(packet));
            packet[offsets[axis]] = inputs[sample];
            CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                                      packet, 9U, &output) == 1);
            axes[0] = output.lx;
            axes[1] = output.ly;
            axes[2] = output.rx;
            axes[3] = output.ry;
            expected = (axis == 1U || axis == 3U) ? y_expected[sample] : x_expected[sample];
            CHECK(axes[axis] == expected);
            CHECK(output.buttons == 0U && output.hat == 0U);
            for (other = 0U; other < 4U; ++other) {
                if (other != axis) {
                    CHECK(axes[other] == 0);
                }
            }
        }
    }
}

static void test_20bc_9byte_hat_buttons_and_vendor(void)
{
    static const uint8_t hat_expected[16] = {
        1U, 3U, 2U, 6U, 4U, 12U, 8U, 9U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    /* Literal expected masks keep the decoder's table out of the oracle. */
    static const uint16_t button_expected[16] = {
        0U, 0U, 0U, 0U, 0U, 0U, 0x4000U, 0x8000U,
        0x0001U, 0x0002U, 0x0004U, 0x0008U, 0U, 0U, 0U, 0U
    };
    uint8_t packet[9];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int index;
    for (index = 0U; index < 16U; ++index) {
        memcpy(packet, synthetic_20bc_9byte_neutral, sizeof(packet));
        packet[2] = (uint8_t)(0xA0U | index);
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                                  packet, 9U, &output) == 1);
        CHECK(output.hat == hat_expected[index]);
        CHECK(output.buttons == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    }
    /* Usages 1..15 and the constant padding bit 15 are independently checked. */
    for (index = 0U; index < 16U; ++index) {
        uint16_t raw = (uint16_t)(1U << index);
        memcpy(packet, synthetic_20bc_9byte_neutral, sizeof(packet));
        packet[0] = (uint8_t)raw;
        packet[1] = (uint8_t)(raw >> 8);
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                                  packet, 9U, &output) == 1);
        CHECK(output.buttons == button_expected[index]);
        CHECK(output.hat == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.ry == 0);
        CHECK(output.rx == 0);
    }
    /* All pressed, both with and without the constant padding bit. */
    for (index = 0U; index < 2U; ++index) {
        memcpy(packet, synthetic_20bc_9byte_neutral, sizeof(packet));
        packet[0] = 0xFFU;
        packet[1] = (index == 0U) ? 0x7FU : 0xFFU;
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                                  packet, 9U, &output) == 1);
        CHECK(output.buttons == 0xC00FU);
        CHECK(output.rx == 0 && output.hat == 0U);
        CHECK((output.buttons & (GAMEPAD_BUTTON_MASK_CROSS | GAMEPAD_BUTTON_MASK_CIRCLE |
                                GAMEPAD_BUTTON_MASK_SQUARE | GAMEPAD_BUTTON_MASK_TRIANGLE)) == 0U);
    }
    /* Nine-byte schema has two full vendor fields; neither controls motion. */
    for (index = 0U; index < 256U; ++index) {
        memcpy(packet, synthetic_20bc_9byte_neutral, sizeof(packet));
        packet[7] = (uint8_t)index;
        packet[8] = (uint8_t)(255U - index);
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                                  packet, 9U, &output) == 1);
        CHECK(output.buttons == 0U && output.hat == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.rx == 0 && output.ry == 0);
    }
}

static void test_20bc_9byte_face_keys_and_native_turn_axis(void)
{
    /* Calibrated physical turn is byte5. Unmapped usages 1..4 cannot turn
       even with the native axis centered; measured face14/15 are below. */
    static const uint8_t analog_inputs[9] = {
        0U, 117U, 118U, 127U, 128U, 129U, 138U, 139U, 255U
    };
    static const int analog_expected[9] = {-127, -11, -10, -1, 0, 1, 10, 11, 127};
    uint8_t packet[9];
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int direction;
    unsigned int sample;
    for (direction = 0U; direction < 16U; ++direction) {
        memcpy(packet, synthetic_20bc_9byte_neutral, sizeof(packet));
        packet[0] = (uint8_t)direction;
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                                  packet, 9U, &output) == 1);
        CHECK(output.rx == 0);
        CHECK(output.buttons == 0U);
        CHECK(output.lx == 0 && output.ly == 0 && output.ry == 0 && output.hat == 0U);
        for (sample = 0U; sample < 9U; ++sample) {
            memcpy(packet, synthetic_20bc_9byte_neutral, sizeof(packet));
            packet[0] = (uint8_t)direction;
            packet[1] = 0x80U; /* Padding cannot act as a button or affect the native axis. */
            packet[3] = 0U;
            packet[4] = 255U;
            packet[6] = 0U;
            packet[5] = analog_inputs[sample];
            CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                                      packet, 9U, &output) == 1);
            CHECK(output.rx == analog_expected[sample]);
            CHECK(output.lx == -127 && output.ly == -127 && output.ry == 127);
            CHECK(output.buttons == 0U && output.hat == 0U);
        }
    }
    /* Shoulder and system actions coexist with native right/left movement. */
    memcpy(packet, synthetic_20bc_9byte_neutral, sizeof(packet));
    packet[0] = 0xF2U;
    packet[1] = 0x0FU;
    packet[5] = 255U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                              packet, 9U, &output) == 1);
    CHECK(output.rx == 127 && output.buttons == 0xC00FU);
    packet[0] = 0xF8U;
    packet[5] = 0U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                              packet, 9U, &output) == 1);
    CHECK(output.rx == -127 && output.buttons == 0xC00FU);
}

static void test_20bc_9byte_rejections_and_profile_isolation(void)
{
    uint8_t packet[64] = {0U};
    uint8_t prefixed[10] = {0U};
    HID_GAMEPAD_Info_TypeDef current, old, old8;
    unsigned int length;
    memcpy(packet, synthetic_20bc_9byte_neutral, 9U);
    for (length = 0U; length <= 64U; ++length) {
        if (length != 9U) {
            check_rejected(GAMEPAD_REPORT_20BC_5500_9BYTE, packet, (uint16_t)length);
        }
    }
    check_rejected(GAMEPAD_REPORT_20BC_5500_9BYTE, packet, UINT16_MAX);
    check_rejected(GAMEPAD_REPORT_20BC_5500_9BYTE, NULL, 9U);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE, packet, 9U, NULL) == 0);
    memcpy(prefixed + 1U, synthetic_20bc_9byte_neutral, 9U);
    check_rejected(GAMEPAD_REPORT_20BC_5500_9BYTE, prefixed, 10U);
    check_rejected(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, 9U);
    check_rejected(GAMEPAD_REPORT_0810_0001, packet, 9U);
    /* Same bytes under old profile 1 keep its original byte5-X/byte6-Y map. */
    packet[5] = 0U;
    packet[6] = 255U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE, packet, 9U, &current) == 1);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet, 9U, &old) == 1);
    CHECK(current.rx == -127 && current.ry == -127);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, 8U, &old8) == 1);
    CHECK(old8.rx == 127 && old8.ry == 127);
    CHECK(old.rx == -127 && old.ry == -127);
    /* First byte is a raw button field, including face usage 2, never an ID. */
    packet[0] = 2U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE, packet, 9U, &current) == 1);
    CHECK(current.rx == -127 && current.buttons == 0U);
    /* Distinct button mapping cannot leak back to either older 20BC profile. */
    packet[0] = 0U; packet[1] = 0x08U;
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE, packet, 9U, &current) == 1);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500, packet, 9U, &old) == 1);
    CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_8BYTE, packet, 8U, &old8) == 1);
    CHECK(current.buttons == 0x0008U && old.buttons == 0x0040U && old8.buttons == 0x0040U);
}

static void test_20bc_9byte_measured_controls(void)
{
    /* User-labelled 18-step PC capture, receiver A/MI01, 2026-10-13.
       Other four receivers have matching descriptors, not action validation. */
    static const struct {
        uint8_t packet[9];
        int lx, ly, rx, ry;
        uint16_t buttons;
        uint8_t hat;
    } fixtures[] = {
        {{0,0,15,128,  0,128,128,0,0},    0, 127,   0,   0, 0x0000, 0}, /* left forward */
        {{0,0,15,128,255,128,128,0,0},    0,-127,   0,   0, 0x0000, 0}, /* left back */
        {{0,0,15,  0,128,128,128,0,0}, -127,   0,   0,   0, 0x0000, 0}, /* left left */
        {{0,0,15,255,128,128,128,0,0},  127,   0,   0,   0, 0x0000, 0}, /* left right */
        {{0,0,15,128,128,128,  0,0,0},    0,   0,   0, 127, 0x0000, 0}, /* right forward */
        {{0,0,15,128,128,128,255,0,0},    0,   0,   0,-127, 0x0000, 0}, /* right back */
        {{0,0,15,128,128,  0,128,0,0},    0,   0,-127,   0, 0x0000, 0}, /* right left */
        {{0,0,15,128,128,255,128,0,0},    0,   0, 127,   0, 0x0000, 0}, /* right right */
        {{0x40,0,15,128,128,128,128,0,0},0,   0,   0,   0, 0x4000, 0}, /* L1 */
        {{0,1,15,128,128,128,128,0,0},    0,   0,   0,   0, 0x0001, 0}, /* L2 */
        {{0x80,0,15,128,128,128,128,0,0},0,   0,   0,   0, 0x8000, 0}, /* R1 */
        {{0,2,15,128,128,128,128,0,0},    0,   0,   0,   0, 0x0002, 0}, /* R2 */
        {{0,8,15,128,128,128,128,0,0},    0,   0,   0,   0, 0x0008, 0}, /* START */
        {{0,4,15,128,128,128,128,0,0},    0,   0,   0,   0, 0x0004, 0}, /* SELECT */
        {{0,0x20,15,128,128,128,128,0,0},0,   0,   0,   0, 0x0000, 0}, /* face left */
        {{0,0x40,15,128,128,128,128,0,0},0,   0,   0,   0, 0x0000, 0}, /* face right */
        {{0,0, 0,128,128,128,128,0,0},    0,   0,   0,   0, 0x0000, 1}, /* hat up */
        {{0,0, 2,128,128,128,128,0,0},    0,   0,   0,   0, 0x0000, 2}  /* hat right */
    };
    HID_GAMEPAD_Info_TypeDef output;
    unsigned int index;
    for (index = 0U; index < sizeof(fixtures) / sizeof(fixtures[0]); ++index) {
        CHECK(Gamepad_DecodeReport(GAMEPAD_REPORT_20BC_5500_9BYTE,
                                  fixtures[index].packet, 9U, &output) == 1);
        CHECK(output.lx == fixtures[index].lx);
        CHECK(output.ly == fixtures[index].ly);
        CHECK(output.rx == fixtures[index].rx);
        CHECK(output.ry == fixtures[index].ry);
        CHECK(output.buttons == fixtures[index].buttons);
        CHECK(output.hat == fixtures[index].hat);
    }
}

int main(void)
{
    test_20bc_9byte_measured_controls();
    test_20bc_9byte_neutral_and_axes();
    test_20bc_9byte_hat_buttons_and_vendor();
    test_20bc_9byte_face_keys_and_native_turn_axis();
    test_20bc_9byte_rejections_and_profile_isolation();
    test_captured_neutral();
    test_axes();
    test_hat();
    test_buttons();
    test_legacy();
    test_rejections();
    test_0810_neutral_and_axes();
    test_0810_hat_buttons_and_vendor();
    test_0810_rejections();
    test_0810_digital_turn_and_analog_priority();
    test_0810_measured_controls();
    test_20bc_8byte_neutral_and_axes();
    test_20bc_8byte_hat_buttons_and_vendor();
    test_20bc_8byte_face_keys_and_native_turn_axis();
    test_20bc_8byte_measured_controls();
    test_20bc_8byte_rejections();
    if (failures != 0) {
        fprintf(stderr, "%d failures in %u checks\n", failures, checks);
        return 1;
    }
    printf("PASS: %u gamepad report checks\n", checks);
    return 0;
}
