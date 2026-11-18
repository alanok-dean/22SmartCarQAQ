/**
 * @file    proto.h
 * @brief   M4-4 上下位机二进制协议（STM32 端）
 *
 * 契约见 docs/protocol_v1.0.md：
 *   SOF(2) | VER(1) | LEN(2,LE) | MSG(1) | SEQ(1) | PAYLOAD(LEN) | CRC16(2,LE)
 *   CRC16-CCITT-FALSE，覆盖 VER..PAYLOAD 末尾，不含 SOF 与 CRC 自身。
 *   多字节一律小端。LEN 上限 64。
 */
#ifndef __PROTO_H
#define __PROTO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PROTO_SOF1          0xAAu
#define PROTO_SOF2          0x55u
#define PROTO_VER           0x01u
#define PROTO_LEN_MAX       64u
#define PROTO_STATUS_MS     50u

#define MSG_CMD_SPEED       0x01u
#define MSG_STATUS          0x02u

/* 解析器状态 */
#define PST_SOF1            0u
#define PST_SOF2            1u
#define PST_BODY            2u
#define PST_CRC_LO          3u
#define PST_CRC_HI          4u

typedef struct {
    uint8_t  msg_id;
    uint8_t  seq;
    uint16_t len;
    uint8_t  payload[PROTO_LEN_MAX];
} proto_frame_t;

typedef struct {
    uint8_t       state;
    uint8_t       body[5 + PROTO_LEN_MAX];  /* VER LEN(2) MSG SEQ PAYLOAD */
    uint16_t      blen;
    uint16_t      expect_len;               /* LEN 字段的值 */
    uint16_t      crc_calc;                 /* CRC_LO 阶段算好，CRC_HI 复用 */
    proto_frame_t frame;
    uint32_t      good_cnt;
    uint32_t      bad_cnt;
} proto_ctx_t;

void     proto_init(proto_ctx_t *ctx);

/* 逐字节喂入。返回 1 表示凑出一帧完整且 CRC 正确的帧（ctx->frame 有效）。 */
int      proto_feed(proto_ctx_t *ctx, uint8_t byte);

uint16_t proto_crc16(const uint8_t *data, uint16_t len);

/* 组帧到 out，返回总长度；out_max 不足返回 0。 */
uint16_t proto_build(uint8_t msg_id, uint8_t seq,
                     const uint8_t *payload, uint16_t len,
                     uint8_t *out, uint16_t out_max);

/* ---- 串口收发（USART1，115200 8N1） ---- */
void     proto_uart_start(void);            /* 启动单字节中断接收 */
uint8_t  proto_uart_pop(uint8_t *byte);     /* 从环形缓冲取 1 字节，1=成功 */
void     proto_send_status(uint16_t l_rpm, uint16_t r_rpm,
                           int16_t pitch_x10, uint16_t heartbeat,
                           uint16_t bad_cnt);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_H */
