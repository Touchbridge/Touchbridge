
/*
 * Module for passing messages over TCP connections.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>

#include "netbuf.h"


netbuf_decoder_init_fn_t tlv1_dec_init;
netbuf_decoder_free_fn_t tlv1_dec_free;
netbuf_decoder_fn_t tlv1_decode;
netbuf_encoder_fn_t tlv1_encode;

const netbuf_codec_t tlv1_codec = {
    .dec_init_fn = tlv1_dec_init,
    .dec_free_fn = tlv1_dec_free,
    .dec_in_fn = tlv1_decode,
    .enc_fn = tlv1_encode,
};

#define BUF_UNIT                (4096)

void buf_init(buf_t *buf)
{
    buf->buf = NULL;
    buf->size = 0;
    buf->in_ptr = 0;
    buf->out_ptr = 0;
}

void buf_init_from_static(buf_t *buf, uint8_t *data, int size)
{
    buf->buf = data;
    buf->size = size;
    buf->in_ptr = 0;
    buf->out_ptr = 0;
}

void buf_reset(buf_t *buf)
{
    buf->in_ptr = 0;
    buf->out_ptr = 0;
}

void buf_resize(buf_t *buf, int length)
{
    int size = buf->in_ptr + length;
    if (size > buf->size) {
        // Round-up to nearest multiple of BUF_UNIT
        // to save a bunch calls to realloc for
        // small increments in size.
        int new_size = BUF_UNIT * (1 + size / BUF_UNIT);
        buf->buf = realloc(buf->buf, new_size);
        assert(buf->buf != NULL);
        buf->size = new_size;
    }
}

/*
 * Works a bit like read(2) or recv(2) but takes
 * data from buffer.
 */
int buf_get(buf_t *buf, uint8_t *data, int length)
{
    int ret = buf_available_rd(buf);
    assert(ret >= 0);
    ret = (ret > length) ? length : ret;
    if (data)
        memcpy(data, buf->buf + buf->out_ptr, ret);
    buf->out_ptr += ret;
    return ret;
}

void buf_append8(buf_t *buf, uint8_t data)
{
    assert(buf->in_ptr + sizeof(data) <= buf->size);
    buf->buf[buf->in_ptr++] = data;
}

void buf_append32(buf_t *buf, uint32_t data)
{
    assert(buf->in_ptr + sizeof(data) <= buf->size);
    buf->buf[buf->in_ptr++] = (data >>  0) & 0xff;
    buf->buf[buf->in_ptr++] = (data >>  8) & 0xff;
    buf->buf[buf->in_ptr++] = (data >> 16) & 0xff;
    buf->buf[buf->in_ptr++] = (data >> 24) & 0xff;
}

void buf_append(buf_t *buf, uint8_t *data, int length)
{
    assert(buf->in_ptr + length <= buf->size);
    memcpy(buf->buf + buf->in_ptr, data, length);
    buf->in_ptr += length;
}

int _netbuf_send(netbuf_t *nb, uint8_t *data, int size)
{
    return send(nb->socket, data, size, 0);
}

int _netbuf_recv(netbuf_t *nb, uint8_t *data, int size)
{
    return recv(nb->socket, data, size, 0);
}

int buf_is_empty(buf_t *buf)
{
    return (buf->out_ptr == buf->in_ptr);
}

/*
 * Return bytes available for reading from buf.
 */
int buf_available_rd(buf_t *buf)
{
    return (buf->in_ptr - buf->out_ptr);
}

/*
 * Send data from buf. Return -ve on error,
 * zero if data remaining (not all sent)
 * and +ve if all data sent.
 */
int buf_send(netbuf_t *nb, buf_t *buf)
{
    int to_send = buf->in_ptr - buf->out_ptr;
    assert(to_send > 0);
    int ret = _netbuf_send(nb, buf->buf + buf->out_ptr, to_send); 
    if (ret < 0)
        return ret;
    buf->out_ptr += ret;
    return (ret == to_send);
}

/*
 * Receive data into bottom of buf. Return -ve on error,
 * zero if no data available, +ve otherwise.
 */
int buf_recv(int fd, buf_t *buf, int flags)
{
    int ret = recv(fd, buf->buf, buf->size, flags); 
    if (ret < 0)
        return ret;
    buf->in_ptr = ret;
    return ret;
}

