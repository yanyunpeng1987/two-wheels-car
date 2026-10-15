#include "bluetooth_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <float.h>

static unsigned int feed(BtParser *p, const char *s, uint32_t now, uint32_t epoch)
{
    unsigned int count = 0U;
    while (*s != '\0') count += bt_parser_feed(p, (uint8_t)*s++, now, epoch, 500U);
    return count;
}

static void test_decode(void)
{
    BtCommand command;
    const char *valid[] = {
        "CMD|1|-4.0|$", "CMD|2|1|140|450|$", "CMD|3|0|0|$",
        "CMD|3|-2147483648|2147483647$", "CMD|4|0|$", "CMD|5|1$",
        "CMD|6|1|$", "CMD|7|$", "CMD|8|$", "CMD|8|1|$", "CMD|8|2|$",
        "CMD|1|1e0|$", "CMD|1|1e-2|$", "CMD|1|+.5E+1|$", "CMD|1|1.e2|$",
        "CMD|1|3.4028235e38|$", "CMD|1|-3.4028235e38|$", "CMD|1|1e-45|$",
        "CMD|1|1e-99999999999999999999999999999|$", "CMD|1|0e99999999999999999999999999999|$"
    };
    const char *invalid[] = {
        "CMD|3|1|$", "CMD|3|1|0|9|$", "CMD|3|1x|0|$", "CMD|3||0|$",
        "CMD|3|2147483648|0|$", "CMD|3|-2147483649|0|$", "CMD|3|1.0|0|$",
        "CMD|3| 1|0|$", "CMD|3|+|0|$", "CMD|3|1|0|$$", "CMD|03|1|0|$",
        "CMD|1|nan|$", "CMD|1|inf|$", "CMD|1|.|$",
        "CMD|1|1e|$", "CMD|1|1e+|$", "CMD|1|1e-1x|$", "CMD|1|1e1e1|$",
        "CMD|1|1e999999999999999999999999999999|$", "CMD|1|-1e9999|$",
        "CMD|1|3.4028236e38|$", "CMD|1|-3.4028236e38|$", "CMD|1|0x1p0|$", "CMD|1|1f|$",
        "CMD|1|99999999999999999999999999999999999999999|$",
        "CMD|4|2|$", "CMD|8|3|$", "CMD|2|4|1|1|$", "CMD|9|$", "CMD|7||$", "CMD|7|1|$"
    };
    unsigned int i;
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
        assert(bt_decode_command(valid[i], &command));
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!bt_decode_command(invalid[i], &command));
    assert(bt_decode_command("CMD|1|-4.25|$", &command) && command.decimal == -4.25f);
    assert(bt_decode_command("CMD|1|1e0|$", &command) && command.decimal == 1.0f);
    assert(bt_decode_command("CMD|1|1e-2|$", &command) && command.decimal == 0.01f);
    assert(bt_decode_command("CMD|1|3.4028235e38|$", &command) && command.decimal == FLT_MAX);
    assert(bt_decode_command("CMD|1|1e-99999|$", &command) && command.decimal == 0.0f);
    assert(bt_decode_command("CMD|8|$", &command) && command.args[0] == 1);
    assert(bt_decode_command("CMD|3|-2147483648|2147483647|$", &command));
    assert(command.args[0] == (-2147483647 - 1) && command.args[1] == 2147483647);
}

