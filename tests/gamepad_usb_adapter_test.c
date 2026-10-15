#include "usbh_hid_gamepad.h"
#include "usbh_hiwonder_hid.h"
#include <stdio.h>
#include <string.h>

/* Production event callback owns this pointer; this test is its API caller. */
HID_GAMEPAD_Info_TypeDef *info;

static USBH_ClassTypeDef *expected_class;
/* Identity discovery is the adapter's dependency. Its real implementation is
   exercised by the separate class and actual-consumer integration tests. */
HID_TypeTypeDef HIWONDER_USBH_HID_GetDeviceType(USBH_HandleTypeDef *host)
{
    HID_HandleTypeDef *hid;
    if (host == NULL || host->gState != HOST_CLASS ||
        host->pActiveClass != expected_class || expected_class == NULL ||
        expected_class->pData == NULL) {
        return HID_UNKNOWN;
    }
    hid = (HID_HandleTypeDef *)expected_class->pData;
    return hid->Init == USBH_HID_GamepadInit ? (HID_TypeTypeDef)HID_GAMEPAD : HID_KEYBOARD;
}
static USBH_StatusTypeDef keyboard_init_stub(USBH_HandleTypeDef *host)
{ (void)host; return USBH_OK; }

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
    expected_class = &test->usb_class;
    test->host.gState = HOST_CLASS;
    test->host.device.is_connected = 1U;
    test->host.device.PortEnabled = 1U;
    test->usb_class.pData = &test->hid;
    test->hid.length = endpoint_size;
    test->hid.Init = USBH_HID_GamepadInit;
}

/* Model the production caller's independent cycle clock for existing cases. */
#define TEST_CYCLES_PER_MS 84000U
static USBH_StatusTypeDef init_gamepad(USBH_HandleTypeDef *host)
{
    USBH_StatusTypeDef status = USBH_HID_GamepadInit(host);
    USBH_HID_GamepadSafetyPoll(host, host->Timer * TEST_CYCLES_PER_MS, TEST_CYCLES_PER_MS);
    return status;
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
    CHECK(init_gamepad(&test.host) == USBH_OK);
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
    CHECK(gamepad_reports_accepted == 0U && gamepad_reports_rejected == 0U);
    test.host.gState = HOST_CLASS;
    info = USBH_HID_GetGamepadInfo(&test.host);
    CHECK(info == NULL); /* A stale frame received outside CLASS cannot reappear. */
    queue_neutral(&test);
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
    CHECK(init_gamepad(&test.host) == USBH_OK);
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
        CHECK(init_gamepad(&test.host) == USBH_OK);
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
    CHECK(init_gamepad(&test.host) == USBH_OK);
    queue_neutral(&test);
    CHECK(info != NULL);
    USBH_HID_GamepadReceive(&test.host, 8U);
    queue_neutral(&test);
    CHECK(gamepad_reports_accepted == 2U && gamepad_reports_rejected == 1U);
    USBH_HID_GamepadReset();
    check_no_input(&test);
    CHECK(gamepad_reports_accepted == 0U && gamepad_reports_rejected == 0U);
    CHECK(init_gamepad(&test.host) == USBH_OK);
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
    CHECK(init_gamepad(&test.host) == USBH_FAIL);
    check_no_input(&test);
    test.hid.length = 65U;
    CHECK(init_gamepad(&test.host) == USBH_FAIL);
    test.usb_class.pData = NULL;
    CHECK(init_gamepad(&test.host) == USBH_FAIL);
    USBH_HID_GamepadReceive(&test.host, 9U);
    CHECK(gamepad_reports_rejected == 0U); /* Transport failure, not a format error. */
    check_no_input(&test);

    test.usb_class.pData = &test.hid;
    test.hid.length = 64U;
    CHECK(init_gamepad(&test.host) == USBH_OK);
    test.hid.pData = NULL;
    USBH_HID_GamepadReceive(&test.host, 9U);
    CHECK(gamepad_reports_rejected == 1U);
    check_no_input(&test);
}

static const uint8_t neutral_0810[8] = {
    GAMEPAD_0810_SELECTED_REPORT_ID, 0x80U, 0x80U, 0x80U, 0x80U, 0x0FU, 0U, 0U
};









static void test_timeout_does_not_change_existing_profiles(void)
{
    TestHost test;
    static const uint8_t legacy[8] = {1U, 128U, 127U, 128U, 127U, 0xFFU, 0U, 0U};
    setup_host(&test, 0x20BCU, 0x5500U, 64U);
    CHECK(init_gamepad(&test.host) == USBH_OK);
    queue_neutral(&test);
    test.host.Timer = 1000000U;
    USBH_HID_GamepadCheckTimeout(&test.host);
    CHECK(info != NULL && gamepad_reports_timed_out == 0U);
    setup_host(&test, 0x2563U, 0x0526U, 32U);
    CHECK(USBH_HID_GamepadUsesInterruptInput(&test.host) == 0U);
    CHECK(init_gamepad(&test.host) == USBH_OK);
    memcpy(test.hid.pData, legacy, sizeof(legacy));
    USBH_HID_GamepadReceive(&test.host, 8U);
    info = USBH_HID_GetGamepadInfo(&test.host);
    test.host.Timer = 1000000U;
    USBH_HID_GamepadCheckTimeout(&test.host);
    CHECK(info != NULL && gamepad_reports_timed_out == 0U);
}