netbuf_t *netbuf_new(int socket, const netbuf_codec_t *codec)
{
    netbuf_t *nb = malloc(sizeof(netbuf_t));
    assert(nb != NULL);

    nb->codec = codec;
    if (nb->codec->dec_init_fn)
        nb->codec->dec_init_fn(nb);
    nb->bufsel = 0;
    nb->tx_enable_cb = NULL;
    nb->tx_disable_cb = NULL;
    nb->socket = socket;
    buf_init(nb->txbuf + 0);
    buf_init(nb->txbuf + 1);
    buf_init(&nb->rx_data);
    return nb;
}

void netbuf_free(netbuf_t *nb)
{
    free(nb->txbuf[0].buf);
    free(nb->txbuf[1].buf);
    free(nb->rx_data.buf);
    if (nb->codec && nb->codec->dec_free_fn) {
        nb->codec->dec_free_fn(nb);
    }
    free(nb);
}

/*
 * Attempt to send as much data as possible from netbuf.
 */
int netbuf_send(netbuf_t *nb)
{
    int ret = 1;

    buf_t *non_active_buf = nb->txbuf + ((~nb->bufsel) & 1);
    buf_t *active_buf = nb->txbuf + (nb->bufsel & 1);

    // Try to send 'non-active' buffer first, i.e. the one we're
    // not currently feeding data into via netbuf_tx().
    if (!buf_is_empty(non_active_buf)) {
        ret = buf_send(nb, non_active_buf);
        if (ret < 0)
            return ret;
    }

    if (ret) {
        // Non-active buffer is now empty, so swap the buffers.
        nb->bufsel ^= 1;
        buf_reset(non_active_buf);
        if (!buf_is_empty(active_buf)) {
            ret = buf_send(nb, active_buf);
            if (ret < 0)
                return ret;
        }
    }

    // If ret is still 1, there is no more data to send, so call
    // the tx disable callback - this will normally ensure
    // that our socket descriptor gets removed from a poll/select
    // loop for writeable notification.
    if (ret && nb->tx_disable_cb) {
        nb->tx_disable_cb(nb, nb->cb_data);
    }

    return ret;
}

void netbuf_set_tx_callbacks(netbuf_t *nb, netbuf_callback_t *enable_cb, netbuf_callback_t *disable_cb, void *cb_data)
{
    nb->tx_enable_cb = enable_cb;
    nb->tx_disable_cb = disable_cb;
    nb->cb_data = cb_data;
}

void netbuf_add_msg(netbuf_t *nb, uint8_t type, uint8_t *data, int length)
{
    assert(nb != NULL);
    assert(length >= 0);
    assert(nb->codec != NULL);
    assert(nb->codec->enc_fn != NULL);

    // Select buffer
    buf_t *buf = nb->txbuf + (nb->bufsel & 1);

    // Ensure buffer is big enough
    int enc_len = nb->codec->enc_fn(NULL, type, data, length);
    buf_resize(buf, enc_len);

    // Encode
    nb->codec->enc_fn(buf, type, data, length);

    // Run callback to enable tx  - this will normally ensure
    // that our socket descriptor gets added to a poll/select
    // loop for writable (POLLOUT) notification.
    if (nb->tx_enable_cb) {
        nb->tx_enable_cb(nb, nb->cb_data);
    }
}

/*
 * Given a buf_t containing some data, run the message decoder until we
 * get a valid message (returns 1) or runs out of data (returns 0).
 * If we return 1, the message can be retrieved from nb using
 * the NETBUF_GET_* macros.
 */
int netbuf_decode(netbuf_t *nb, buf_t *buf)
{
    assert(nb != NULL);
    assert(nb->codec != NULL);
    assert(nb->codec->dec_in_fn != NULL);

    return nb->codec->dec_in_fn(nb, buf);
}


/******************************************************************************
 *  TLV 1 Codec
 ******************************************************************************/

/*
 * Intro:
 * TLV1 Codec is to be used for reliable 8-bit-clean stream-oriented
 * channels, specifically TCP.
 *
 * It is a fairly simple Type,Length,Value encoding as it doesn't need to
 * provide for explicit framing as we can assume underlying channel is 100%
 * reliable and doesn't ever miss any data or insert any unexpected data.
 * If something goes wrong, it is assumed connection will be torn-down and
 * set up again. In other words, we won't ever get out of sync.
 *
 * TLV1 header has two formats: short and extended.
 *
 *  -----------------
 * |   1    |   2    |
 *  -----------------
 * |  Type  | Length |
 *  -----------------
 *
 *  ------------------------------------------------------
 * |   1    |   2    |   3    |    4   |   5    |    6    |
 *  ------------------------------------------------------
 * |  Type  |  0xff  |             Length                 |
 *  ------------------------------------------------------
 * 
 */
