#include "ws_reasm.h"

#include <string.h>

#define OP_CONTINUATION  0x0
#define OP_TEXT          0x1
#define OP_BINARY        0x2
#define OP_CLOSE         0x8

void ws_reasm_reset(ws_reasm_t *r)
{
    r->len = 0;
    r->in_msg = false;
    r->dropping = false;
    r->close_code = 0;
}

ws_reasm_result_t ws_reasm_feed(ws_reasm_t *r, uint8_t op_code, bool fin, int payload_len, int payload_offset,
                                const char *data, int data_len)
{
    if (data == NULL || data_len < 0) {
        data_len = 0;
    }

    if (op_code == OP_CLOSE) {
        if (payload_offset != 0) {
            return WS_REASM_NONE;
        }
        if (data_len >= 2) {
            r->close_code = (uint16_t)(((uint8_t)data[0] << 8) | (uint8_t)data[1]);
        } else {
            r->close_code = WS_REASM_CLOSE_NO_CODE;
        }
        return WS_REASM_CLOSE;
    }
    if (op_code != OP_TEXT && op_code != OP_BINARY && op_code != OP_CONTINUATION) {
        return WS_REASM_NONE;
    }

    if (op_code != OP_CONTINUATION && payload_offset == 0) {
        r->len = 0;
        r->in_msg = true;
        r->dropping = op_code == OP_BINARY;
    }
    if (!r->in_msg) {
        return WS_REASM_NONE;
    }

    if (!r->dropping && r->len + (size_t)data_len <= WS_REASM_CAPACITY) {
        if (data_len > 0) {
            memcpy(r->buf + r->len, data, (size_t)data_len);
        }
        r->len += (size_t)data_len;
    } else {
        r->dropping = true;
    }

    const bool frame_complete = (long)payload_offset + data_len >= payload_len;
    if (!frame_complete || !fin) {
        return WS_REASM_NONE;
    }
    r->in_msg = false;
    return r->dropping ? WS_REASM_DROPPED : WS_REASM_MESSAGE;
}
