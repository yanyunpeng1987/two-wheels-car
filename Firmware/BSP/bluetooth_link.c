/* Portable HC-05D link framing, validation, motion lease and TX ownership. */
#include "bluetooth_link.h"
#include <string.h>
#include <float.h>
#include <stdlib.h>

void bt_parser_reset(BtParser *parser)
{
    memset(parser, 0, sizeof(*parser));
}

uint8_t bt_parser_feed(BtParser *parser, uint8_t byte, uint32_t received_cycles,
                       uint32_t epoch, uint32_t timeout_cycles)
{
    static const char prefix[] = "CMD|";
    if (parser->length != 0U &&
        (parser->epoch != epoch ||
         (uint32_t)(received_cycles - parser->first_cycles) >= timeout_cycles)) {
        parser->length = 0U;
    }
    /* C cannot occur in any numeric payload. It is an unambiguous resync. */
    if (byte == (uint8_t)'C') {
        parser->length = 1U;
        parser->data[0] = 'C';
        parser->first_cycles = received_cycles;
        parser->epoch = epoch;
        return 0U;
    }
    if (parser->length == 0U) {
        return 0U;
    }
    if (byte < 0x20U || byte > 0x7EU || parser->length >= BT_WIRE_MAX ||
        (parser->length < 4U && byte != (uint8_t)prefix[parser->length])) {
        parser->length = 0U;
        return 0U;
    }
    parser->data[parser->length++] = (char)byte;
    if (byte == (uint8_t)'$') {
        parser->data[parser->length] = '\0';
        parser->length = 0U;
        return 1U;
    }
    return 0U;
}

static uint8_t parse_integer(const char *start, size_t length, int32_t *out)
{
    uint32_t value = 0U;
    uint32_t limit = 2147483647U;
    uint8_t negative = 0U;
    size_t i = 0U;
    if (length == 0U) return 0U;
    if (start[i] == '-' || start[i] == '+') {
        negative = (uint8_t)(start[i] == '-');
        ++i;
    }
    if (i == length) return 0U;
    if (negative != 0U) limit = 2147483648U;
    for (; i < length; ++i) {
        uint32_t digit;
        if (start[i] < '0' || start[i] > '9') return 0U;
        digit = (uint32_t)(start[i] - '0');
        if (value > (limit - digit) / 10U) return 0U;
        value = value * 10U + digit;
    }
    if (negative != 0U) {
        *out = value == 2147483648U ? (-2147483647 - 1) : -(int32_t)value;
    } else {
        *out = (int32_t)value;
    }
    return 1U;
}

static uint8_t parse_decimal(const char *start, size_t length, float *out)
{
    char field[BT_WIRE_MAX + 1U];
    char *end;
    float value;
    size_t i = 0U;
    size_t digits = 0U;
    if (length == 0U || length > BT_WIRE_MAX) return 0U;
    if (start[i] == '-' || start[i] == '+') ++i;
    while (i < length && start[i] >= '0' && start[i] <= '9') { ++i; ++digits; }
    if (i < length && start[i] == '.') {
        ++i;
        while (i < length && start[i] >= '0' && start[i] <= '9') { ++i; ++digits; }
    }
    if (digits == 0U) return 0U;
    if (i < length && (start[i] == 'e' || start[i] == 'E')) {
        size_t exponent_start;
        ++i;
        if (i < length && (start[i] == '-' || start[i] == '+')) ++i;
        exponent_start = i;
        while (i < length && start[i] >= '0' && start[i] <= '9') ++i;
        if (i == exponent_start) return 0U;
    }
    if (i != length) return 0U;
    /* Match Android Float.parseFloat rounding, including finite underflow to 0.
       Grammar above excludes locale whitespace, hex floats, suffixes and NaN. */
    memcpy(field, start, length);
    field[length] = '\0';
    value = strtof(field, &end);
    if (end != field + length || value != value || value > FLT_MAX || value < -FLT_MAX)
        return 0U;
    *out = value;
    return 1U;
}

