/*
    Sylverant Ship Server
    Copyright (C) 2009, 2010, 2011, 2012, 2013 Lawrence Sebald

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

#ifdef HAVE_SSIZE_T
#undef HAVE_SSIZE_T
#endif

#include <gnutls/gnutls.h>

#ifdef HAVE_SSIZE_T
#undef HAVE_SSIZE_T
#endif

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

#define SHIPGATE_PROTO_VER  13

/* New header in protocol version 10 and newer. */
typedef struct shipgate_hdr {
    uint16_t pkt_len;
    uint16_t pkt_type;
    uint8_t version;
    uint8_t reserved;
    uint16_t flags;
} PACKED shipgate_hdr_t;

/* Shipgate connection structure. */
struct shipgate_conn {
    int sock;
    int hdr_read;
    int has_key;

    time_t login_attempt;
    ship_t *ship;

    gnutls_session_t session;

    uint16_t key_idx;

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

/* General error packet. Individual packets can/should extend this base
   structure for more specific instances and to help match requests up with the
   error replies. */
typedef struct shipgate_error {
    shipgate_hdr_t hdr;
    uint32_t error_code;
    uint32_t reserved;
    uint8_t data[0];
} PACKED shipgate_error_pkt;

/* Error packet in reply to character data send or character request */
typedef struct shipgate_cdata_err {
    shipgate_error_pkt base;
    uint32_t guildcard;
    uint32_t slot;
} PACKED shipgate_cdata_err_pkt;

/* Error packet in reply to character backup send or character backup request */
typedef struct shipgate_cbkup_err {
    shipgate_error_pkt base;
    uint32_t guildcard;
    uint32_t block;
} PACKED shipgate_cbkup_err_pkt;

/* Error packet in reply to gm login */
typedef struct shipgate_gm_err {
    shipgate_error_pkt base;
    uint32_t guildcard;
    uint32_t block;
} PACKED shipgate_gm_err_pkt;

/* Error packet in reply to ban */
typedef struct shipgate_ban_err {
    shipgate_error_pkt base;
    uint32_t req_gc;
    uint32_t target;
    uint32_t until;
    uint32_t reserved;
} PACKED shipgate_ban_err_pkt;

/* Error packet in reply to a block login */
typedef struct shipgate_blogin_err {
    shipgate_error_pkt base;
    uint32_t guildcard;
    uint32_t blocknum;
} PACKED shipgate_blogin_err_pkt;

/* Error packet in reply to a add/remove friend */
typedef struct shipgate_friend_err {
    shipgate_error_pkt base;
    uint32_t user_gc;
    uint32_t friend_gc;
} PACKED shipgate_friend_err_pkt;

/* The request sent from the shipgate for a ship to identify itself. */
typedef struct shipgate_login {
    shipgate_hdr_t hdr;
    char msg[45];
    uint8_t ver_major;
    uint8_t ver_minor;
    uint8_t ver_micro;
    uint32_t reserved[2];
} PACKED shipgate_login_pkt;

/* The reply to the login request from the shipgate (with IPv6 support).
   Note that IPv4 support is still required, as PSO itself does not actually
   support IPv6 (however, proxies can alleviate this problem a bit). */
typedef struct shipgate_login6_reply {
    shipgate_hdr_t hdr;
    uint32_t proto_ver;
    uint32_t flags;
    uint8_t name[12];
    uint32_t ship_addr4;                /* IPv4 address (required) */
    uint8_t ship_addr6[16];             /* IPv6 address (optional) */
    uint16_t ship_port;
    uint16_t ship_key;                  /* Reserved in TLS clients */
    uint16_t clients;
    uint16_t games;
    uint16_t menu_code;
    uint8_t reserved[6];                /* Pad to a multiple of 8 bytes */
} PACKED shipgate_login6_reply_pkt;

/* A update of the client/games count. */
typedef struct shipgate_cnt {
    shipgate_hdr_t hdr;
    uint16_t clients;
    uint16_t games;
    uint32_t ship_id;                   /* 0 for ship->gate */
} PACKED shipgate_cnt_pkt;

/* A forwarded player packet. */
typedef struct shipgate_fw {
    shipgate_hdr_t hdr;
    uint32_t ship_id;
    uint32_t fw_flags;
    uint8_t pkt[0];
} PACKED shipgate_fw_pkt;

/* A forwarded player packet (updated in proto v9). */
typedef struct shipgate_fw_9 {
    shipgate_hdr_t hdr;
    uint32_t ship_id;
    uint32_t fw_flags;
    uint32_t guildcard;
    uint32_t block;
    uint8_t pkt[0];
} PACKED shipgate_fw_9_pkt;

/* A packet telling clients that a ship has started or dropped. */
typedef struct shipgate_ship_status {
    shipgate_hdr_t hdr;
    uint8_t name[12];
    uint32_t ship_id;
    uint32_t flags;
    uint32_t ship_addr4;                /* IPv4 address (required) */
    uint8_t ship_addr6[16];             /* IPv6 address (optional) */
    uint16_t ship_port;
    uint16_t status;
    uint16_t clients;
    uint16_t games;
    uint16_t menu_code;
    uint8_t  ship_number;
    uint8_t  reserved[5];
} PACKED shipgate_ship_status_pkt;

/* A packet sent to/from clients to save/restore character data. */
typedef struct shipgate_char_data {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t slot;
    uint32_t block;
    uint8_t data[];
} PACKED shipgate_char_data_pkt;

/* A packet sent from clients to save their character backup or to request that
   the gate send it back to them. */
typedef struct shipgate_char_bkup {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t block;
    uint8_t name[32];
    uint8_t data[];
} PACKED shipgate_char_bkup_pkt;

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

/* Packet used to tell the shipgate that a user has logged into/off a block */
typedef struct shipgate_block_login {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t blocknum;
    char ch_name[32];
} PACKED shipgate_block_login_pkt;

/* Packet to tell a ship that a client's friend has logged in/out */
typedef struct shipgate_friend_login {
    shipgate_hdr_t hdr;
    uint32_t dest_guildcard;
    uint32_t dest_block;
    uint32_t friend_guildcard;
    uint32_t friend_ship;
    uint32_t friend_block;
    uint32_t reserved;
    char friend_name[32];
    char friend_nick[32];
} PACKED shipgate_friend_login_pkt;

/* Packet to delete someone from a user's friendlist */
typedef struct shipgate_friend_upd {
    shipgate_hdr_t hdr;
    uint32_t user_guildcard;
    uint32_t friend_guildcard;
} PACKED shipgate_friend_upd_pkt;

/* Packet to add a user to a friendlist */
typedef struct shipgate_friend_add {
    shipgate_hdr_t hdr;
    uint32_t user_guildcard;
    uint32_t friend_guildcard;
    char friend_nick[32];
} PACKED shipgate_friend_add_pkt;

/* Packet to update a user's lobby in the shipgate's info */
typedef struct shipgate_lobby_change {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t lobby_id;
    char lobby_name[32];
} PACKED shipgate_lobby_change_pkt;

/* Packet to send a list of online clients (for when a ship reconnects to the
   shipgate) */
typedef struct shipgate_block_clients {
    shipgate_hdr_t hdr;
    uint32_t count;
    uint32_t block;
    struct {
        uint32_t guildcard;
        uint32_t lobby;
        uint32_t dlobby;
        uint32_t reserved;
        char ch_name[32];
        char lobby_name[32];
    } entries[0];
} PACKED shipgate_block_clients_pkt;

/* A kick request, sent to or from a ship */
typedef struct shipgate_kick_req {
    shipgate_hdr_t hdr;
    uint32_t requester;
    uint32_t reserved;
    uint32_t guildcard;
    uint32_t block;                     /* 0 for ship->shipgate */
    char reason[64];
} PACKED shipgate_kick_pkt;

/* Packet to send a portion of the user's friend list to the ship, including
   online/offline status. */
typedef struct shipgate_friend_list {
    shipgate_hdr_t hdr;
    uint32_t requester;
    uint32_t block;
    struct {
        uint32_t guildcard;
        uint32_t ship;
        uint32_t block;
        uint32_t reserved;
        char name[32];
    } entries[];
} PACKED shipgate_friend_list_pkt;

/* Packet to request a portion of the friend list be sent */
typedef struct shipgate_friend_list_req {
    shipgate_hdr_t hdr;
    uint32_t requester;
    uint32_t block;
    uint32_t start;
    uint32_t reserved;
} PACKED shipgate_friend_list_req;

/* Packet to send a global message to all ships */
typedef struct shipgate_global_msg {
    shipgate_hdr_t hdr;
    uint32_t requester;
    uint32_t reserved;
    char text[];                        /* UTF-8, padded to 8-byte boundary */
} PACKED shipgate_global_msg_pkt;

/* An individual option for the options packet */
typedef struct shipgate_user_opt {
    uint32_t option;
    uint32_t length;
    uint8_t data[];
} PACKED shipgate_user_opt_t;

/* Packet used to send a user's settings to a ship */
typedef struct shipgate_user_options {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t block;
    uint32_t count;
    uint32_t reserved;
    shipgate_user_opt_t options[];
} PACKED shipgate_user_opt_pkt;

/* Packet used to request Blue Burst options */
typedef struct shipgate_bb_opts_req {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t block;
} PACKED shipgate_bb_opts_req_pkt;

/* Packet used to send Blue Burst options to a user */
typedef struct shipgate_bb_opts {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t block;
    sylverant_bb_db_opts_t opts;
} PACKED shipgate_bb_opts_pkt;

/* Packet used to send an update to the user's monster kill counts. */
typedef struct shipgate_mkill {
    shipgate_hdr_t hdr;
    uint32_t guildcard;
    uint32_t block;
    uint8_t episode;
    uint8_t difficulty;
    uint8_t reserved[2];
    uint32_t counts[0x60];
} PACKED shipgate_mkill_pkt;

#undef PACKED

/* Size of the shipgate login packet. */
#define SHIPGATE_LOGINV0_SIZE       64

/* The requisite message for the msg field of the shipgate_login_pkt. */
static const char shipgate_login_msg[] =
    "Sylverant Shipgate Copyright Lawrence Sebald";

/* Flags for the flags field of shipgate_hdr_t */
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
#define SHDR_TYPE_BLKLOGIN  0x0019      /* User logs into a block */
#define SHDR_TYPE_BLKLOGOUT 0x001A      /* User logs off a block */
#define SHDR_TYPE_FRLOGIN   0x001B      /* A user's friend logs onto a block */
#define SHDR_TYPE_FRLOGOUT  0x001C      /* A user's friend logs off a block */
#define SHDR_TYPE_ADDFRIEND 0x001D      /* Add a friend to a user's list */
#define SHDR_TYPE_DELFRIEND 0x001E      /* Remove a friend from a user's list */
#define SHDR_TYPE_LOBBYCHG  0x001F      /* A user changes lobbies */
#define SHDR_TYPE_BCLIENTS  0x0020      /* A bulk transfer of client info */
#define SHDR_TYPE_KICK      0x0021      /* A kick request */
#define SHDR_TYPE_FRLIST    0x0022      /* Friend list request/reply */
#define SHDR_TYPE_GLOBALMSG 0x0023      /* A Global message packet */
#define SHDR_TYPE_USEROPT   0x0024      /* A user's options -- sent on login */
#define SHDR_TYPE_LOGIN6    0x0025      /* A ship login (potentially IPv6) */
#define SHDR_TYPE_BBOPTS    0x0026      /* A user's Blue Burst options */
#define SHDR_TYPE_BBOPT_REQ 0x0027      /* Request Blue Burst options */
#define SHDR_TYPE_CBKUP     0x0028      /* A character data backup packet */
#define SHDR_TYPE_MKILL     0x0029      /* Monster kill update */

/* Flags that can be set in the login packet */
#define LOGIN_FLAG_GMONLY   0x00000001  /* Only Global GMs are allowed */
#define LOGIN_FLAG_PROXY    0x00000002  /* Is a proxy -- exclude many pkts */
#define LOGIN_FLAG_NOV1     0x00000010  /* Do not allow DCv1 clients */
#define LOGIN_FLAG_NOV2     0x00000020  /* Do not allow DCv2 clients */
#define LOGIN_FLAG_NOPC     0x00000040  /* Do not allow PSOPC clients */
#define LOGIN_FLAG_NOEP12   0x00000080  /* Do not allow PSO Ep1&2 clients */
#define LOGIN_FLAG_NOEP3    0x00000100  /* Do not allow PSO Ep3 clients */
#define LOGIN_FLAG_NOBB     0x00000200  /* Do not allow PSOBB clients */
#define LOGIN_FLAG_NODCNTE  0x00000400  /* Do not allowe DC NTE clients */

/* General error codes */
#define ERR_NO_ERROR            0x00000000
#define ERR_BAD_ERROR           0x80000001

/* Error codes in response to shipgate_login_reply_pkt */
#define ERR_LOGIN_BAD_KEY       0x00000001
#define ERR_LOGIN_BAD_PROTO     0x00000002
#define ERR_LOGIN_BAD_MENU      0x00000003  /* bad menu code (out of range) */
#define ERR_LOGIN_INVAL_MENU    0x00000004  /* menu code not allowed */

/* Error codes in response to game packets */
#define ERR_GAME_UNK_PACKET     0x00000001

/* Error codes in response to a character request */
#define ERR_CREQ_NO_DATA        0x00000001

/* Error codes in response to a gm login */
#define ERR_GMLOGIN_NO_ACC      0x00000001
#define ERR_GMLOGIN_NOT_GM      0x00000002

/* Error codes in response to a ban request */
#define ERR_BAN_NOT_GM          0x00000001
#define ERR_BAN_BAD_TYPE        0x00000002
#define ERR_BAN_PRIVILEGE       0x00000003

/* Error codes in response to a block login */
#define ERR_BLOGIN_INVAL_NAME   0x00000001
#define ERR_BLOGIN_ONLINE       0x00000002

/* Possible values for user options */
#define USER_OPT_QUEST_LANG     0x00000001
#define USER_OPT_ENABLE_BACKUP  0x00000002
#define USER_OPT_GC_PROTECT     0x00000003

/* Possible values for the fw_flags on a forwarded packet */
#define FW_FLAG_PREFER_IPV6     0x00000001  /* Prefer IPv6 on reply */
#define FW_FLAG_IS_PSOPC        0x00000002  /* Client is on PSOPC */

/* Attempt to connect to the shipgate. Returns < 0 on error, returns 0 on
   success. */
int shipgate_connect(ship_t *s, shipgate_conn_t *rv);

/* Reconnect to the shipgate if we are disconnected for some reason. */
int shipgate_reconnect(shipgate_conn_t *conn);

/* Clean up a shipgate connection. */
void shipgate_cleanup(shipgate_conn_t *c);

/* Read data from the shipgate. */
int shipgate_process_pkt(shipgate_conn_t *c);

/* Send any piled up data. */
int shipgate_send_pkts(shipgate_conn_t *c);

/* Send a newly opened ship's information to the shipgate. */
int shipgate_send_ship_info(shipgate_conn_t *c, ship_t *ship);

/* Send a client/game count update to the shipgate. */
int shipgate_send_cnt(shipgate_conn_t *c, uint16_t clients, uint16_t games);

/* Forward a Dreamcast packet to the shipgate. */
int shipgate_fw_dc(shipgate_conn_t *c, const void *dcp, uint32_t flags,
                   ship_client_t *req);

/* Forward a PC packet to the shipgate. */
int shipgate_fw_pc(shipgate_conn_t *c, const void *pcp, uint32_t flags,
                   ship_client_t *req);

/* Forward a Blue Burst packet to the shipgate. */
int shipgate_fw_bb(shipgate_conn_t *c, const void *bbp, uint32_t flags,
                   ship_client_t *req);

/* Send a ping packet to the server. */
int shipgate_send_ping(shipgate_conn_t *c, int reply);

/* Send the shipgate a character data save request. */
int shipgate_send_cdata(shipgate_conn_t *c, uint32_t gc, uint32_t slot,
                        const void *cdata, int len, uint32_t block);

/* Send the shipgate a request for character data. */
int shipgate_send_creq(shipgate_conn_t *c, uint32_t gc, uint32_t slot);

/* Send a GM login request. */
int shipgate_send_gmlogin(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                          const char *username, const char *password);

/* Send a ban request. */
int shipgate_send_ban(shipgate_conn_t *c, uint16_t type, uint32_t requester,
                      uint32_t target, uint32_t until, const char *msg);

/* Send a friendlist update */
int shipgate_send_friend_del(shipgate_conn_t *c, uint32_t user,
                             uint32_t friend_gc);
int shipgate_send_friend_add(shipgate_conn_t *c, uint32_t user,
                             uint32_t friend_gc, const char *nick);

/* Send a block login/logout */
int shipgate_send_block_login(shipgate_conn_t *c, int on, uint32_t user,
                              uint32_t block, const char *name);
int shipgate_send_block_login_bb(shipgate_conn_t *c, int on, uint32_t user,
                                 uint32_t block, const uint16_t *name);

/* Send a lobby change packet */
int shipgate_send_lobby_chg(shipgate_conn_t *c, uint32_t user, uint32_t lobby,
                            const char *lobby_name);

/* Send a full client list */
int shipgate_send_clients(shipgate_conn_t *c);

/* Send a kick packet */
int shipgate_send_kick(shipgate_conn_t *c, uint32_t requester, uint32_t user,
                       const char *reason);

/* Send a friend list request packet */
int shipgate_send_frlist_req(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                             uint32_t start);

/* Send a global message packet */
int shipgate_send_global_msg(shipgate_conn_t *c, uint32_t gc,
                             const char *text);

/* Send a user option update packet */
int shipgate_send_user_opt(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                           uint32_t opt, uint32_t len, const uint8_t *data);

/* Send a request for the user's Blue Burst options */
int shipgate_send_bb_opt_req(shipgate_conn_t *c, uint32_t gc, uint32_t block);

/* Send the user's Blue Burst options to be stored */
int shipgate_send_bb_opts(shipgate_conn_t *c, ship_client_t *cl);

/* Send the shipgate a character data backup request. */
int shipgate_send_cbkup(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                        const char *name, const void *cdata, int len);

/* Send the shipgate a request for character backup data. */
int shipgate_send_cbkup_req(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                            const char *name);

/* Send a monster kill count update */
int shipgate_send_mkill(shipgate_conn_t *c, uint32_t gc, uint32_t block,
                        uint8_t ep, uint8_t d, uint32_t counts[0x60]);

#endif /* !SHIPGATE_H */
