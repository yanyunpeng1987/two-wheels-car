/* HC-05D BLE UART bridge. LINK-001/002/003; no AT traffic during operation. */
#include "bluetooth.h"
#include "bluetooth_link.h"
#include "main.h"
#include "control.h"
#include "persistent_storage.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define BT_RX_SLOTS 8U
#define BT_TX_TIMEOUT_MS 250U

typedef struct {
    uint8_t data[BT_WIRE_MAX];
    uint16_t length;
    uint32_t cycles;
    uint32_t epoch;
} BtRxBlock;

static uint8_t rx_dma_buffers[2][BT_WIRE_MAX];
static volatile uint8_t rx_dma_index;
static volatile uint32_t rx_dma_epoch;
static BtRxBlock rx_blocks[BT_RX_SLOTS];
static volatile uint32_t rx_write;
static volatile uint32_t rx_read;
static volatile uint8_t rx_recovery;
static volatile uint8_t rx_paused;
static uint8_t callbacks_ready;
static BtParser parser;
static volatile BtMotionGuard motion;
static BtTxQueue tx_queue;
static volatile uint8_t tx_done;
static uint8_t tx_started;
static uint8_t tx_pending;
static uint32_t tx_started_cycles;
static uint32_t last_periodic_cycles;
static uint8_t reporting[BT_TELEMETRY_SLOTS];

bool enable_bluetooth_output = true;
int datw;
volatile BluetoothLinkStats bluetooth_link_stats;
/* Retain the existing diagnostic ABI; these are not part of the wire format. */
volatile LogMessage g_log_ring_buffer[LOG_BUFFER_COUNT];
volatile uint16_t g_log_write_index;
volatile uint16_t g_log_read_index;

static uint32_t lock_interrupts(void)
{
    uint32_t previous = __get_PRIMASK();
    __disable_irq();
    return previous;
}

static void receive_fault(void)
{
    uint32_t previous = lock_interrupts();
    rx_recovery = 1U;
    bt_guard_invalidate(&motion);
    __set_PRIMASK(previous);
}

static uint8_t start_receive(uint8_t index)
{
    uint32_t previous = lock_interrupts();
    uint32_t epoch = motion.epoch;
    /* Capture the epoch at DMA arm, so old partial bytes cannot re-arm motion. */
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart6, rx_dma_buffers[index], BT_WIRE_MAX) != HAL_OK) {
        ++bluetooth_link_stats.rx_restart_errors;
        receive_fault();
        __set_PRIMASK(previous);
        return 0U;
    }
    __HAL_DMA_DISABLE_IT(huart6.hdmarx, DMA_IT_HT);
    rx_dma_index = index;
    rx_dma_epoch = epoch;
    __set_PRIMASK(previous);
    return 1U;
}

static void synchronize_receive_epoch(void)
{
    uint32_t previous = lock_interrupts();
    if (parser.length != 0U && parser.epoch != motion.epoch) bt_parser_reset(&parser);
    if (rx_recovery != 0U || rx_dma_epoch == motion.epoch) {
        __set_PRIMASK(previous);
        return;
    }
    /* Main-loop only: discard an old DMA arm during silence, without touching TX.
       Already queued blocks retain their epochs and are filtered individually. */
    rx_paused = 1U;
    __set_PRIMASK(previous);
    if (HAL_UART_AbortReceive(&huart6) != HAL_OK) {
        ++bluetooth_link_stats.rx_restart_errors;
        receive_fault();
    }
    previous = lock_interrupts();
    if (rx_recovery == 0U) (void)start_receive(0U);
    rx_paused = 0U;
    __set_PRIMASK(previous);
}

static uint8_t register_callbacks(void)
{
    if (callbacks_ready != 0U) return 1U;
    if (HAL_UART_RegisterRxEventCallback(&huart6, bluetooth_dma_rx_callback) != HAL_OK ||
        HAL_UART_RegisterCallback(&huart6, HAL_UART_TX_COMPLETE_CB_ID,
                                  bluetooth_tx_complete_callback) != HAL_OK) {
        ++bluetooth_link_stats.rx_restart_errors;
        receive_fault();
        return 0U;
    }
    callbacks_ready = 1U;
    return 1U;
}

void bluetooth_init(void)
{
    memset((void *)&bluetooth_link_stats, 0, sizeof(bluetooth_link_stats));
    memset(reporting, 0, sizeof(reporting));
    bt_parser_reset(&parser);
    bt_tx_init(&tx_queue);
    bt_guard_init(&motion, SystemCoreClock / 1000U,
                  (uint8_t)(running_mode == Normal_Mode));
    rx_write = rx_read = 0U;
    rx_recovery = tx_done = tx_started = tx_pending = 0U;
    rx_paused = 0U;
    callbacks_ready = 0U;
    last_periodic_cycles = DWT->CYCCNT;
    if (!register_callbacks()) return;
    (void)start_receive(0U);
}