static const uint8_t neutral_20bc8[8] = {
    0U, 0U, 0x0FU, 0x80U, 0x80U, 0x80U, 0x80U, 0U
};

static void setup_20bc8(TestHost *test)
{
    setup_host(test, 0x20BCU, 0x5500U, 8U);
    test->host.device.DevDesc.bcdDevice = 0x1003U;
    test->hid.HID_Desc.wItemLength = 90U;
    CHECK(init_gamepad(&test->host) == USBH_OK);
}



static void test_20bc8_exact_profile_selection(void)
{
    static const struct {
        uint16_t bcd;
        uint16_t endpoint;
        uint16_t descriptor;
        int selects_eight;
    } cases[] = {
        {0x1003U, 8U, 90U, 1},
        {0x1002U, 8U, 90U, 0},
        {0x1004U, 8U, 90U, 0},
        {0x1003U, 8U, 89U, 0},
        {0x1003U, 8U, 91U, 0},
        {0x1003U, 8U, 0U, 0},
        {0x1003U, 7U, 90U, 0},
        {0x1003U, 9U, 90U, 0},
        {0x1003U, 64U, 90U, 0},
        {0x1003U, 64U, 148U, 0}
    };
    TestHost test;
    unsigned int index;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        setup_host(&test, 0x20BCU, 0x5500U, cases[index].endpoint);
        test.host.device.DevDesc.bcdDevice = cases[index].bcd;
        test.hid.HID_Desc.wItemLength = cases[index].descriptor;
        CHECK(init_gamepad(&test.host) == USBH_OK);
        CHECK(USBH_HID_GamepadIs20BC(&test.host) != 0U);
        memcpy(test.hid.pData, neutral_20bc8, sizeof(neutral_20bc8));
        if (cases[index].selects_eight) {
            USBH_HID_GamepadReceive(&test.host, 8U);
            info = USBH_HID_GetGamepadInfo(&test.host);
            CHECK(info == NULL);
        }
        test.hid.pData[0] = 2U; /* Buttons, never Report ID 2 on this receiver. */
        USBH_HID_GamepadReceive(&test.host, 8U);
        info = USBH_HID_GetGamepadInfo(&test.host);
        if (cases[index].selects_eight) {
            CHECK(info != NULL && info->rx == 0 && info->buttons == 0U);
            CHECK(gamepad_raw_buttons == 2U && gamepad_reports_accepted == 2U);
            CHECK(gamepad_reports_ignored == 0U && gamepad_reports_rejected == 0U);
            USBH_HID_GamepadReceive(&test.host, 9U);
            check_no_input(&test);
            CHECK(gamepad_reports_rejected == 1U);
        } else {
            check_no_input(&test);
            CHECK(gamepad_reports_accepted == 0U && gamepad_reports_rejected == 1U);
            if (cases[index].endpoint >= 9U) {
                memcpy(test.hid.pData, neutral_report, sizeof(neutral_report));
                USBH_HID_GamepadReceive(&test.host, 9U);
                info = USBH_HID_GetGamepadInfo(&test.host);
                CHECK(info != NULL && info->lx == 0 && info->ly == 0 && info->rx == 0);
                test.host.Timer = 1000000U;
                USBH_HID_GamepadCheckTimeout(&test.host);
                CHECK(info != NULL && gamepad_reports_timed_out == 0U);
            }
        }
    }
    /* All four identifying fields matter: nearby VID/PID remains legacy. */
    for (index = 0U; index < 2U; ++index) {
        setup_host(&test, index == 0U ? 0x20BDU : 0x20BCU,
                   index == 0U ? 0x5500U : 0x5501U, 8U);
        test.host.device.DevDesc.bcdDevice = 0x1003U;
        test.hid.HID_Desc.wItemLength = 90U;
        CHECK(init_gamepad(&test.host) == USBH_OK);
        CHECK(USBH_HID_GamepadIs20BC(&test.host) == 0U);
        memcpy(test.hid.pData, neutral_20bc8, sizeof(neutral_20bc8));
        USBH_HID_GamepadReceive(&test.host, 8U);
        info = USBH_HID_GetGamepadInfo(&test.host);
        CHECK(info != NULL && info->lx == -128 && info->ly == 112);
        CHECK(gamepad_raw_buttons == 0x8000U);
    }
}





static void send_channel(TestHost *test,HID_HandleTypeDef *hid,uint8_t channel,
                           const uint8_t *packet,uint32_t length,uint32_t now)
{
    test->host.Timer=now;
    USBH_HID_GamepadSafetyPoll(&test->host, now * TEST_CYCLES_PER_MS, TEST_CYCLES_PER_MS);
    if(packet!=NULL) { memcpy(hid->pData,packet,length<=8U?length:8U); }
    USBH_HID_GamepadReceiveChannel(&test->host,hid,channel,length);
    info=USBH_HID_GetGamepadInfo(&test->host);
}