uint8_t bt_decode_command(const char *frame, BtCommand *command)
{
    const char *position;
    BtCommand decoded;
    size_t length;
    if (frame == NULL || command == NULL) return 0U;
    length = strlen(frame);
    if (length < 6U || length > BT_WIRE_MAX || strncmp(frame, "CMD|", 4U) != 0 ||
        frame[length - 1U] != '$' || frame[4] < '1' || frame[4] > '8') return 0U;
    memset(&decoded, 0, sizeof(decoded));
    decoded.code = (uint8_t)(frame[4] - '0');
    position = frame + 5;
    while (*position != '$') {
        const char *start;
        size_t field_length;
        if (*position++ != '|') return 0U;
        if (*position == '$') break; /* optional final delimiter */
        start = position;
        while (*position != '|' && *position != '$' && *position != '\0') ++position;
        field_length = (size_t)(position - start);
        if (decoded.argc >= 3U || *position == '\0') return 0U;
        if (decoded.code == 1U) {
            if (decoded.argc != 0U || !parse_decimal(start, field_length, &decoded.decimal)) return 0U;
        } else if (!parse_integer(start, field_length, &decoded.args[decoded.argc])) {
            return 0U;
        }
        ++decoded.argc;
    }
    if (position[1] != '\0') return 0U;
    switch (decoded.code) {
    case 1: if (decoded.argc != 1U) return 0U; break;
    case 2:
        if (decoded.argc != 3U || decoded.args[0] < 1 || decoded.args[0] > 3) return 0U;
        break;
    case 3: if (decoded.argc != 2U) return 0U; break;
    case 4: case 5: case 6:
        if (decoded.argc != 1U || decoded.args[0] < 0 || decoded.args[0] > 1) return 0U;
        break;
    case 7:
        if (decoded.argc != 0U) return 0U;
        break;
    case 8:
        if (decoded.argc == 0U) decoded.args[0] = 1;
        else if (decoded.argc != 1U || decoded.args[0] < 1 || decoded.args[0] > 2) return 0U;
        break;
    default: return 0U;
    }
    *command = decoded;
    return 1U;
}

void bt_guard_init(volatile BtMotionGuard *guard, uint32_t cycles_per_ms, uint8_t normal)
{
    memset((void *)guard, 0, sizeof(*guard));
    guard->timeout_cycles = cycles_per_ms * BT_TIMEOUT_MS;
    guard->normal = normal;
    guard->epoch = 1U;
}

void bt_guard_invalidate(volatile BtMotionGuard *guard)
{
    guard->armed = 0U;
    guard->speed = 0;
    guard->turn = 0;
    ++guard->epoch;
}

void bt_guard_tick(volatile BtMotionGuard *guard, uint32_t now_cycles, uint8_t normal)
{
    if (normal != guard->normal) {
        guard->normal = normal;
        bt_guard_invalidate(guard);
    } else if (guard->armed != 0U &&
               (uint32_t)(now_cycles - guard->last_cycles) >= guard->timeout_cycles) {
        bt_guard_invalidate(guard);
    }
}

uint8_t bt_guard_receive(volatile BtMotionGuard *guard, int32_t speed, int32_t turn,
                         uint32_t first_cycles, uint32_t last_cycles,
                         uint32_t frame_epoch, uint32_t now_cycles)
{
    bt_guard_tick(guard, now_cycles, guard->normal);
    if (guard->normal == 0U || frame_epoch != guard->epoch ||
        (uint32_t)(now_cycles - first_cycles) >= guard->timeout_cycles ||
        (uint32_t)(now_cycles - last_cycles) >= guard->timeout_cycles) return 0U;
    if (guard->armed == 0U && (speed != 0 || turn != 0)) return 0U;
    guard->armed = 1U;
    guard->last_cycles = last_cycles;
    guard->speed = speed > 0 ? 1 : (speed < 0 ? -1 : 0);
    guard->turn = turn > 0 ? 1 : (turn < 0 ? -1 : 0);
    return 1U;
}

void bt_tx_init(BtTxQueue *queue)
{
    memset(queue, 0, sizeof(*queue));
}

uint8_t bt_tx_submit(BtTxQueue *queue, uint8_t channel,
                     const uint8_t *data, uint16_t length)
{
    BtTxFrame *frame;
    if (data == NULL || length == 0U || length > BT_WIRE_MAX || channel > 3U) {
        ++queue->rejected;
        return 0U;
    }
    if (channel == 0U) {
        if (queue->count == BT_QUERY_SLOTS) {
            ++queue->rejected;
            return 0U;
        }
        frame = &queue->queries[(queue->read_index + queue->count) % BT_QUERY_SLOTS];
        ++queue->count;
    } else {
        frame = &queue->telemetry[channel - 1U];
        if (frame->length != 0U) ++queue->coalesced;
    }
    memcpy(frame->data, data, length);
    frame->length = length;
    return 1U;
}

const BtTxFrame *bt_tx_begin(BtTxQueue *queue)
{
    unsigned int i;
    if (queue->active_valid != 0U) return &queue->active;
    if (queue->count != 0U) {
        queue->active = queue->queries[queue->read_index];
        queue->read_index = (uint8_t)((queue->read_index + 1U) % BT_QUERY_SLOTS);
        --queue->count;
    } else {
        for (i = 0U; i < BT_TELEMETRY_SLOTS; ++i) {
            unsigned int index = (queue->next_telemetry + i) % BT_TELEMETRY_SLOTS;
            if (queue->telemetry[index].length != 0U) {
                queue->active = queue->telemetry[index];
                queue->telemetry[index].length = 0U;
                queue->next_telemetry = (uint8_t)((index + 1U) % BT_TELEMETRY_SLOTS);
                break;
            }
        }
        if (i == BT_TELEMETRY_SLOTS) return NULL;
    }
    queue->active_valid = 1U;
    return &queue->active;
}

void bt_tx_complete(BtTxQueue *queue)
{
    queue->active_valid = 0U;
}