void bluetooth_dma_rx_callback(UART_HandleTypeDef *huart, uint16_t size)
{
    uint8_t completed;
    uint32_t epoch, received_cycles;
    HAL_UART_RxEventTypeTypeDef event;
    BtRxBlock *block;
    if (huart->Instance != USART6) return;
    event = HAL_UARTEx_GetRxEventType(huart);
    if (event == HAL_UART_RXEVENT_HT) return;
    if (rx_recovery != 0U || rx_paused != 0U) return;
    if ((event != HAL_UART_RXEVENT_IDLE && event != HAL_UART_RXEVENT_TC) ||
        size == 0U || size > BT_WIRE_MAX) {
        ++bluetooth_link_stats.rx_errors;
        receive_fault();
        return;
    }
    completed = rx_dma_index;
    epoch = rx_dma_epoch;
    received_cycles = DWT->CYCCNT;
    if (!start_receive((uint8_t)(completed ^ 1U))) return;
    if ((uint32_t)(rx_write - rx_read) >= BT_RX_SLOTS) {
        ++bluetooth_link_stats.rx_overflows;
        receive_fault();
        return;
    }
    block = &rx_blocks[rx_write % BT_RX_SLOTS];
    memcpy(block->data, rx_dma_buffers[completed], size);
    block->length = size;
    block->cycles = received_cycles;
    block->epoch = epoch;
    __DMB();
    ++rx_write;
}

void bluetooth_uart_error_callback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6) {
        ++bluetooth_link_stats.rx_errors;
        receive_fault();
    }
}

void bluetooth_tx_complete_callback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6) tx_done = 1U;
}

/* Called in the control ISR after all mode selection, before velocity/turn. */
void bluetooth_control_update(uint8_t normal_mode)
{
    /* Publish one coherent snapshot even if UART IRQ priority changes later. */
    uint32_t previous = lock_interrupts();
    bt_guard_tick(&motion, DWT->CYCCNT, normal_mode);
    blue_front = (uint8_t)(motion.speed > 0);
    blue_back = (uint8_t)(motion.speed < 0);
    blue_right = (uint8_t)(motion.turn > 0);
    blue_left = (uint8_t)(motion.turn < 0);
    __set_PRIMASK(previous);
}

static uint8_t submit_frame(uint8_t channel, const char *data, int length)
{
    if (!enable_bluetooth_output || length <= 0 || length > (int)BT_WIRE_MAX) return 0U;
    return bt_tx_submit(&tx_queue, channel, (const uint8_t *)data, (uint16_t)length);
}

void bluetooth_send_data(const uint8_t *data, uint16_t size)
{
    if (enable_bluetooth_output) (void)bt_tx_submit(&tx_queue, 0U, data, size);
}

bool send_response(const char *response)
{
    /* Legacy internal length prefix; send neither the prefix nor trailing NUL. */
    if (response == NULL || !enable_bluetooth_output) return false;
    return bt_tx_submit(&tx_queue, 0U, (const uint8_t *)&response[1],
                        (uint8_t)response[0]) != 0U;
}

static void service_transmit(void)
{
    const BtTxFrame *frame;
    uint32_t now = DWT->CYCCNT;
    if (tx_done != 0U) {
        tx_done = 0U;
        tx_started = tx_pending = 0U;
        bt_tx_complete(&tx_queue);
    }
    if (tx_pending != 0U &&
        (uint32_t)(now - tx_started_cycles) >= (SystemCoreClock / 1000U) * BT_TX_TIMEOUT_MS) {
        (void)HAL_UART_AbortTransmit(&huart6);
        ++bluetooth_link_stats.tx_errors;
        tx_done = tx_started = tx_pending = 0U;
        bt_tx_complete(&tx_queue);
    }
    if (tx_started != 0U || !enable_bluetooth_output) return;
    frame = bt_tx_begin(&tx_queue);
    if (frame == NULL) return;
    if (tx_pending == 0U) {
        tx_started_cycles = now;
        tx_pending = 1U;
    }
    if (HAL_UART_Transmit_DMA(&huart6, (uint8_t *)frame->data, frame->length) == HAL_OK) {
        tx_started = 1U;
    }
}

