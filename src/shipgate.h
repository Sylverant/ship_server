/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License version 3
    as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SHIPGATE_H
#define SHIPGATE_H

#include <time.h>
#include <inttypes.h>
#include <openssl/rc4.h>

/* Forward declarations. */
struct ship;

#ifndef SHIP_DEFINED
#define SHIP_DEFINED
typedef struct ship ship_t;
#endif

#ifdef PACKED
#undef PACKED
#endif

#define PACKED __attribute__((packed))

/* The header that is prepended to any packets sent to the shipgate. */
typedef struct shipgate_hdr {
    uint16_t pkt_len;                   /* Compressed length */
    uint16_t pkt_type;
    uint16_t pkt_unc_len;               /* Uncompressed length */
    uint16_t flags;                     /* Packet flags */
} PACKED shipgate_hdr_t;

/* Shipgate connection structure. */
struct shipgate_conn {
    int sock;
    int hdr_read;
    int has_key;

    time_t login_attempt;

    ship_t *ship;

    uint16_t key_idx;

    RC4_KEY ship_key;
    RC4_KEY gate_key;

    unsigned char *recvbuf;
    int recvbuf_cur;
    int recvbuf_size;
    shipgate_hdr_t pkt;

    unsigned char *sendbuf;
    int sendbuf_cur;
    int sendbuf_size;
    int sendbuf_start;
};

#ifndef SHIPGATE_CONN_DEFINED
#define SHIPGATE_CONN_DEFINED
typedef struct shipgate_conn shipgate_conn_t;
#endif

/* The request sent from the shipgate for a ship to identify itself. */
typedef struct shipgate_login {
    shipgate_hdr_t hdr;
    char msg[45];
    uint8_t ver_major;
    uint8_t ver_minor;
    uint8_t ver_micro;
    uint8_t gate_nonce[4];
    uint8_t ship_nonce[4];
} PACKED shipgate_login_pkt;

/* The reply to the login request from the shipgate. */
typedef struct shipgate_login_reply {
    shipgate_hdr_t hdr;
    char name[12];
    uint32_t ship_addr;
    uint32_t int_addr;
    uint16_t ship_port;
    uint16_t ship_key;
    uint32_t connections;
    uint32_t flags;
} PACKED shipgate_login_reply_pkt;

/* A update of the client/games count. */
typedef struct shipgate_cnt {
    shipgate_hdr_t hdr;
    uint16_t ccnt;
    uint16_t gcnt;
    uint32_t padding;
} PACKED shipgate_cnt_pkt;

/* A forwarded player packet. */
typedef struct shipgate_fw {
    shipgate_hdr_t hdr;
    uint32_t ship_id;
    uint32_t reserved;
    uint8_t pkt[0];
} PACKED shipgate_fw_pkt;

/* A packet telling clients that a ship has started or dropped. */
typedef struct shipgate_ship_status {
    shipgate_hdr_t hdr;
    char name[12];
    uint32_t ship_id;
    uint32_t ship_addr;
    uint32_t int_addr;
    uint16_t ship_port;
    uint16_t status;
    uint32_t flags;
} PACKED shipgate_ship_status_pkt;

/* A packet sent to/from clients to save/restore character data. */
typedef struct shipgate_char_data {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t slot;
    uint32_t padding;
    uint8_t data[1052];
} PACKED shipgate_char_data_pkt;

/* A packet sent to request saved character data. */
typedef struct shipgate_char_req {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t slot;
} PACKED shipgate_char_req_pkt;

/* A packet sent to login a Global GM. */
typedef struct shipgate_gmlogin_req {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t block;
    char username[32];
    char password[32];
} PACKED shipgate_gmlogin_req_pkt;

/* A packet replying to a Global GM login. */
typedef struct shipgate_gmlogin_reply {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t block;
    uint8_t priv;
    uint8_t reserved[7];
} PACKED shipgate_gmlogin_reply_pkt;