static void test_parser(void)
{
    const char *wire = "CMD|3|-1|1|$";
    size_t split, i;
    BtParser parser;
    BtCommand command;
    char maximum[BT_WIRE_MAX + 2U];
    for (split = 0; split <= strlen(wire); ++split) {
        unsigned int count = 0;
        bt_parser_reset(&parser);
        for (i = 0; i < strlen(wire); ++i)
            count += bt_parser_feed(&parser, (uint8_t)wire[i], i < split ? 10U : 20U, 1U, 500U);
        assert(count == 1U && strcmp(parser.data, wire) == 0);
    }
    bt_parser_reset(&parser);
    assert(feed(&parser, "noiseCMD|7|$CMD|8|1|$", 1U, 1U) == 2U);
    assert(feed(&parser, "CMD|3|1", 2U, 1U) == 0U);
    assert(!bt_parser_feed(&parser, 0U, 3U, 1U, 500U));
    assert(feed(&parser, "|0|$CMD|7|$", 4U, 1U) == 1U);
    assert(feed(&parser, "CMD|3|1", 5U, 1U) == 0U);
    assert(feed(&parser, "|0|$", 505U, 1U) == 0U);
    assert(feed(&parser, "CMD|3|0", 510U, 1U) == 0U);
    assert(feed(&parser, "|0|$", 511U, 2U) == 1U);
    /* Parsing may finish across an epoch, but its old identity cannot be lost. */
    assert(parser.epoch == 1U && parser.first_cycles == 510U);
    assert(bt_decode_command(parser.data, &command) && command.code == 3U);
    assert(feed(&parser, "CMD|xCMD|7|$", 512U, 2U) == 1U);
    memcpy(maximum, "CMD|3|", 6U);
    memset(maximum + 6U, '0', BT_WIRE_MAX - 10U);
    memcpy(maximum + BT_WIRE_MAX - 4U, "|0|$", 4U);
    maximum[BT_WIRE_MAX] = '\0';
    assert(feed(&parser, maximum, 520U, 2U) == 1U);
    assert(bt_decode_command(parser.data, &command));
    memmove(maximum + 7U, maximum + 6U, BT_WIRE_MAX - 5U);
    maximum[6] = '0';
    assert(feed(&parser, maximum, 530U, 2U) == 0U);
    assert(feed(&parser, "CMD|7|$", 531U, 2U) == 1U);
}

static void test_parser_epoch_identity(void)
{
    const char *frames[] = { "CMD|3|0|0|$", "CMD|1|-4.0|$", "CMD|2|1|140|450|$",
                             "CMD|8|2|$", "CMD|7|$", "CMD|8|1|$", "CMD|4|1|$" };
    BtParser parser;
    unsigned int f;
    size_t split, i;
    for (f = 0U; f < sizeof(frames) / sizeof(frames[0]); ++f) {
        for (split = 1U; split < strlen(frames[f]); ++split) {
            unsigned int count = 0U;
            bt_parser_reset(&parser);
            for (i = 0U; i < strlen(frames[f]); ++i)
                count += bt_parser_feed(&parser, (uint8_t)frames[f][i],
                                        i < split ? 499U : 501U,
                                        i < split ? 1U : 2U, 500U);
            assert(count == 1U && strcmp(parser.data, frames[f]) == 0);
            assert(parser.epoch == 1U && parser.first_cycles == 499U);
        }
    }
    bt_parser_reset(&parser);
    assert(feed(&parser, "CMD|3|0|", 1U, 1U) == 0U);
    assert(feed(&parser, "CMD|7|$", 2U, 2U) == 1U);
    assert(parser.epoch == 2U && parser.first_cycles == 2U);
    bt_parser_reset(&parser);
    assert(feed(&parser, "CMD|7", UINT32_MAX - 100U, 1U) == 0U);
    assert(feed(&parser, "|$", 399U, 2U) == 0U); /* 500ms framing age, including wrap. */
}