static void test_dual_storage_and_arbitration(void)
{
    TestHost test;
    HID_HandleTypeDef second;
    uint8_t forward[8],turn[8],face[8];
    uint32_t accepted;
    unsigned int i;
    setup_20bc8(&test);
    memset(&second,0,sizeof(second));second.length=8U;second.HID_Desc.wItemLength=90U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host,&second,1U)==USBH_OK);
    CHECK(second.pData!=NULL && second.pData!=test.hid.pData);
    CHECK(((uintptr_t)second.pData % 4U)==0U);
    second.length=9U;CHECK(USBH_HID_GamepadInitChannel(&test.host,&second,1U)==USBH_FAIL);
    second.length=8U;second.HID_Desc.wItemLength=91U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host,&second,1U)==USBH_FAIL);
    second.HID_Desc.wItemLength=90U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host,&second,1U)==USBH_OK);
    memcpy(forward,neutral_20bc8,8U);forward[4]=0U;
    memcpy(turn,neutral_20bc8,8U);turn[6]=255U;
    memcpy(face,neutral_20bc8,8U);face[0]=10U;
    send_channel(&test,&second,1U,turn,8U,1U);check_no_input(&test);
    send_channel(&test,&test.hid,0U,neutral_20bc8,8U,2U);
    send_channel(&test,&second,1U,neutral_20bc8,8U,3U);
    send_channel(&test,&test.hid,0U,forward,8U,4U);
    CHECK(info!=NULL && info->ly==127 && gamepad_active_channel==0U);
    send_channel(&test,&second,1U,face,8U,5U);
    CHECK(info!=NULL && info->ly==127 && gamepad_raw_buttons==0U);
    CHECK(gamepad_channel_raw_buttons[1]==10U && gamepad_channel_info[1].rx==0);
    /* Raw buttons omitted from the action map must still prevent neutral. */
    send_channel(&test,&test.hid,0U,face,8U,6U);
    CHECK(info!=NULL && gamepad_active_channel==0U && gamepad_raw_buttons==10U);
    send_channel(&test,&test.hid,0U,neutral_20bc8,8U,7U);check_no_input(&test);
    send_channel(&test,&second,1U,turn,8U,17U);check_no_input(&test);
    send_channel(&test,&second,1U,neutral_20bc8,8U,18U);
    send_channel(&test,&second,1U,turn,8U,19U);
    CHECK(info!=NULL && info->rx==127 && info->ly==0 && gamepad_active_channel==1U);
    accepted=gamepad_reports_accepted;
    for(i=0U;i<8U;++i) {
        send_channel(&test,&test.hid,0U,forward,i,20U+i);
        CHECK(info!=NULL && info->rx==127 && gamepad_active_channel==1U);
    }
    CHECK(gamepad_reports_accepted==accepted && gamepad_channel_rejected[0]==8U);
    send_channel(&test,&second,1U,turn,7U,30U);check_no_input(&test);
    CHECK(gamepad_channel_rejected[1]==1U);
    send_channel(&test,&second,1U,turn,8U,41U);check_no_input(&test);
    send_channel(&test,&second,1U,neutral_20bc8,8U,42U);
    send_channel(&test,&second,1U,turn,8U,43U);
    CHECK(info!=NULL && gamepad_active_channel==1U);
    CHECK(gamepad_channel_accepted[0]>0U && gamepad_channel_accepted[1]>0U);
    USBH_HID_GamepadReset();check_no_input(&test);
    CHECK(gamepad_active_channel==0xFFU && gamepad_channel_accepted[0]==0U && gamepad_channel_accepted[1]==0U);
}

static void test_owner_timeout_and_wrap(void)
{
    TestHost test;
    HID_HandleTypeDef second;
    uint8_t forward[8];
    uint32_t start=UINT32_MAX-100U;
    setup_20bc8(&test);
    memset(&second,0,sizeof(second));second.length=8U;second.HID_Desc.wItemLength=90U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host,&second,1U)==USBH_OK);
    memcpy(forward,neutral_20bc8,8U);forward[4]=0U;
    send_channel(&test,&second,1U,neutral_20bc8,8U,start-1U);
    send_channel(&test,&second,1U,forward,8U,start);
    CHECK(info!=NULL && gamepad_active_channel==1U);
    send_channel(&test,&test.hid,0U,neutral_20bc8,8U,start+400U);
    test.host.Timer=start+499U;USBH_HID_GamepadCheckTimeout(&test.host);
    CHECK(info!=NULL && gamepad_reports_timed_out==0U);
    test.host.Timer=start+500U;USBH_HID_GamepadCheckTimeout(&test.host);
    check_no_input(&test);CHECK(gamepad_reports_timed_out==1U);
    test.host.Timer=start+501U;USBH_HID_GamepadCheckTimeout(&test.host);
    CHECK(gamepad_reports_timed_out==1U);
    send_channel(&test,&test.hid,0U,forward,8U,start+510U);check_no_input(&test);
    send_channel(&test,&test.hid,0U,neutral_20bc8,8U,start+511U);
    send_channel(&test,&test.hid,0U,forward,8U,start+512U);
    CHECK(info!=NULL && gamepad_active_channel==0U);
    USBH_HID_GamepadCheckTimeout(NULL);
    CHECK(info!=NULL);
}