typedef struct __attribute__((__packed__)) {
    uint8_t type;
    uint8_t length;
    uint32_t ext_length;
} tlv1_hdr_t;

typedef struct {
    int state;
    tlv1_hdr_t hdr;
    int ptr;
} tlv1_decoder_t;

enum {
    TLV1_DEC_STATE_HEADER,
    TLV1_DEC_STATE_HEADER_EXT,
    TLV1_DEC_STATE_DATA,
};

#define TLV1_HDR_LEN_SHORT    (2)
#define TLV1_HDR_LEN_EXT      (6)


void tlv1_dec_init(netbuf_t *nb)
{
    tlv1_decoder_t *dec = malloc(sizeof(tlv1_decoder_t));
    nb->dec_state = dec;
    dec->state = TLV1_DEC_STATE_HEADER;
    dec->ptr = 0;
}

void tlv1_dec_free(netbuf_t *nb)
{
    tlv1_decoder_t *dec = nb->dec_state;
    free(dec);
}

int do_recv(buf_t *buf, uint8_t *data, int len, int *ptr)
{
    assert(ptr != NULL);
    assert(*ptr <= len);
    int rem = len - *ptr;
    int recvd = buf_get(buf, data + *ptr, rem);
    if (recvd < 0) return recvd;
    assert(recvd <= rem);
    *ptr += recvd;
    if (recvd == rem)
        return 1;
    else
        return 0;
}

int tlv1_decode(netbuf_t *nb, buf_t *buf)
{
    tlv1_decoder_t *dec = nb->dec_state;

    if (dec->state == TLV1_DEC_STATE_HEADER) {
        // Get short header
        int ret = do_recv(buf, (uint8_t *)&dec->hdr, TLV1_HDR_LEN_SHORT, &dec->ptr);

        // Not enough header recv'd yet
        if (ret == 0)
            return 0;

        nb->rx_type = dec->hdr.type;
        if (dec->hdr.length == 0xff) {
            dec->state = TLV1_DEC_STATE_HEADER_EXT;
            // Don't reset pointer
        } else {
            dec->state = TLV1_DEC_STATE_DATA;
            nb->rx_len = dec->hdr.length;
            buf_resize(&nb->rx_data, nb->rx_len);
            dec->ptr = 0;
        }
        // We can drop through to next state
    }

    if (dec->state == TLV1_DEC_STATE_HEADER_EXT) {
        // Get short header
        int ret = do_recv(buf, (uint8_t *)&dec->hdr, TLV1_HDR_LEN_EXT, &dec->ptr);

        // Not enough header recv'd yet
        if (ret == 0)
            return 0;

        nb->rx_len = dec->hdr.ext_length;
        buf_resize(&nb->rx_data, nb->rx_len);
        dec->state = TLV1_DEC_STATE_DATA;
        dec->ptr = 0;
        // We can drop through to next state
    }

    if (dec->state == TLV1_DEC_STATE_DATA) {
        // Receiving data
        int ret = do_recv(buf, nb->rx_data.buf, nb->rx_len, &dec->ptr);

        // Not enough data recv'd yet
        if (ret == 0)
            return 0;

        dec->state = TLV1_DEC_STATE_HEADER;
        dec->ptr = 0;
        return 1;
    }


    // We shouldn't ever reach here unless we get into
    // a mystery state or user calls this fn in error
    // state.
    assert(0);

    return 0;
}

int tlv1_encode(buf_t *buf, uint8_t type, uint8_t *data, int length)
{
    int hdr_len = (length >= 255) ?  TLV1_HDR_LEN_EXT : TLV1_HDR_LEN_SHORT;

    if (buf != NULL) {

        // Add header
        buf_append8(buf, type);

        if (length >= 255) {
            buf_append8(buf, 255);
            buf_append32(buf, length);
        } else {
            buf_append8(buf, length);
        }

        // Add data
        buf_append(buf, data, length);
    }

    return hdr_len + length;
}

