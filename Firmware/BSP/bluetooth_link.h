#ifndef BLUETOOTH_LINK_H
#define BLUETOOTH_LINK_H

#include <stdint.h>
#include <stddef.h>

/* LINK-001/002/003: ASCII wire size includes CMD| and $, excludes NUL. */
#define BT_WIRE_MAX 128U
#define BT_TIMEOUT_MS 500U
#define BT_QUERY_SLOTS 4U
#define BT_TELEMETRY_SLOTS 3U

typedef struct {
    char data[BT_WIRE_MAX + 1U];
    uint16_t length;
    uint32_t first_cycles;
    uint32_t epoch;
} BtParser;

typedef struct {
    uint8_t code;
    uint8_t argc;
    int32_t args[3];
    float decimal;
} BtCommand;

typedef struct {
    uint32_t timeout_cycles;
    uint32_t last_cycles;
    uint32_t epoch;
    uint8_t normal;
    uint8_t armed;
    int8_t speed;
    int8_t turn;
} BtMotionGuard;

typedef struct {
    uint8_t data[BT_WIRE_MAX];
    uint16_t length;
} BtTxFrame;

typedef struct {
    BtTxFrame queries[BT_QUERY_SLOTS];
    BtTxFrame telemetry[BT_TELEMETRY_SLOTS];
    BtTxFrame active;
    uint8_t read_index;
    uint8_t count;
    uint8_t next_telemetry;
    uint8_t active_valid;
    uint32_t rejected;
    uint32_t coalesced;
} BtTxQueue;

void bt_parser_reset(BtParser *parser);
/* Returns one complete wire frame in parser->data, valid until next feed.
   epoch stays stamped at the first byte, including across epoch transitions;
   callers must enforce freshness for motion and parameter writes. */
uint8_t bt_parser_feed(BtParser *parser, uint8_t byte, uint32_t received_cycles,
                       uint32_t epoch, uint32_t timeout_cycles);
uint8_t bt_decode_command(const char *frame, BtCommand *command);
void bt_guard_init(volatile BtMotionGuard *guard, uint32_t cycles_per_ms, uint8_t normal);
void bt_guard_invalidate(volatile BtMotionGuard *guard);
void bt_guard_tick(volatile BtMotionGuard *guard, uint32_t now_cycles, uint8_t normal);
uint8_t bt_guard_receive(volatile BtMotionGuard *guard, int32_t speed, int32_t turn,
                         uint32_t first_cycles, uint32_t last_cycles,
                         uint32_t frame_epoch, uint32_t now_cycles);
void bt_tx_init(BtTxQueue *queue);
/* channel 0: query FIFO; 1..3: newest CMD4/5/6 telemetry. Main-loop only. */
uint8_t bt_tx_submit(BtTxQueue *queue, uint8_t channel,
                     const uint8_t *data, uint16_t length);
const BtTxFrame *bt_tx_begin(BtTxQueue *queue);
void bt_tx_complete(BtTxQueue *queue);

#endif