static void test_0810_two_ids_and_digital_turn(void)
{
    TestHost test;
    uint8_t packets[2][8],right[8],left[8],bad[8];
    uint8_t channel;
    setup_host(&test,0x0810U,1U,8U);
    CHECK(init_gamepad(&test.host)==USBH_OK);
    CHECK(USBH_HID_GamepadIs0810(&test.host) && USBH_HID_GamepadUsesInterruptInput(&test.host));
    memcpy(packets[0],neutral_0810,8U);memcpy(packets[1],neutral_0810,8U);packets[1][0]=2U;
    for(channel=0U;channel<2U;++channel) {
        uint32_t now=100U+100U*channel;
        send_channel(&test,&test.hid,0U,packets[channel],8U,now);
        memcpy(right,packets[channel],8U);right[5]=0x2FU;
        send_channel(&test,&test.hid,0U,right,8U,now+1U);
        CHECK(info!=NULL && info->rx==127 && info->buttons==0U);
        CHECK(gamepad_active_channel==channel && gamepad_raw_buttons==2U);
        memcpy(bad,packets[channel],8U);bad[0]=3U;
        send_channel(&test,&test.hid,0U,bad,8U,now+2U);
        CHECK(info!=NULL && gamepad_active_channel==channel);
        memcpy(left,packets[channel],8U);left[5]=0x8FU;
        send_channel(&test,&test.hid,0U,left,8U,now+3U);
        CHECK(info!=NULL && info->rx==-127 && gamepad_raw_buttons==8U);
        left[1]=139U;
        send_channel(&test,&test.hid,0U,left,8U,now+4U);
        CHECK(info!=NULL && info->rx==11);
        send_channel(&test,&test.hid,0U,packets[channel],8U,now+5U);
        check_no_input(&test);
    }
    CHECK(gamepad_reports_ignored==0U && gamepad_reports_rejected==2U);
    CHECK(gamepad_channel_accepted[0]>0U && gamepad_channel_accepted[1]>0U);
}

static void setup_20bc9(TestHost *test)
{
    setup_host(test, 0x20BCU, 0x5500U, 64U);
    test->hid.HID_Desc.wItemLength = 123U;
    CHECK(init_gamepad(&test->host) == USBH_OK);
}

static void send9(TestHost *test, HID_HandleTypeDef *hid, uint8_t channel,
                  const uint8_t *packet, uint32_t length, uint32_t now)
{
    test->host.Timer = now;
    USBH_HID_GamepadSafetyPoll(&test->host, now * TEST_CYCLES_PER_MS, TEST_CYCLES_PER_MS);
    if (packet != NULL) {
        memset(hid->pData, 0xA5, hid->length);
        memcpy(hid->pData, packet, length < 9U ? length : 9U);
    }
    USBH_HID_GamepadReceiveChannel(&test->host, hid, channel, length);
    info = USBH_HID_GetGamepadInfo(&test->host);
}