static void test_guard(void)
{
    BtMotionGuard guard;
    uint32_t epoch;
    bt_guard_init(&guard, 1U, 1U);
    epoch = guard.epoch;
    assert(!bt_guard_receive(&guard, 1, 0, 0U, 0U, epoch, 0U));
    assert(bt_guard_receive(&guard, 0, 0, 1U, 1U, epoch, 1U));
    assert(bt_guard_receive(&guard, -100, 100, 2U, 2U, epoch, 2U));
    assert(guard.speed == -1 && guard.turn == 1);
    bt_guard_tick(&guard, 501U, 1U);
    assert(guard.armed && guard.speed == -1);
    bt_guard_tick(&guard, 502U, 1U);
    assert(!guard.armed && guard.speed == 0 && guard.turn == 0);
    assert(!bt_guard_receive(&guard, 0, 0, 500U, 500U, epoch, 503U));
    assert(!bt_guard_receive(&guard, 1, 1, 504U, 504U, guard.epoch, 504U));
    assert(!bt_guard_receive(&guard, 0, 0, 1U, 504U, guard.epoch, 504U));
    assert(bt_guard_receive(&guard, 0, 0, 505U, 505U, guard.epoch, 505U));
    assert(bt_guard_receive(&guard, 1, 0, 506U, 506U, guard.epoch, 506U));
    bt_guard_tick(&guard, 507U, 0U);
    assert(!guard.armed && guard.speed == 0);
    assert(!bt_guard_receive(&guard, 0, 0, 508U, 508U, guard.epoch, 508U));
    bt_guard_tick(&guard, 509U, 1U);
    assert(!bt_guard_receive(&guard, 1, 0, 510U, 510U, guard.epoch, 510U));
    assert(bt_guard_receive(&guard, 0, 0, 511U, 511U, guard.epoch, 511U));
    bt_guard_invalidate(&guard);
    assert(!guard.armed);
    assert(bt_guard_receive(&guard, 0, 0, UINT32_MAX - 100U, UINT32_MAX - 100U,
                            guard.epoch, UINT32_MAX - 100U));
    assert(bt_guard_receive(&guard, 1, -1, UINT32_MAX - 99U, UINT32_MAX - 99U,
                            guard.epoch, UINT32_MAX - 99U));
    bt_guard_tick(&guard, 399U, 1U);
    assert(guard.armed);
    bt_guard_tick(&guard, 400U, 1U);
    assert(!guard.armed);
    bt_guard_init(&guard, 84000U, 1U);
    assert(guard.timeout_cycles == 42000000U);
}

static void test_tx(void)
{
    BtTxQueue queue;
    const BtTxFrame *active;
    unsigned int i;
    char mutable[16] = "CMD|7|1|$";
    bt_tx_init(&queue);
    assert(bt_tx_submit(&queue, 1U, (const uint8_t *)"old", 3U));
    assert(bt_tx_submit(&queue, 1U, (const uint8_t *)"new", 3U));
    assert(queue.coalesced == 1U);
    assert(bt_tx_submit(&queue, 0U, (const uint8_t *)mutable, 9U));
    active = bt_tx_begin(&queue);
    assert(active != NULL && active->length == 9U && memcmp(active->data, mutable, 9U) == 0);
    memset(mutable, 'X', 9U);
    for (i = 0; i < BT_QUERY_SLOTS; ++i)
        assert(bt_tx_submit(&queue, 0U, (const uint8_t *)"q", 1U));
    assert(!bt_tx_submit(&queue, 0U, (const uint8_t *)"x", 1U));
    assert(memcmp(active->data, "CMD|7|1|$", 9U) == 0);
    assert(bt_tx_begin(&queue) == active);
    bt_tx_complete(&queue);
    for (i = 0; i < BT_QUERY_SLOTS; ++i) {
        assert(bt_tx_begin(&queue)->data[0] == 'q');
        bt_tx_complete(&queue);
    }
    assert(memcmp(bt_tx_begin(&queue)->data, "new", 3U) == 0);
    bt_tx_complete(&queue);
    assert(bt_tx_begin(&queue) == NULL);
    assert(!bt_tx_submit(&queue, 4U, (const uint8_t *)"x", 1U));
    assert(!bt_tx_submit(&queue, 0U, (const uint8_t *)"x", 129U));
}

int main(void)
{
    test_decode();
    test_parser();
    test_parser_epoch_identity();
    test_guard();
    test_tx();
    puts("bluetooth_link: parser, strict values, lease/re-arm and TX ownership passed");
    return 0;
}
