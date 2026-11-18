/**
 * @file    upper.c
 * @brief   M4-4 上位机：Linux 下的串口工具（C 实现）
 *
 * 协议见 ../docs/protocol_v1.0.md：
 *   SOF(2) | VER(1) | LEN(2,LE) | MSG(1) | SEQ(1) | PAYLOAD | CRC16(2,LE)
 *   CRC16-CCITT-FALSE，覆盖 VER..PAYLOAD，小端，LEN 上限 64
 *
 * 设计要求（现场会考）：
 *   1. termios + cfmakeraw —— 关回显、关行缓冲、关特殊字符
 *   2. select() 带超时 —— 禁止裸阻塞 read()
 *   3. read() 返回 0/-1 时不退出进程，等设备重新出现
 *   4. 逐字节状态机 —— 脏字节后能重同步，不会永久失步
 *   5. 设备路径与波特率从命令行传入，不硬编码
 *
 * 编译：make
 * 运行：./upper -d /dev/ttyUSB0 -b 115200
 *       ./upper -d /dev/ttyUSB0 --send 300,300      # 下发目标转速
 *       ./upper -d /dev/ttyUSB0 --inject 20         # 每 20 个好帧插脏字节（破坏测试）
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PROTO_SOF1      0xAAu
#define PROTO_SOF2      0x55u
#define PROTO_VER       0x01u
#define PROTO_LEN_MAX   64u
#define PROTO_STATUS_MS 50u

#define MSG_CMD_SPEED   0x01u
#define MSG_STATUS      0x02u

/* ---------------- CRC16 / CCITT-FALSE ---------------- */
static uint16_t crc16_ccitt_false(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ---------------- 组帧 ---------------- */
static uint16_t build_frame(uint8_t msg_id, uint8_t seq,
                            const uint8_t *payload, uint16_t len,
                            uint8_t *out, uint16_t out_max)
{
    uint16_t total = (uint16_t)(2u + 5u + len + 2u);
    uint16_t crc;
    uint16_t i;

    if (len > PROTO_LEN_MAX || total > out_max) {
        return 0u;
    }
    out[0] = PROTO_SOF1;
    out[1] = PROTO_SOF2;
    out[2] = PROTO_VER;
    out[3] = (uint8_t)(len & 0xFFu);
    out[4] = (uint8_t)((len >> 8) & 0xFFu);
    out[5] = msg_id;
    out[6] = seq;
    for (i = 0; i < len; i++) {
        out[7 + i] = payload[i];
    }
    crc = crc16_ccitt_false(&out[2], (uint16_t)(5u + len));
    out[7 + len]      = (uint8_t)(crc & 0xFFu);
    out[7 + len + 1u] = (uint8_t)((crc >> 8) & 0xFFu);
    return total;
}

/* ---------------- 串口 ---------------- */
static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default:     return 0;
    }
}