static void test_20bc9_exact_profile_selection(void)
{
    static const struct {
        uint16_t bcd, endpoint, descriptor;
        GamepadReportProfile expected;
    } cases[] = {
        {0x1003U, 64U, 123U, GAMEPAD_REPORT_20BC_5500_9BYTE},
        {0x1002U, 64U, 123U, GAMEPAD_REPORT_20BC_5500},
        {0x1004U, 64U, 123U, GAMEPAD_REPORT_20BC_5500},
        {0x1003U, 64U, 122U, GAMEPAD_REPORT_20BC_5500},
        {0x1003U, 64U, 124U, GAMEPAD_REPORT_20BC_5500},
        {0x1003U, 64U, 95U, GAMEPAD_REPORT_20BC_5500},
        {0x1003U, 64U, 90U, GAMEPAD_REPORT_20BC_5500},
        {0x1003U, 63U, 123U, GAMEPAD_REPORT_20BC_5500},
        {0x1003U, 9U, 123U, GAMEPAD_REPORT_20BC_5500},
        {0x1003U, 8U, 123U, GAMEPAD_REPORT_20BC_5500},
        {0x1003U, 8U, 90U, GAMEPAD_REPORT_20BC_5500_8BYTE}
    };
    TestHost test;
    HID_HandleTypeDef second;
    uint8_t moving[9];
    unsigned int index;
    memcpy(moving, neutral_report, 9U);
    moving[4] = 0U;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        setup_host(&test, 0x20BCU, 0x5500U, cases[index].endpoint);
        test.host.device.DevDesc.bcdDevice = cases[index].bcd;
        test.hid.HID_Desc.wItemLength = cases[index].descriptor;
        CHECK(USBH_HID_Gamepad20BCProfile(&test.host, cases[index].endpoint,
                                        cases[index].descriptor) == cases[index].expected);
        CHECK(init_gamepad(&test.host) == USBH_OK);
        memset(&second, 0, sizeof(second));
        second.length = 64U;
        second.HID_Desc.wItemLength = 123U;
        CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 1U) ==
              (cases[index].expected == GAMEPAD_REPORT_20BC_5500_9BYTE ? USBH_OK : USBH_FAIL));
        if (cases[index].endpoint >= 9U) {
            /* Only verified profile4 requires a neutral frame before movement. */
            send9(&test, &test.hid, 0U, moving, 9U, 1U);
            if (cases[index].expected == GAMEPAD_REPORT_20BC_5500_9BYTE) {
                check_no_input(&test);
                send9(&test, &test.hid, 0U, neutral_report, 9U, 2U);
                send9(&test, &test.hid, 0U, moving, 9U, 3U);
                CHECK(info != NULL && info->ly == 127 && gamepad_active_channel == 0U);
                test.host.Timer = 503U;
                USBH_HID_GamepadCheckTimeout(&test.host);
                check_no_input(&test);
                CHECK(gamepad_reports_timed_out == 1U);
            } else {
                CHECK(info != NULL && info->ly == 127);
                test.host.Timer = 1000000U;
                USBH_HID_GamepadCheckTimeout(&test.host);
                CHECK(info != NULL && gamepad_reports_timed_out == 0U);
            }
        }
    }
    CHECK(USBH_HID_Gamepad20BCProfile(NULL, 64U, 123U) == GAMEPAD_REPORT_LEGACY);
    for (index = 0U; index < 2U; ++index) {
        setup_host(&test, index == 0U ? 0x20BDU : 0x20BCU,
                   index == 0U ? 0x5500U : 0x5501U, 64U);
        CHECK(USBH_HID_Gamepad20BCProfile(&test.host, 64U, 123U) == GAMEPAD_REPORT_LEGACY);
    }
}

static void test_20bc9_dual_storage_lengths_and_raw_buttons(void)
{
    TestHost test;
    HID_HandleTypeDef second;
    uint8_t forward[9], turn[9], padding[9], face[9];
    unsigned int byte, length;
    uint32_t rejected;
    setup_20bc9(&test);
    memset(&second, 0, sizeof(second));
    second.length = 64U; second.HID_Desc.wItemLength = 123U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 1U) == USBH_OK);
    CHECK(test.hid.pData != second.pData && second.pData != NULL);
    CHECK((uintptr_t)second.pData % 4U == 0U && (uintptr_t)test.hid.pData % 4U == 0U);
    for (byte = 0U; byte < 64U; ++byte) {
        CHECK(test.hid.pData[byte] == 0U && second.pData[byte] == 0U);
    }
    second.length = 8U; second.HID_Desc.wItemLength = 90U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 1U) == USBH_FAIL);
    second.length = 63U; second.HID_Desc.wItemLength = 123U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 1U) == USBH_FAIL);
    second.length = 64U; second.HID_Desc.wItemLength = 122U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 1U) == USBH_FAIL);
    second.HID_Desc.wItemLength = 123U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 1U) == USBH_OK);
    CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 2U) == USBH_FAIL);
    memcpy(forward, neutral_report, 9U); forward[4] = 0U;
    memcpy(turn, neutral_report, 9U); turn[5] = 255U;
    memcpy(padding, neutral_report, 9U); padding[1] = 0x80U;
    memcpy(face, neutral_report, 9U); face[0] = 2U;
    send9(&test, &second, 1U, turn, 9U, 1U); check_no_input(&test);
    send9(&test, &test.hid, 0U, padding, 9U, 2U);
    CHECK(gamepad_channel_raw_buttons[0] == 0U);
    send9(&test, &second, 1U, neutral_report, 9U, 3U);
    send9(&test, &test.hid, 0U, forward, 9U, 4U);
    CHECK(info != NULL && info->ly == 127 && gamepad_active_channel == 0U);
    for (byte = 9U; byte < 64U; ++byte) {
        CHECK(test.hid.pData[byte] == 0xA5U && second.pData[byte] == 0xA5U);
    }
    send9(&test, &second, 1U, turn, 9U, 5U);
    CHECK(info != NULL && info->ly == 127 && info->rx == 0);
    send9(&test, &test.hid, 0U, face, 9U, 6U);
    CHECK(info != NULL && gamepad_raw_buttons == 2U && info->buttons == 0U);
    send9(&test, &test.hid, 0U, padding, 9U, 7U); check_no_input(&test);
    send9(&test, &second, 1U, turn, 9U, 17U); check_no_input(&test);
    send9(&test, &second, 1U, neutral_report, 9U, 18U);
    send9(&test, &second, 1U, turn, 9U, 19U);
    CHECK(info != NULL && info->rx == 127 && info->ly == 0 && gamepad_active_channel == 1U);
    rejected = gamepad_reports_rejected;
    for (length = 0U; length <= 65U; ++length) {
        if (length == 9U) { continue; }
        send9(&test, &test.hid, 0U, forward, length, 20U + length);
        CHECK(info != NULL && info->rx == 127 && gamepad_active_channel == 1U);
    }
    send9(&test, &test.hid, 0U, forward, 65545U, 86U);
    send9(&test, &test.hid, 0U, forward, UINT32_MAX, 87U);
    CHECK(gamepad_channel_rejected[0] == 67U && gamepad_reports_rejected == rejected + 67U);
    CHECK(gamepad_channel_rejected[1] == 0U);
    send9(&test, &second, 1U, turn, 64U, 90U); check_no_input(&test);
    CHECK(gamepad_channel_rejected[1] == 1U);
    send9(&test, &second, 1U, turn, 9U, 100U); check_no_input(&test);
    send9(&test, &second, 1U, padding, 9U, 101U);
    send9(&test, &second, 1U, turn, 9U, 102U);
    CHECK(info != NULL && info->rx == 127 && gamepad_active_channel == 1U);
    USBH_HID_GamepadReset(); check_no_input(&test);
    setup_20bc9(&test);
    CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 1U) == USBH_OK);
    send9(&test, &second, 1U, turn, 9U, 200U); check_no_input(&test);
    send9(&test, &second, 1U, neutral_report, 9U, 201U);
    send9(&test, &second, 1U, turn, 9U, 202U);
    CHECK(info != NULL && gamepad_active_channel == 1U);
}

