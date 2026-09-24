/* Compile the actual bluetooth.c, replacing only the UART/CPU hardware edges. */
#include "bluetooth.h"
#include "bluetooth_link.h"
#include "persistent_storage.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

DWT_Type test_dwt;
uint32_t SystemCoreClock = 1000000U;
static DMA_HandleTypeDef dma;
UART_HandleTypeDef huart6 = { USART6, &dma };
uint8_t blue_front, blue_back, blue_left, blue_right, running_mode;
volatile uint8_t flag_move = 1U;
int modify;
volatile float voltage = 11.5f;
float velocity_left = -12.0f, velocity_right = 34.0f, acceleration_y = 1.2f, gyro_turn = -3.4f;
uint16_t dis = 99U;
PID_Params pidparams = { -4.0f, 140, 450, 30, 80, 5, 500, 0U };

static uint32_t primask;
static uint8_t pending_uart_error;
static unsigned int delivered_uart_errors;
static unsigned int directions_at_irq;
static uint8_t uart_error_during_rx_abort;
static uint8_t *rx_memory;
static uint8_t rx_active, tx_active;
static const uint8_t *tx_memory;
static uint16_t tx_length;
static unsigned int rx_starts, rx_aborts, tx_starts, tx_aborts;
static HAL_StatusTypeDef next_rx_status;
static HAL_StatusTypeDef next_registration_status;
static HAL_UART_RxEventTypeTypeDef event_type;
static void (*rx_callback)(UART_HandleTypeDef *, uint16_t);
static void (*tx_callback)(UART_HandleTypeDef *);

uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1U; }
void __set_PRIMASK(uint32_t previous)
{
    primask = previous;
    if (primask == 0U && pending_uart_error != 0U) {
        /* Model a higher-priority pending UART IRQ at interrupt restoration. */
        pending_uart_error = 0U;
        directions_at_irq = (unsigned int)blue_front | ((unsigned int)blue_back << 1U) |
                            ((unsigned int)blue_left << 2U) | ((unsigned int)blue_right << 3U);
        ++delivered_uart_errors;
        bluetooth_uart_error_callback(&huart6);
    }
}
HAL_StatusTypeDef HAL_UART_RegisterRxEventCallback(UART_HandleTypeDef *uart,
                                                   void (*callback)(UART_HandleTypeDef *, uint16_t))
{
    HAL_StatusTypeDef status = next_registration_status;
    next_registration_status = HAL_OK;
    assert(uart == &huart6);
    if (status == HAL_OK) rx_callback = callback;
    return status;
}
HAL_StatusTypeDef HAL_UART_RegisterCallback(UART_HandleTypeDef *uart, unsigned int id,
                                            void (*callback)(UART_HandleTypeDef *))
{ assert(uart == &huart6 && id == HAL_UART_TX_COMPLETE_CB_ID); tx_callback = callback; return HAL_OK; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *uart, uint8_t *memory, uint16_t length)
{
    HAL_StatusTypeDef status = next_rx_status;
    next_rx_status = HAL_OK;
    assert(uart == &huart6 && length == BT_WIRE_MAX);
    if (status != HAL_OK) return status;
    if (rx_active) return HAL_BUSY;
    rx_memory = memory;
    rx_active = 1U;
    dma.ht_enabled = 1U;
    ++rx_starts;
    return HAL_OK;
}
HAL_UART_RxEventTypeTypeDef HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *uart)
{ assert(uart == &huart6); return event_type; }
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *uart, uint8_t *memory, uint16_t length)
{
    assert(uart == &huart6 && length > 0U && length <= BT_WIRE_MAX);
    if (tx_active) return HAL_BUSY;
    tx_memory = memory;
    tx_length = length;
    tx_active = 1U;
    ++tx_starts;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{ assert(uart == &huart6); tx_active = 0U; ++tx_aborts; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{
    /* HAL abort may wait; it must run outside the interrupt-masked arm commit. */
    assert(uart == &huart6 && primask == 0U);
    rx_active = 0U;
    ++rx_aborts;
    if (uart_error_during_rx_abort != 0U) {
        uart_error_during_rx_abort = 0U;
        bluetooth_uart_error_callback(&huart6);
    }
    return HAL_OK;
}

static void reset(void)
{
    rx_active = tx_active = 0U;
    rx_starts = rx_aborts = tx_starts = tx_aborts = 0U;
    next_rx_status = HAL_OK;
    next_registration_status = HAL_OK;
    pending_uart_error = 0U;
    uart_error_during_rx_abort = 0U;
    delivered_uart_errors = directions_at_irq = 0U;
    running_mode = 0U;
    flag_move = 1U;
    primask = 0U;
    test_dwt.CYCCNT = 0U;
    bluetooth_init();
    bluetooth_control_update(1U);
    assert(rx_active && !dma.ht_enabled);
}

static void at(uint32_t ms) { test_dwt.CYCCNT = ms * 1000U; }
static void receive_bytes(const uint8_t *bytes, size_t length, HAL_UART_RxEventTypeTypeDef event)
{
    assert(rx_active && length <= BT_WIRE_MAX);
    memcpy(rx_memory, bytes, length);
    event_type = event;
    if (event != HAL_UART_RXEVENT_HT) rx_active = 0U;
    rx_callback(&huart6, (uint16_t)length);
}
static void receive(const char *wire)
{ receive_bytes((const uint8_t *)wire, strlen(wire), HAL_UART_RXEVENT_IDLE); }
static void process(void)
{
    bluetooth_main_loop_task();
    bluetooth_control_update((uint8_t)(running_mode == 0U));
    assert(primask == 0U);
}
static void tx_complete(void)
{
    assert(tx_active);
    tx_active = 0U;
    tx_callback(&huart6);
}
static void assert_tx(const char *wire)
{ assert(tx_active && tx_length == strlen(wire) && memcmp(tx_memory, wire, tx_length) == 0); }

static void test_motion(void)
{
    reset();
    receive("CMD|3|1|1|$"); process();
    assert(!blue_front && !blue_right);
    at(10U); receive("CMD|3|0|0|$CMD|3|100|-100|$"); process();
    assert(blue_front && blue_left && flag_move == 1U);
    at(400U); receive("CMD|3|1|1x|$CMD|7|$"); process();
    assert(blue_front && blue_left && bluetooth_link_stats.invalid_frames == 1U);
    at(509U); bluetooth_control_update(1U); assert(blue_front);
    at(510U); bluetooth_control_update(1U);
    assert(!blue_front && !blue_left && flag_move == 1U);
    /* First block was armed before timeout; it must not re-arm motion. */
    at(520U); receive("CMD|3|0|0|$CMD|3|1|1|$"); process();
    assert(!blue_front && !blue_right);
    at(620U); receive("CMD|3|1|1|$"); process(); assert(!blue_front);
    at(720U); receive("CMD|3|0|0|$"); process();
    at(730U); receive("CMD|3|-1|1|$"); process(); assert(blue_back && blue_right);
    flag_move = 0U;
    at(740U); receive("CMD|3|0|0|$"); process();
    assert(!blue_back && !blue_right && !flag_move);
    at(750U); receive("CMD|3|1|1|$"); process();
    running_mode = 2U; bluetooth_control_update(0U);
    assert(!blue_front && !blue_right);
    at(760U); receive("CMD|3|0|0|$"); process();
    running_mode = 0U; bluetooth_control_update(1U);
    at(770U); receive("CMD|3|0|0|$"); process();
    at(780U); receive("CMD|3|1|1|$"); process(); assert(!blue_front);
    at(790U); receive("CMD|3|0|0|$CMD|3|1|1|$"); process();
    assert(blue_front && blue_right && !flag_move);
}

static void test_rx_and_stale(void)
{
    unsigned int starts, i;
    char block[128];
    reset();
    starts = rx_starts;
    memset(block, 'x', sizeof(block));
    receive_bytes((const uint8_t *)block, 64U, HAL_UART_RXEVENT_HT);
    assert(rx_starts == starts && rx_active);
    memcpy(block + 116U, "CMD|3|0|0|$", 11U);
    block[127] = 'x';
    receive_bytes((const uint8_t *)block, 128U, HAL_UART_RXEVENT_TC);
    process();
    assert(rx_starts == starts + 1U && !dma.ht_enabled);
    at(10U); receive("CMD|3|1|0|$"); process(); assert(blue_front);
    at(20U); receive("CMD|3|-1|0|$"); /* delayed main-loop consumption */
    at(510U); bluetooth_control_update(1U);
    at(520U); process(); assert(!blue_front && !blue_back);
    /* Age is checked even if an unarmed guard has not incremented its epoch. */
    reset();
    receive("CMD|3|0|0|$CMD|3|1|0|$");
    at(500U); process(); assert(!blue_front);
    assert(bluetooth_link_stats.stale_frames == 2U);
    reset();
    receive("CMD|3|0|0|$CMD|3|1|0|$"); process(); assert(blue_front);
    for (i = 0U; i < 9U; ++i) receive("CMD|7|$");
    assert(bluetooth_link_stats.rx_overflows == 1U);
    bluetooth_control_update(1U); assert(!blue_front);
    process(); assert(rx_active && !dma.ht_enabled);
    at(10U); receive("CMD|3|1|0|$"); process(); assert(!blue_front);
    at(20U); receive("CMD|3|0|0|$CMD|3|1|0|$"); process(); assert(blue_front);
    bluetooth_uart_error_callback(&huart6);
    bluetooth_control_update(1U); assert(!blue_front);
    process(); assert(rx_active && bluetooth_link_stats.rx_errors == 1U);
    next_rx_status = HAL_BUSY;
    receive("CMD|3|0|0|$");
    assert(bluetooth_link_stats.rx_restart_errors == 1U);
    process(); assert(rx_active && !dma.ht_enabled);
    receive("CMD|3|1|0|$"); process(); assert(!blue_front);
    /* A failed callback registration is retried before re-arming reception. */
    rx_active = 0U;
    next_registration_status = HAL_BUSY;
    bluetooth_init();
    assert(!rx_active && bluetooth_link_stats.rx_restart_errors == 1U);
    process();
    assert(rx_active && !dma.ht_enabled);
}

static void test_telemetry_ownership(void)
{
    uint8_t held[128];
    uint16_t held_length;
    reset();
    receive("CMD|4|1|$CMD|5|1|$CMD|6|1|$"); process();
    at(100U); bluetooth_periodic_update();
    assert_tx("CMD|4|34|-12|$");
    held_length = tx_length;
    memcpy(held, tx_memory, tx_length);
    at(120U); receive("CMD|8|1|$"); process();
    dis = 123U; velocity_right = 77.0f;
    at(200U); bluetooth_periodic_update();
    assert(tx_length == held_length && memcmp(tx_memory, held, held_length) == 0);
    tx_complete(); process();
    assert_tx("CMD|8|-4.0|140|30|5|450|80|500|$");
    assert(tx_length > 20U);
    tx_complete(); process(); assert_tx("CMD|5|123|$");
    tx_complete(); process(); assert_tx("CMD|6|1.2|-3.4|$");
    tx_complete(); process(); assert_tx("CMD|4|77|-12|$");
    tx_complete(); process(); assert(!tx_active);
    assert(tx_starts == 5U);
    at(210U); receive("CMD|7|$"); process(); assert_tx("CMD|7|11500|$");
    at(460U); process(); assert(!tx_active && bluetooth_link_stats.tx_errors == 1U);
    assert(tx_aborts == 1U);
}

static void test_fragmentation_and_invalid(void)
{
    const uint8_t nul[] = { 'C','M','D','|','3','|','1',0,'|','0','|','$' };
    reset();
    receive("CMD|3|0|"); process();
    at(1U); receive("0|$"); process();
    at(2U); receive("CMD|3|1|"); process();
    at(3U); receive("0|$"); process(); assert(blue_front);
    at(100U); receive_bytes(nul, sizeof(nul), HAL_UART_RXEVENT_IDLE); process();
    at(503U); bluetooth_control_update(1U); assert(!blue_front);
    assert(flag_move == 1U);
    reset();
    modify = 0;
    receive("CMD|2|1|1junk|2|$CMD|1|NaN|$"); process();
    assert(!modify && pidparams.balance_kp == 140 && pidparams.middle_angle == -4.0f);
    receive("CMD|2|1|141|451|$CMD|1|-4.25|$"); process();
    assert(modify && pidparams.balance_kp == 141 && pidparams.balance_kd == 451);
    assert(pidparams.middle_angle == -4.25f);
    receive("CMD|1|-4.25e0|$"); process();
    assert(pidparams.middle_angle == -4.25f);
    receive("CMD|1|1e99999999|$"); process();
    assert(pidparams.middle_angle == -4.25f);
    receive("CMD|1|1e-99999999|$"); process();
    assert(pidparams.middle_angle == 0.0f);
}

static void test_irq_snapshot(void)
{
    reset();
    receive("CMD|3|0|0|$CMD|3|1|-1|$");
    bluetooth_main_loop_task(); /* Motion has changed; ISR output is still zero. */
    assert(!blue_front && !blue_left);
    pending_uart_error = 1U;
    bluetooth_control_update(1U);
    assert(delivered_uart_errors == 1U && !pending_uart_error && primask == 0U);
    assert(directions_at_irq == 5U); /* Complete front + left, never a partial axis. */
    assert(blue_front && blue_left && flag_move == 1U);
    bluetooth_control_update(1U);
    assert(!blue_front && !blue_back && !blue_left && !blue_right && flag_move == 1U);

    reset();
    receive("CMD|3|0|0|$CMD|3|-1|1|$");
    bluetooth_main_loop_task();
    primask = 1U;
    pending_uart_error = 1U;
    bluetooth_control_update(1U);
    assert(primask == 1U && delivered_uart_errors == 0U && pending_uart_error);
    assert(blue_back && blue_right);
    __set_PRIMASK(0U);
    assert(delivered_uart_errors == 1U && directions_at_irq == 10U);
    bluetooth_control_update(1U);
    assert(!blue_front && !blue_back && !blue_left && !blue_right && flag_move == 1U);
}

static void test_silent_epoch_rearm(void)
{
    unsigned int starts;
    const uint8_t *held_tx;
    reset();
    receive("CMD|3|0|0|$CMD|3|1|0|$"); process(); assert(blue_front);
    at(499U); receive("CMD|7|$"); process(); assert_tx("CMD|7|11500|$");
    held_tx = tx_memory;
    starts = rx_starts;
    at(500U); bluetooth_control_update(1U); assert(!blue_front);
    bluetooth_main_loop_task();
    assert(rx_aborts == 1U && rx_starts == starts + 1U && rx_active && !dma.ht_enabled);
    assert(tx_aborts == 0U && tx_memory == held_tx);
    assert_tx("CMD|7|11500|$");
    /* First fresh gesture works even if its zero and motion are one UART burst. */
    at(501U); receive("CMD|3|0|0|$CMD|3|1|-1|$"); process();
    assert(blue_front && blue_left && flag_move == 1U && tx_aborts == 0U);

    /* An old partial parser cannot join a new epoch tail and unlock motion. */
    reset();
    receive("CMD|3|0|0|$CMD|3|1|0|$"); process();
    at(10U); receive("CMD|3|0|"); process();
    at(500U); bluetooth_control_update(1U); bluetooth_main_loop_task();
    at(501U); receive("0|$CMD|3|1|0|$"); process(); assert(!blue_front);
    at(502U); receive("CMD|3|0|0|$CMD|3|1|0|$"); process(); assert(blue_front);

    /* UART callback may already have armed and queued the current epoch.
       Main must skip only the stale blocks, not flush the fresh zero/motion. */
    reset();
    receive("CMD|3|0|0|$CMD|3|1|0|$"); process();
    at(499U); receive("CMD|3|0|0|$CMD|3|-1|0|$"); /* queue remains unread */
    at(500U); bluetooth_control_update(1U);
    at(501U); receive("CMD|3|-1|0|$"); /* completes old arm, creates new arm */
    at(502U); receive("CMD|3|0|0|$CMD|3|1|1|$");
    process();
    assert(blue_front && blue_right && !blue_back && rx_aborts == 0U);

    reset();
    receive("CMD|3|0|0|$CMD|3|1|0|$"); process();
    running_mode = 2U; bluetooth_control_update(0U); bluetooth_main_loop_task();
    receive("CMD|3|0|0|$CMD|3|1|0|$"); process(); assert(!blue_front);
    running_mode = 0U; bluetooth_control_update(1U); bluetooth_main_loop_task();
    receive("CMD|3|0|0|$CMD|3|-1|1|$"); process();
    assert(blue_back && blue_right && rx_aborts == 2U && tx_aborts == 0U);

    bluetooth_uart_error_callback(&huart6);
    bluetooth_control_update(1U); assert(!blue_back);
    bluetooth_main_loop_task();
    receive("CMD|3|0|0|$CMD|3|1|-1|$"); process();
    assert(blue_front && blue_left && flag_move == 1U);

    /* An error IRQ while main aborts an obsolete arm wins over a normal re-arm. */
    reset();
    receive("CMD|3|0|0|$CMD|3|1|0|$"); process();
    at(500U); bluetooth_control_update(1U);
    uart_error_during_rx_abort = 1U;
    bluetooth_main_loop_task();
    assert(!rx_active && bluetooth_link_stats.rx_errors == 1U);
    bluetooth_main_loop_task();
    assert(rx_active && !dma.ht_enabled);
    at(501U); receive("CMD|3|0|0|$CMD|3|1|-1|$"); process();
    assert(blue_front && blue_left && flag_move == 1U);
}

int main(void)
{
    test_motion();
    test_rx_and_stale();
    test_telemetry_ownership();
    test_fragmentation_and_invalid();
    test_irq_snapshot();
    test_silent_epoch_rearm();
    puts("bluetooth_adapter: actual HAL bridge, DMA ownership, RX faults and motion guard passed");
    return 0;
}