static int serial_open(const char *dev, int baud)
{
    struct termios tio;
    speed_t sp = baud_to_speed(baud);
    int fd;

    if (sp == 0) {
        fprintf(stderr, "[ERR] 不支持的波特率: %d\n", baud);
        return -1;
    }

    fd = open(dev, O_RDWR | O_NOCTTY);   /* 不用 O_NDELAY，配合 select 使用 */
    if (fd < 0) {
        fprintf(stderr, "[ERR] 打开 %s 失败: %s\n", dev, strerror(errno));
        return -1;
    }

    if (tcgetattr(fd, &tio) != 0) {
        fprintf(stderr, "[ERR] tcgetattr 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);                     /* 8N1、无回显、无行缓冲、无特殊字符 */
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;             /* 无硬件流控 */
    tio.c_cc[VMIN]  = 0;                 /* 配合 select，非阻塞读 */
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        fprintf(stderr, "[ERR] tcsetattr 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* ---------------- 解析状态机 ---------------- */
enum { PST_SOF1, PST_SOF2, PST_BODY, PST_CRC_LO, PST_CRC_HI };

typedef struct {
    int      state;
    uint8_t  body[5 + PROTO_LEN_MAX];
    uint16_t blen;
    uint16_t expect_len;
    uint16_t crc_calc;
    uint32_t good;
    uint32_t bad;
} parser_t;

static void parser_init(parser_t *p)
{
    memset(p, 0, sizeof(*p));
    p->state = PST_SOF1;
}

static void parser_reset(parser_t *p)
{
    p->state      = PST_SOF1;
    p->blen       = 0;
    p->expect_len = 0xFFFFu;
}

/* 返回 1 表示凑出一帧，out_msg/out_seq/out_pl/out_len 有效 */
static int parser_feed(parser_t *p, uint8_t byte,
                       uint8_t *out_msg, uint8_t *out_seq,
                       uint8_t *out_pl, uint16_t *out_len)
{
    switch (p->state) {
    case PST_SOF1:
        if (byte == PROTO_SOF1) {
            p->state = PST_SOF2;
        }
        return 0;                        /* 其它字节是噪声，静默丢弃 */

    case PST_SOF2:
        if (byte == PROTO_SOF2) {
            p->state = PST_BODY;
            p->blen  = 0;
            p->expect_len = 0xFFFFu;
        } else {
            p->bad++;
            p->state = (byte == PROTO_SOF1) ? PST_SOF2 : PST_SOF1;
        }
        return 0;

    case PST_BODY:
        if (p->blen >= sizeof(p->body)) {
            p->bad++;
            parser_reset(p);
            return 0;
        }
        p->body[p->blen++] = byte;

        if (p->blen == 2u && p->body[0] != PROTO_VER) {
            p->bad++;
            parser_reset(p);
            return 0;
        }
        if (p->blen == 3u) {
            p->expect_len = (uint16_t)p->body[1] | ((uint16_t)p->body[2] << 8);
            if (p->expect_len > PROTO_LEN_MAX) {   /* 长度上限保护 */
                p->bad++;
                parser_reset(p);
                return 0;
            }
        }
        if (p->expect_len != 0xFFFFu &&
            p->blen == (uint16_t)(5u + p->expect_len)) {
            p->state = PST_CRC_LO;
        }
        return 0;

    case PST_CRC_LO:
        p->crc_calc = crc16_ccitt_false(p->body, p->blen);
        if ((uint8_t)(p->crc_calc & 0xFFu) != byte) {
            p->bad++;
            parser_reset(p);
            return 0;
        }
        p->state = PST_CRC_HI;
        return 0;

    case PST_CRC_HI:
        if ((uint8_t)(p->crc_calc >> 8) != byte) {
            p->bad++;
            parser_reset(p);
            return 0;
        }
        *out_msg = p->body[3];
        *out_seq = p->body[4];
        *out_len = p->expect_len;
        memcpy(out_pl, &p->body[5], p->expect_len);
        p->good++;
        parser_reset(p);
        return 1;

    default:
        parser_reset(p);
        return 0;
    }
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    const char *dev = NULL;
    int   baud      = 115200;
    int   seconds   = 20;
    int   inject    = 0;
    int   tgt_l     = 300;
    int   tgt_r     = 300;
    int   opt;
    int   fd;
    parser_t p;
    uint8_t pl[PROTO_LEN_MAX];
    uint8_t msg, seq;
    uint16_t plen;
    uint8_t  frame[64];
    uint16_t flen;
    uint8_t  buf[256];
    double   t0, t_last = 0.0;
    unsigned injected = 0;
    static const struct option longopts[] = {
        {"send",   required_argument, 0, 's'},
        {"inject", required_argument, 0, 'i'},
        {"time",   required_argument, 0, 't'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "d:b:s:i:t:", longopts, NULL)) != -1) {
        switch (opt) {
        case 'd': dev    = optarg; break;
        case 'b': baud   = atoi(optarg); break;
        case 's': sscanf(optarg, "%d,%d", &tgt_l, &tgt_r); break;
        case 'i': inject = atoi(optarg); break;
        case 't': seconds = atoi(optarg); break;
        default:
            fprintf(stderr,
                    "用法: %s -d /dev/ttyUSB0 [-b 115200] [--send L,R] "
                    "[--inject N] [--time 秒]\n", argv[0]);
            return 1;
        }
    }
    if (!dev) {
        fprintf(stderr, "错误: 必须用 -d 指定串口设备\n");
        return 1;
    }

    fd = serial_open(dev, baud);
    if (fd < 0) {
        return 1;
    }
    printf("[INFO] %s @ %d 8N1\n", dev, baud);

    /* 下发一次速度指令 */
    pl[0] = (uint8_t)(tgt_l & 0xFF);
    pl[1] = (uint8_t)((tgt_l >> 8) & 0xFF);
    pl[2] = (uint8_t)(tgt_r & 0xFF);
    pl[3] = (uint8_t)((tgt_r >> 8) & 0xFF);
    flen = build_frame(MSG_CMD_SPEED, 0x01, pl, 4, frame, sizeof(frame));
    if (flen > 0 && write(fd, frame, flen) == (ssize_t)flen) {
        printf("[TX] CMD_SPEED L=%d R=%d :", tgt_l, tgt_r);
        for (uint16_t i = 0; i < flen; i++) {
            printf(" %02X", frame[i]);
        }
        printf("\n");
    }

    parser_init(&p);
    t0 = now_s();

    while (now_s() - t0 < seconds) {
        fd_set rfds;
        struct timeval tv;
        int ret;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec  = 0;
        tv.tv_usec = 100000;             /* 100ms 超时，绝不永久阻塞 */

        ret = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "[ERR] select: %s\n", strerror(errno));
            break;
        }
        if (ret == 0) {
            continue;                    /* 超时：本轮没数据，继续等 */
        }

        {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n == 0) {
                continue;                /* 无数据 */
            }
            if (n < 0) {
                /* EIO/ENODEV：USB-TTL 被拔掉。不退出，等它回来。 */
                fprintf(stderr, "[WARN] read: %s（设备可能已断开，继续等待）\n",
                        strerror(errno));
                usleep(200000);
                continue;
            }
            for (ssize_t i = 0; i < n; i++) {
                if (parser_feed(&p, buf[i], &msg, &seq, pl, &plen)) {
                    if (msg == MSG_STATUS && plen == 10) {
                        uint16_t l = (uint16_t)(pl[0] | (pl[1] << 8));
                        uint16_t r = (uint16_t)(pl[2] | (pl[3] << 8));
                        int16_t  pitch = (int16_t)(pl[4] | (pl[5] << 8));
                        uint16_t hb    = (uint16_t)(pl[6] | (pl[7] << 8));
                        uint16_t badf  = (uint16_t)(pl[8] | (pl[9] << 8));
                        double   t     = now_s() - t0;

                        if (t - t_last > 0.25) {
                            printf("[RX] t=%5.1fs seq=%3u L=%5u R=%5u pitch=%6.1f "
                                   "hb=%5u bad=%3u | good=%u bad=%u\n",
                                   t, seq, l, r, pitch / 10.0, hb, badf,
                                   p.good, p.bad);
                            t_last = t;
                        }
                    }
                }
            }
        }

        /* 破坏测试：每隔 N 个好帧往串口灌 5 字节脏数据 */
        if (inject > 0 && p.good > 0 && p.good % (uint32_t)inject == 0) {
            uint8_t noise[5];
            for (int i = 0; i < 5; i++) {
                noise[i] = (uint8_t)(rand() & 0xFF);
            }
            if (write(fd, noise, sizeof(noise)) < 0) {
                /* 破坏测试注入失败不影响主流程，忽略 */
            }
            injected++;
            p.good++;                    /* 避免同一批次重复注入 */
        }
    }

    printf("\n[SUM] 好帧=%u 坏帧=%u 注入脏包=%u\n", p.good, p.bad, injected);
    close(fd);
    return 0;
}