static void test_20bc9_timeout_and_wrap(void)
{
    TestHost test;
    HID_HandleTypeDef second;
    uint8_t forward[9];
    uint32_t start=UINT32_MAX-100U;
    setup_20bc9(&test);
    memset(&second,0,sizeof(second));second.length=64U;second.HID_Desc.wItemLength=123U;
    CHECK(USBH_HID_GamepadInitChannel(&test.host,&second,1U)==USBH_OK);
    memcpy(forward,neutral_report,9U);forward[4]=0U;
    send9(&test,&second,1U,neutral_report,9U,start-1U);
    send9(&test,&second,1U,forward,9U,start);
    CHECK(info!=NULL && gamepad_active_channel==1U);
    send9(&test,&test.hid,0U,neutral_report,9U,start+400U);
    test.host.Timer=start+499U;USBH_HID_GamepadCheckTimeout(&test.host);
    CHECK(info!=NULL && gamepad_reports_timed_out==0U);
    test.host.Timer=start+500U;USBH_HID_GamepadCheckTimeout(&test.host);
    check_no_input(&test);CHECK(gamepad_reports_timed_out==1U);
    test.host.Timer=start+501U;USBH_HID_GamepadCheckTimeout(&test.host);
    CHECK(gamepad_reports_timed_out==1U);
    send9(&test,&test.hid,0U,forward,9U,start+510U);check_no_input(&test);
    send9(&test,&test.hid,0U,neutral_report,9U,start+511U);
    send9(&test,&test.hid,0U,forward,9U,start+512U);
    CHECK(info!=NULL && gamepad_active_channel==0U);
    USBH_HID_GamepadCheckTimeout(NULL);
    CHECK(info!=NULL);
}

static void test_20bc9_all_raw_button_bits(void)
{
    TestHost test;
    uint8_t packet[9];
    unsigned int bit;
    static const uint16_t expected[16] = {
        0U,0U,0U,0U,0U,0U,0x4000U,0x8000U,
        0x0001U,0x0002U,0x0004U,0x0008U,0U,0U,0U,0U
    };
    for (bit = 0U; bit < 16U; ++bit) {
        uint16_t raw = (uint16_t)(1U << bit);
        setup_20bc9(&test);
        send9(&test, &test.hid, 0U, neutral_report, 9U, 1U);
        memcpy(packet, neutral_report, 9U);
        packet[0] = (uint8_t)raw;
        packet[1] = (uint8_t)(0x80U | (raw >> 8));
        send9(&test, &test.hid, 0U, packet, 9U, 2U);
        CHECK(gamepad_channel_raw_buttons[0] == (raw & 0x7FFFU));
        if (bit < 15U) {
            CHECK(info != NULL && gamepad_active_channel == 0U);
            CHECK(gamepad_raw_buttons == raw);
            CHECK(info->buttons == expected[bit]);
            CHECK(info->lx == 0 && info->ly == 0 && info->rx == 0 && info->ry == 0);
        } else {
            check_no_input(&test);
            CHECK(gamepad_active_channel == 0xFFU);
        }
        packet[0] = 0U; packet[1] = 0x80U;
        send9(&test, &test.hid, 0U, packet, 9U, 3U); check_no_input(&test);
    }
}


/* These cases deliberately freeze USB time and never run SOF. */
static void cycle_packet(TestHost *test, HID_HandleTypeDef *hid, uint8_t channel,
                         const uint8_t *packet, uint32_t length, uint32_t cycles)
{
    USBH_HID_GamepadSafetyPoll(&test->host, cycles, TEST_CYCLES_PER_MS);
    memcpy(hid->pData, packet, length);
    USBH_HID_GamepadReceiveChannel(&test->host, hid, channel, length);
    info = USBH_HID_GetGamepadInfo(&test->host);
}

