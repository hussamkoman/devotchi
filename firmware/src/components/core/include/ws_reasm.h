#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Largest message kept (protocol section 11); longer ones are dropped whole. */
#define WS_REASM_CAPACITY       4096
/** Close code reported for a close frame without a status (RFC 6455 section 7.4.1). */
#define WS_REASM_CLOSE_NO_CODE  1005

typedef enum {
    WS_REASM_NONE,
    /** A complete text message is in buf[0..len). */
    WS_REASM_MESSAGE,
    /** A complete message was discarded: binary, or longer than WS_REASM_CAPACITY. */
    WS_REASM_DROPPED,
    /** The server sent a close frame; close_code holds its status. */
    WS_REASM_CLOSE,
} ws_reasm_result_t;

typedef struct {
    char buf[WS_REASM_CAPACITY];
    size_t len;
    bool in_msg;
    bool dropping;
    uint16_t close_code;
} ws_reasm_t;

void ws_reasm_reset(ws_reasm_t *r);

/**
 * Feeds one read of the WebSocket client. payload_len and payload_offset
 * describe the current frame, not the message: a message is a text frame plus
 * its continuation frames up to the one with fin set, and each frame can
 * arrive in several reads.
 */
ws_reasm_result_t ws_reasm_feed(ws_reasm_t *r, uint8_t op_code, bool fin, int payload_len, int payload_offset,
                                const char *data, int data_len);