static void process_command(uint32_t last_cycles)
{
    BtCommand command;
    char response[BT_WIRE_MAX + 1U];
    int length = 0;
    uint32_t previous;
    if (!bt_decode_command(parser.data, &command)) {
        ++bluetooth_link_stats.invalid_frames;
        return;
    }
    previous = lock_interrupts();
    bt_guard_tick(&motion, DWT->CYCCNT, (uint8_t)(running_mode == Normal_Mode));
    if (parser.epoch != motion.epoch || rx_recovery != 0U ||
        (uint32_t)(DWT->CYCCNT - parser.first_cycles) >= motion.timeout_cycles) {
        __set_PRIMASK(previous);
        ++bluetooth_link_stats.stale_frames;
        return;
    }
    if (command.code == 3U) {
        /* The neutral transition cannot be overwritten by a following command. */
        (void)bt_guard_receive(&motion, command.args[0], command.args[1],
                               parser.first_cycles, last_cycles, parser.epoch, DWT->CYCCNT);
        __set_PRIMASK(previous);
        return;
    }
    __set_PRIMASK(previous);
    switch (command.code) {
    case 1:
        previous = lock_interrupts();
        pidparams.middle_angle = command.decimal;
        modify = 1;
        __set_PRIMASK(previous);
        break;
    case 2:
        previous = lock_interrupts();
        if (command.args[0] == 1) {
            pidparams.balance_kp = command.args[1];
            pidparams.balance_kd = command.args[2];
        } else if (command.args[0] == 2) {
            pidparams.velocity_kp = command.args[1];
            pidparams.velocity_ki = command.args[2];
        } else {
            pidparams.turn_kp = command.args[1];
            pidparams.turn_kd = command.args[2];
        }
        modify = 1;
        __set_PRIMASK(previous);
        break;
    case 4: case 5: case 6:
        reporting[command.code - 4U] = (uint8_t)command.args[0];
        if (command.args[0] == 0) tx_queue.telemetry[command.code - 4U].length = 0U;
        break;
    case 7:
        {
            /* Snapshot the ISR-visible value before checking and conversion. */
            float local_voltage = voltage;
            if (local_voltage >= 0.0f && local_voltage < 1000000.0f)
                length = snprintf(response, sizeof(response), "CMD|7|%d|$", (int)(local_voltage * 1000.0f));
        }
        break;
    case 8:
        {
            PID_Params snapshot;
            previous = lock_interrupts();
            if (command.args[0] == 2) {
                pidparams.middle_angle = -4.0f;
                pidparams.balance_kp = 140;
                pidparams.balance_kd = 450;
                pidparams.velocity_kp = 30;
                pidparams.velocity_ki = 80;
                pidparams.turn_kp = 5;
                pidparams.turn_kd = 500;
                /* Preserve the previous reset command's non-persistent behavior. */
            }
            snapshot = pidparams;
            __set_PRIMASK(previous);
            length = snprintf(response, sizeof(response), "CMD|8|%.1f|%d|%d|%d|%d|%d|%d|$",
                              snapshot.middle_angle, snapshot.balance_kp, snapshot.velocity_kp,
                              snapshot.turn_kp, snapshot.balance_kd, snapshot.velocity_ki, snapshot.turn_kd);
        }
        break;
    default: break;
    }
    if (length > 0) (void)submit_frame(0U, response, length);
}

void bluetooth_main_loop_task(void)
{
    BtRxBlock block;
    uint32_t previous;
    unsigned int budget = BT_RX_SLOTS;
    if (rx_recovery != 0U) {
        /* Error callbacks only latch faults. Abort/re-arm remains outside ISR. */
        (void)HAL_UART_AbortReceive(&huart6);
        (void)HAL_UART_AbortTransmit(&huart6);
        previous = lock_interrupts();
        rx_read = rx_write;
        rx_recovery = 0U;
        tx_done = tx_started = tx_pending = 0U;
        __set_PRIMASK(previous);
        bt_parser_reset(&parser);
        bt_tx_init(&tx_queue);
        if (register_callbacks()) (void)start_receive(0U);
    }
    synchronize_receive_epoch();
    while (budget-- != 0U && rx_recovery == 0U) {
        unsigned int i;
        previous = lock_interrupts();
        if (rx_read == rx_write) {
            __set_PRIMASK(previous);
            break;
        }
        block = rx_blocks[rx_read % BT_RX_SLOTS];
        ++rx_read;
        if (block.epoch != motion.epoch) {
            __set_PRIMASK(previous);
            continue;
        }
        __set_PRIMASK(previous);
        for (i = 0U; i < block.length; ++i) {
            if (bt_parser_feed(&parser, block.data[i], block.cycles,
                                block.epoch, motion.timeout_cycles)) {
                process_command(block.cycles);
            }
        }
    }
    service_transmit();
}

void bluetooth_periodic_update(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t previous;
    float right, left, accel, gyro;
    uint16_t distance_value;
    char response[BT_WIRE_MAX + 1U];
    int length;
    if ((uint32_t)(now - last_periodic_cycles) < SystemCoreClock / 10U) return;
    last_periodic_cycles = now;
    previous = lock_interrupts();
    right = velocity_right;
    left = velocity_left;
    distance_value = dis;
    accel = acceleration_y;
    gyro = gyro_turn;
    __set_PRIMASK(previous);
    if (reporting[0] != 0U && right >= (float)INT_MIN && right < (float)INT_MAX &&
        left >= (float)INT_MIN && left < (float)INT_MAX) {
        length = snprintf(response, sizeof(response), "CMD|4|%d|%d|$", (int)right, (int)left);
        (void)submit_frame(1U, response, length);
    }
    if (reporting[1] != 0U) {
        length = snprintf(response, sizeof(response), "CMD|5|%d|$", (int)distance_value);
        (void)submit_frame(2U, response, length);
    }
    if (reporting[2] != 0U && accel == accel && gyro == gyro &&
        accel <= 1.0e30f && accel >= -1.0e30f && gyro <= 1.0e30f && gyro >= -1.0e30f) {
        length = snprintf(response, sizeof(response), "CMD|6|%.1f|%.1f|$", accel, gyro);
        (void)submit_frame(3U, response, length);
    }
    service_transmit();
}