static void test_independent_cycle_timeout_and_wrap(void)
{
    static const uint32_t starts[] = {123U, UINT32_MAX - 200U};
    unsigned int index;
    for (index = 0U; index < sizeof(starts) / sizeof(starts[0]); ++index) {
        TestHost test;
        HID_HandleTypeDef second;
        HID_GAMEPAD_Info_TypeDef *cached;
        uint8_t forward[9];
        uint32_t start = starts[index], accepted;
        setup_20bc9(&test);
        memset(&second, 0, sizeof(second));
        second.length = 64U; second.HID_Desc.wItemLength = 123U;
        CHECK(USBH_HID_GamepadInitChannel(&test.host, &second, 1U) == USBH_OK);
        memcpy(forward, neutral_report, 9U); forward[4] = 0U;
        test.host.Timer = 16599U;
        cycle_packet(&test, &test.hid, 0U, neutral_report, 9U, start - 1U);
        cycle_packet(&test, &test.hid, 0U, forward, 9U, start);
        CHECK(info != NULL && info->ly == 127 && gamepad_active_channel == 0U);
        cached = info;
        cycle_packet(&test, &second, 1U, neutral_report, 9U,
                     start + 400U * TEST_CYCLES_PER_MS);
        accepted = gamepad_reports_accepted;
        USBH_HID_GamepadSafetyPoll(&test.host,
            start + 500U * TEST_CYCLES_PER_MS - 1U, TEST_CYCLES_PER_MS);
        CHECK(info == cached && info != NULL && info->ly == 127);
        CHECK(gamepad_reports_timed_out == 0U);
        USBH_HID_GamepadSafetyPoll(&test.host,
            start + 500U * TEST_CYCLES_PER_MS, TEST_CYCLES_PER_MS);
        check_no_input(&test);
        CHECK(cached->ly == 0 && cached->rx == 0 && cached->buttons == 0U);
        CHECK(gamepad_active_channel == 0xFFU && gamepad_reports_timed_out == 1U);
        CHECK(gamepad_reports_accepted == accepted && test.host.Timer == 16599U);
        USBH_HID_GamepadSafetyPoll(&test.host,
            start + 501U * TEST_CYCLES_PER_MS, TEST_CYCLES_PER_MS);
        CHECK(gamepad_reports_timed_out == 1U); /* Expire each lease once. */
        test.host.Timer += 10U; /* USB resumes; original handoff timebase resumes too. */
        cycle_packet(&test, &second, 1U, forward, 9U,
                     start + 510U * TEST_CYCLES_PER_MS);
        check_no_input(&test); /* Old neutral/arming does not survive owner expiry. */
        cycle_packet(&test, &second, 1U, neutral_report, 9U,
                     start + 511U * TEST_CYCLES_PER_MS);
        cycle_packet(&test, &second, 1U, forward, 9U,
                     start + 512U * TEST_CYCLES_PER_MS);
        CHECK(info != NULL && info->ly == 127 && gamepad_active_channel == 1U);
    }
}

static void test_transport_failure_invalidates_cached_output(void)
{
    unsigned int fault;
    for (fault = 0U; fault < 9U; ++fault) {
        TestHost test;
        uint8_t forward[9];
        HID_GAMEPAD_Info_TypeDef *cached;
        uint32_t accepted;
        USBH_ClassTypeDef foreign_class;
        setup_20bc9(&test);
        memcpy(forward, neutral_report, 9U); forward[4] = 0U;
        test.host.Timer = 16599U;
        cycle_packet(&test, &test.hid, 0U, neutral_report, 9U, 0U);
        cycle_packet(&test, &test.hid, 0U, forward, 9U, 1U);
        cached = info; accepted = gamepad_reports_accepted;
        CHECK(cached != NULL && cached->ly == 127);
        switch (fault) {
        case 0U: test.host.device.PortEnabled = 0U; break;
        case 1U: test.host.device.is_connected = 0U; break;
        case 2U: test.host.device.is_disconnected = 1U; break;
        case 3U: test.host.gState = HOST_CLASS_REQUEST; break;
        case 4U: test.host.pActiveClass = NULL; break;
        case 5U: test.usb_class.pData = NULL; break;
        case 7U:
            foreign_class = test.usb_class;
            test.host.pActiveClass = &foreign_class;
            break;
        case 8U: test.hid.Init = keyboard_init_stub; break;
        default: break;
        }
        USBH_HID_GamepadSafetyPoll(fault == 6U ? NULL : &test.host, 2U, TEST_CYCLES_PER_MS);
        CHECK(info == NULL && gamepad_active_channel == 0xFFU);
        CHECK(cached->ly == 0 && cached->rx == 0);
        CHECK(gamepad_reports_accepted == accepted && gamepad_reports_timed_out == 0U);
        test.host.device.PortEnabled = test.host.device.is_connected = 1U;
        test.host.device.is_disconnected = 0U;
        test.host.gState = HOST_CLASS;
        test.host.pActiveClass = &test.usb_class;
        test.usb_class.pData = &test.hid;
        test.hid.Init = USBH_HID_GamepadInit;
        test.host.Timer += 10U;
        cycle_packet(&test, &test.hid, 0U, forward, 9U, 3U);
        check_no_input(&test);
        cycle_packet(&test, &test.hid, 0U, neutral_report, 9U, 4U);
        cycle_packet(&test, &test.hid, 0U, forward, 9U, 5U);
        CHECK(info != NULL && info->ly == 127);
    }
}