/* A packet used to set a ban. */
typedef struct shipgate_ban_req {
    shipgate_hdr_t hdr;
    uint32_t req_gc;
    uint32_t target;
    uint32_t until;
    uint32_t reserved;
    char message[256];
} PACKED shipgate_ban_req_pkt;

#undef PACKED

/* Size of the shipgate login packet. */
#define SHIPGATE_LOGIN_SIZE         64

/* The requisite message for the msg field of the shipgate_login_pkt. */
static const char shipgate_login_msg[] =
    "Sylverant Shipgate Copyright Lawrence Sebald";

/* Flags for the flags field of shipgate_hdr_t */
#define SHDR_NO_DEFLATE     0x0001      /* Packet was not deflate()'d */
#define SHDR_RESPONSE       0x8000      /* Response to a request */
#define SHDR_FAILURE        0x4000      /* Failure to complete request */

/* Types for the pkt_type field of shipgate_hdr_t */
#define SHDR_TYPE_DC        0x0001      /* A decrypted Dreamcast game packet */
#define SHDR_TYPE_BB        0x0002      /* A decrypted Blue Burst game packet */
#define SHDR_TYPE_PC        0x0003      /* A decrypted PCv2 game packet */
#define SHDR_TYPE_GC        0x0004      /* A decrypted Gamecube game packet */
#define SHDR_TYPE_LOGIN     0x0010      /* A login request */
#define SHDR_TYPE_COUNT     0x0011      /* A Client Count update */
#define SHDR_TYPE_SSTATUS   0x0012      /* A Ship has come up or gone down */
#define SHDR_TYPE_PING      0x0013      /* A Ping packet, enough said */
#define SHDR_TYPE_CDATA     0x0014      /* Character data */
#define SHDR_TYPE_CREQ      0x0015      /* Request saved character data */
#define SHDR_TYPE_GMLOGIN   0x0016      /* Login request for a Global GM */
#define SHDR_TYPE_GCBAN     0x0017      /* Guildcard ban */
#define SHDR_TYPE_IPBAN     0x0018      /* IP ban */

/* Flags that can be set in the login packet */
#define LOGIN_FLAG_GMONLY   0x00000001  /* Only Global GMs are allowed */
#define LOGIN_FLAG_PROXY    0x00000002  /* Is a proxy -- exclude many pkts */

/* Attempt to connect to the shipgate. Returns < 0 on error, returns 0 on
   success. */
int shipgate_connect(ship_t *s, shipgate_conn_t *rv);

/* Reconnect to the shipgate if we are disconnected for some reason. */
int shipgate_reconnect(shipgate_conn_t *conn);

/* Read data from the shipgate. */
int shipgate_process_pkt(shipgate_conn_t *c);

/* Send any piled up data. */
int shipgate_send_pkts(shipgate_conn_t *c);

/* Send a newly opened ship's information to the shipgate. */
int shipgate_send_ship_info(shipgate_conn_t *c, ship_t *ship);

/* Send a client/game count update to the shipgate. */
int shipgate_send_cnt(shipgate_conn_t *c, uint16_t ccnt, uint16_t gcnt);

/* Forward a Dreamcast packet to the shipgate. */
int shipgate_fw_dc(shipgate_conn_t *c, void *dcp);

/* Forward a PC packet to the shipgate. */
int shipgate_fw_pc(shipgate_conn_t *c, void *pcp);

/* Send a ping packet to the server. */
int shipgate_send_ping(shipgate_conn_t *c, int reply);

/* Send the shipgate a character data save request. */
int shipgate_send_cdata(shipgate_conn_t *c, uint32_t gc, uint32_t slot,
                        void *cdata);

/* Send the shipgate a request for character data. */
int shipgate_send_creq(shipgate_conn_t *c, uint32_t gc, uint32_t slot);

/* Send a GM login request. */
int shipgate_send_gmlogin(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                          char *username, char *password);

/* Send a ban request. */
int shipgate_send_ban(shipgate_conn_t *c, uint16_t type, uint32_t requester,
                      uint32_t target, uint32_t until, char *msg);

#endif /* !SHIPGATE_H */
