/**
 * @file    proto.c
 * @brief   M4-4 协议实现：CRC16、逐字节状态机、组帧、串口收发
 *
 * 设计要点（答辩会问）：
 *  1. 中断里只做「收 1 字节进环形缓冲」，不做解析 —— 中断越短越好。
 *  2. 解析在主循环里做，逐字节驱动，因此插脏字节最多丢 1 帧就重同步。
 *  3. CRC 覆盖 VER..PAYLOAD 末尾，与上位机 frame_gen.py 完全一致。
 *  4. LEN > 64 直接丢弃并计数，不按 LEN 读缓冲区，避免越界。
 */
#include "proto.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

/* ===================== CRC16 / CCITT-FALSE ===================== */
uint16_t proto_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t  b;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ============================ 组帧 ============================ */
uint16_t proto_build(uint8_t msg_id, uint8_t seq,
                     const uint8_t *payload, uint16_t len,
                     uint8_t *out, uint16_t out_max)
{
    uint16_t total = (uint16_t)(2u + 5u + len + 2u);
    uint16_t crc;
    uint16_t i;

    if (out == 0 || len > PROTO_LEN_MAX || total > out_max) {
        return 0u;
    }

    out[0] = PROTO_SOF1;
    out[1] = PROTO_SOF2;
    out[2] = PROTO_VER;
    out[3] = (uint8_t)(len & 0xFFu);          /* LEN 小端 */
    out[4] = (uint8_t)((len >> 8) & 0xFFu);
    out[5] = msg_id;
    out[6] = seq;
    for (i = 0; i < len; i++) {
        out[7 + i] = payload[i];
    }

    crc = proto_crc16(&out[2], (uint16_t)(5u + len));   /* 从 VER 起 */
    out[7 + len]      = (uint8_t)(crc & 0xFFu);         /* CRC 小端 */
    out[7 + len + 1u] = (uint8_t)((crc >> 8) & 0xFFu);
    return total;
}

/* ========================== 状态机 ============================ */
void proto_init(proto_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = PST_SOF1;
}

int proto_feed(proto_ctx_t *ctx, uint8_t byte)
{
    switch (ctx->state) {
    case PST_SOF1:
        if (byte == PROTO_SOF1) {
            ctx->state = PST_SOF2;
        }
        /* 其它字节：噪声，静默丢弃，不计数 */
        return 0;

    case PST_SOF2:
        if (byte == PROTO_SOF2) {
            ctx->state = PST_BODY;
            ctx->blen  = 0;
            ctx->expect_len = 0xFFFFu;      /* 未读到 LEN */
        } else {
            ctx->bad_cnt++;                  /* 有 0xAA 但没有 0x55 */
            ctx->state = (byte == PROTO_SOF1) ? PST_SOF2 : PST_SOF1;
        }
        return 0;

    case PST_BODY:
        /* body 依次为 VER LEN_LO LEN_HI MSG SEQ PAYLOAD... */
        if (ctx->blen >= sizeof(ctx->body)) {
            ctx->bad_cnt++;
            ctx->state = PST_SOF1;
            return 0;
        }
        ctx->body[ctx->blen++] = byte;

        if (ctx->blen == 2u) {               /* 刚读到 VER 和 LEN_LO */
            if (ctx->body[0] != PROTO_VER) {
                ctx->bad_cnt++;
                ctx->state = PST_SOF1;
                return 0;
            }
        }
        if (ctx->blen == 3u) {               /* LEN 到齐：小端 */
            ctx->expect_len = (uint16_t)ctx->body[1]
                            | ((uint16_t)ctx->body[2] << 8);
            if (ctx->expect_len > PROTO_LEN_MAX) {
                ctx->bad_cnt++;              /* 长度上限保护 */
                ctx->state = PST_SOF1;
                return 0;
            }
        }
        if (ctx->expect_len != 0xFFFFu &&
            ctx->blen == (uint16_t)(5u + ctx->expect_len)) {
            ctx->state = PST_CRC_LO;
        }
        return 0;

    case PST_CRC_LO:
        if (ctx->blen > sizeof(ctx->body)) {
            ctx->bad_cnt++;
            ctx->state = PST_SOF1;
            return 0;
        }
        ctx->crc_calc = proto_crc16(ctx->body, ctx->blen);
        if ((uint8_t)(ctx->crc_calc & 0x00FFu) != byte) {
            ctx->bad_cnt++;
            ctx->state = PST_SOF1;
            return 0;
        }
        ctx->state = PST_CRC_HI;
        return 0;

    case PST_CRC_HI:
        if ((uint8_t)(ctx->crc_calc >> 8) != byte) {
            ctx->bad_cnt++;
            ctx->state = PST_SOF1;
            return 0;
        }
        /* 有效帧 */
        ctx->frame.msg_id = ctx->body[3];
        ctx->frame.seq    = ctx->body[4];
        ctx->frame.len    = ctx->expect_len;
        memcpy(ctx->frame.payload, &ctx->body[5], ctx->expect_len);
        ctx->good_cnt++;
        ctx->state = PST_SOF1;
        return 1;

    default:
        ctx->state = PST_SOF1;
        return 0;
    }
}