static void test_clock_configuration_and_old_periodic_profiles(void)
{
    static const uint32_t invalid_rates[] = {0U, UINT32_MAX};
    unsigned int index, profile;
    for (index = 0U; index < sizeof(invalid_rates) / sizeof(invalid_rates[0]); ++index) {
        TestHost test;
        uint8_t forward[9];
        setup_20bc9(&test);
        memcpy(forward, neutral_report, 9U); forward[4] = 0U;
        cycle_packet(&test, &test.hid, 0U, neutral_report, 9U, 0U);
        cycle_packet(&test, &test.hid, 0U, forward, 9U, 1U);
        CHECK(info != NULL);
        USBH_HID_GamepadSafetyPoll(&test.host, 2U, invalid_rates[index]);
        check_no_input(&test);
        USBH_HID_GamepadReceive(&test.host, 9U);
        CHECK(USBH_HID_GetGamepadInfo(&test.host) == NULL);
    }
    for (profile = 0U; profile < 2U; ++profile) {
        TestHost test;
        uint8_t neutral[8], forward[8];
        if (profile == 0U) {
            setup_20bc8(&test); memcpy(neutral, neutral_20bc8, 8U);
        } else {
            setup_host(&test, 0x0810U, 1U, 8U);
            CHECK(init_gamepad(&test.host) == USBH_OK);
            memcpy(neutral, neutral_0810, 8U);
        }
        memcpy(forward, neutral, 8U); forward[4] = 0U;
        cycle_packet(&test, &test.hid, 0U, neutral, 8U, 0U);
        cycle_packet(&test, &test.hid, 0U, forward, 8U, 1U);
        CHECK(info != NULL && info->ly == 127);
        USBH_HID_GamepadSafetyPoll(&test.host, 1U + 500U * TEST_CYCLES_PER_MS,
                                   TEST_CYCLES_PER_MS);
        check_no_input(&test);
        CHECK(gamepad_reports_timed_out == 1U);
    }
}

static void test_nonperiodic_profiles_keep_valid_report(void)
{
    TestHost test;
    unsigned int profile;
    static const uint8_t legacy[8] = {1U, 128U, 0U, 255U, 127U, 0xFFU, 0U, 0U};
    uint8_t forward[9];
    memcpy(forward, neutral_report, 9U); forward[4] = 0U;
    for (profile = 0U; profile < 2U; ++profile) {
        setup_host(&test, profile == 0U ? 0x20BCU : 0x2563U,
                   profile == 0U ? 0x5500U : 0x0526U, 64U);
        CHECK(init_gamepad(&test.host) == USBH_OK);
        cycle_packet(&test, &test.hid, 0U, profile == 0U ? forward : legacy,
                     profile == 0U ? 9U : 8U, 0U);
        CHECK(info != NULL && info->ly == 127);
        USBH_HID_GamepadSafetyPoll(&test.host, 10000U * TEST_CYCLES_PER_MS,
                                   TEST_CYCLES_PER_MS);
        CHECK(info != NULL && info->ly == 127 && gamepad_reports_timed_out == 0U);
        test.host.device.PortEnabled = 0U;
        USBH_HID_GamepadSafetyPoll(&test.host, 10001U * TEST_CYCLES_PER_MS,
                                   TEST_CYCLES_PER_MS);
        check_no_input(&test);
    }
}

int main(void)
{
    test_independent_cycle_timeout_and_wrap();
    test_transport_failure_invalidates_cached_output();
    test_clock_configuration_and_old_periodic_profiles();
    test_nonperiodic_profiles_keep_valid_report();
    test_20bc9_all_raw_button_bits();
    test_20bc9_exact_profile_selection();
    test_20bc9_dual_storage_lengths_and_raw_buttons();
    test_20bc9_timeout_and_wrap();
    test_dual_storage_and_arbitration();
    test_owner_timeout_and_wrap();
    test_0810_two_ids_and_digital_turn();
    test_new_endpoint_and_publication();
    test_invalid_reports_clear_input();
    test_legacy_receiver();
    test_disconnect_and_reconnect();
    test_invalid_storage();
    test_timeout_does_not_change_existing_profiles();
    test_20bc8_exact_profile_selection();
    if (failures != 0) {
        fprintf(stderr, "%d failures in %u checks\n", failures, checks);
        return 1;
    }
    printf("PASS: %u gamepad USB adapter checks\n", checks);
    return 0;
}
