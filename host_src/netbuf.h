#ifndef NETBUF_H
#define NETBUF_H

struct netbuf_s;

typedef struct buf_s {
    uint8_t *buf;
    int size;           // Allocated size of buf
    int in_ptr;         // Index of next free byte to write to
    int out_ptr;        // Index of next byte to read from
} buf_t;

void buf_init(buf_t *buf);
void buf_init_from_static(buf_t *buf, uint8_t *data, int size);
void buf_reset(buf_t *buf);
void buf_resize(buf_t *buf, int length);
int buf_get(buf_t *buf, uint8_t *data, int length);
void buf_append8(buf_t *buf, uint8_t data);
void buf_append32(buf_t *buf, uint32_t data);
void buf_append(buf_t *buf, uint8_t *data, int length);
int buf_is_empty(buf_t *buf);
int buf_available_rd(buf_t *buf);
int buf_recv(int fd, buf_t *buf, int flags);


/*
 * Initialise state variable for a decoder.
 */
typedef void (netbuf_decoder_init_fn_t)(struct netbuf_s *);

/*
 * Destroy state variable for a decoder.
 */
typedef void (netbuf_decoder_free_fn_t)(struct netbuf_s *);

/*
 * Given some data in a buf_t, run it through the decoder.
 * Return +ve as soon as a message has been decoded, otherwise
 * return 0.
 * Important: in the even that there is not enough data to
 * decode a message, the decoder must sitll consume it all and
 * do something with it i.e. it mustn't exit zero with any
 * data left in the buf_t since we can't deal with it as
 * buf_t is not a ring buffer.
 */
typedef int (netbuf_decoder_fn_t)(struct netbuf_s *, buf_t *buf);

/*
 * Given a type, length and value, encoder them into supplied buf_t.
 * Return number of octets added to buf. If buf is NULL, just return
 * number of octets which would be encoded.
 */
typedef int (netbuf_encoder_fn_t)(buf_t *, uint8_t type, uint8_t *data, int length);

typedef struct netbuf_codec_s {
    netbuf_decoder_init_fn_t *dec_init_fn;
    netbuf_decoder_free_fn_t *dec_free_fn;
    netbuf_decoder_fn_t *dec_in_fn;
    netbuf_encoder_fn_t *enc_fn;
} netbuf_codec_t;

extern const netbuf_codec_t tlv1_codec;

typedef int (netbuf_callback_t)(struct netbuf_s *, void *);

typedef struct netbuf_s {
    const netbuf_codec_t *codec;    // Codec
    void *dec_state;    // State data for decoder.
    int rx_len;         // Length of rx'd message
    int rx_type;        // Type of rx'd message.
    buf_t rx_data;      // Buffer for rx'd message's data.
    buf_t txbuf[2];     // Transmit buffers for double-buffering
    int bufsel;         // Which tx buffer is in use (bit 0).
    int socket;         // Network socket descriptor.
    netbuf_callback_t *tx_enable_cb;    // Tx Enable Callback
    netbuf_callback_t *tx_disable_cb;   // Tx Disable Callback
    void *cb_data;      // User data for above.
} netbuf_t;

#define NETBUF_GET_TYPE(mb)  ((mb)->rx_type)
#define NETBUF_GET_LENGTH(mb)  ((mb)->rx_len)
#define NETBUF_GET_DATA(mb)  ((mb)->rx_data.buf)

netbuf_t *netbuf_new(int socket, const netbuf_codec_t *codec);
void netbuf_free(netbuf_t *nb);
void netbuf_add_msg(netbuf_t *nb, uint8_t type, uint8_t *data, int length);
int netbuf_send(netbuf_t *nb);
int netbuf_recv(netbuf_t *nb, uint8_t *data, int size, int *type, int *len);
int netbuf_decode(netbuf_t *nb, buf_t *buf);

#endif //NETBUF_H