/* ========================= 串口收发 ========================== */
#define RX_RING_SIZE 256u                    /* 必须是 2 的幂 */

static volatile uint8_t  s_rx_buf[RX_RING_SIZE];
static volatile uint16_t s_rx_head;          /* 中断写 */
static volatile uint16_t s_rx_tail;          /* 主循环读 */
static uint8_t           s_rx_byte;

void proto_uart_start(void)
{
    s_rx_head = 0;
    s_rx_tail = 0;

    /* 使能 USART1 全局中断。CubeMX 里没勾 NVIC，这里手动开，
       避免为了一个中断重新生成工程。 */
    HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
}

/* USART1 中断向量：交给 HAL 分发，最终进入 HAL_UART_RxCpltCallback */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/* HAL 的接收完成回调：只做入缓冲 + 重新挂接收，绝不解析 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        uint16_t next = (uint16_t)((s_rx_head + 1u) & (RX_RING_SIZE - 1u));
        if (next != s_rx_tail) {              /* 满则丢弃，不覆盖未读数据 */
            s_rx_buf[s_rx_head] = s_rx_byte;
            s_rx_head = next;
        }
        (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
    }
}

uint8_t proto_uart_pop(uint8_t *byte)
{
    if (s_rx_tail == s_rx_head) {
        return 0u;
    }
    *byte = s_rx_buf[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) & (RX_RING_SIZE - 1u));
    return 1u;
}

void proto_send_status(uint16_t l_rpm, uint16_t r_rpm,
                       int16_t pitch_x10, uint16_t heartbeat)
{
    uint8_t  payload[10];
    uint8_t  frame[2 + 5 + 10 + 2];
    uint16_t n;

    payload[0] = (uint8_t)(l_rpm & 0xFFu);
    payload[1] = (uint8_t)(l_rpm >> 8);
    payload[2] = (uint8_t)(r_rpm & 0xFFu);
    payload[3] = (uint8_t)(r_rpm >> 8);
    payload[4] = (uint8_t)((uint16_t)pitch_x10 & 0xFFu);
    payload[5] = (uint8_t)((uint16_t)pitch_x10 >> 8);
    payload[6] = (uint8_t)(heartbeat & 0xFFu);
    payload[7] = (uint8_t)(heartbeat >> 8);
    payload[8] = 0u;
    payload[9] = 0u;

    n = proto_build(MSG_STATUS, (uint8_t)(heartbeat & 0xFFu),
                    payload, 10u, frame, sizeof(frame));
    if (n > 0u) {
        (void)HAL_UART_Transmit(&huart1, frame, n, 50u);
    }
}
