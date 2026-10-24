/**
 * oled.c -- 0.96 寸 OLED（SSD1306，128x64，I2C）STM32 HAL 驱动
 *
 * 显存布局与 SSD1306 页地址模式一致：
 *   OLED_GRAM[page][x]，每个字节的 bit0 = 该页最上面一行，bit7 = 最下面一行。
 * 字体为 8x16 ASCII（见 oledfont.h），每个字符占 2 页 × 8 列。
 */
#include "oled.h"
#include "oledfont.h"
#include <string.h>

/* 对应 oled.h 里的 OLED_I2C_HANDLE（在本工程 LED_light.c 中定义） */
extern I2C_HandleTypeDef OLED_I2C_HANDLE;

static uint8_t OLED_GRAM[OLED_PAGES][OLED_WIDTH];

/* ---------------- 底层 I2C 发送 ---------------- */

/* SSD1306 的 I2C 帧：控制字节 + 数据。
   控制字节 0x00 = 后面的字节全是命令；0x40 = 后面的字节全是显存数据。 */
static void OLED_WriteCmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    (void)HAL_I2C_Master_Transmit(&OLED_I2C_HANDLE, OLED_I2C_ADDR, buf, 2, OLED_I2C_TIMEOUT);
}

/* 调用方保证 data[0] 是 0x40 控制字节 */
static void OLED_WriteData(const uint8_t *data, uint16_t len)
{
    (void)HAL_I2C_Master_Transmit(&OLED_I2C_HANDLE, OLED_I2C_ADDR, (uint8_t *)data, len, OLED_I2C_TIMEOUT);
}

static void OLED_WriteCmds(const uint8_t *cmds, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        OLED_WriteCmd(cmds[i]);
    }
}

/* 设置写入位置：page = 0~7（每页 8 行），x = 像素列 0~127 */
static void OLED_SetCursor(uint8_t x, uint8_t page)
{
    page &= 0x07;
    OLED_WriteCmd((uint8_t)(0xB0 | page));              /* 页地址 */
    OLED_WriteCmd((uint8_t)(0x00 | (x & 0x0F)));        /* 列地址低 4 位 */
    OLED_WriteCmd((uint8_t)(0x10 | ((x >> 4) & 0x0F))); /* 列地址高 4 位 */
}

/* ---------------- 对外接口 ---------------- */

void OLED_Init(void)
{
    /* 上电后等一会儿，等 OLED 内部稳压稳定 */
    HAL_Delay(100);

    static const uint8_t init_seq[] = {
        0xAE,        /* 关显示 */
        0xD5, 0x80,  /* 时钟分频 */
        0xA8, 0x3F,  /* 多路复用 1/64 */
        0xD3, 0x00,  /* 显示偏移 0 */
        0x40,        /* 起始行 0 */
        0x8D, 0x14,  /* 电荷泵开（不开就是黑屏！） */
        0x20, 0x02,  /* 页地址模式 */
        0xA1,        /* 列重映射：左右反了改成 0xA0 */
        0xC8,        /* COM 扫描方向：上下反了改成 0xC0 */
        0xDA, 0x12,  /* COM 引脚配置 */
        0x81, 0xCF,  /* 对比度 */
        0xD9, 0xF1,  /* 预充电周期 */
        0xDB, 0x40,  /* VCOMH */
        0xA4,        /* 显示跟随显存 */
        0xA6,        /* 正常显示（0xA7 为反白） */
        0x2E,        /* 关闭滚动 */
        0xAF,        /* 开显示 */
    };

    OLED_WriteCmds(init_seq, sizeof(init_seq));
    OLED_Clear();
    OLED_Refresh();
}

void OLED_Clear(void)
{
    memset(OLED_GRAM, 0x00, sizeof(OLED_GRAM));
}

void OLED_Fill(uint8_t color)
{
    memset(OLED_GRAM, color ? 0xFF : 0x00, sizeof(OLED_GRAM));
}

void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }
    if (color) {
        OLED_GRAM[y >> 3][x] |= (uint8_t)(1u << (y & 0x07));
    } else {
        OLED_GRAM[y >> 3][x] &= (uint8_t)~(1u << (y & 0x07));
    }
}

void OLED_ClearPages(uint8_t page, uint8_t count)
{
    if (page >= OLED_PAGES || count == 0) {
        return;
    }
    if ((uint16_t)page + count > OLED_PAGES) {
        count = (uint8_t)(OLED_PAGES - page);
    }
    memset(&OLED_GRAM[page][0], 0x00, (size_t)count * OLED_WIDTH);
}

/* 逐页把显存推给屏幕 */
void OLED_Refresh(void)
{
    OLED_RefreshPages(0, OLED_PAGES);
}

void OLED_RefreshPages(uint8_t page, uint8_t count)
{
    uint8_t buf[1 + OLED_WIDTH];

    if (page >= OLED_PAGES || count == 0) {
        return;
    }
    if ((uint16_t)page + count > OLED_PAGES) {
        count = (uint8_t)(OLED_PAGES - page);
    }

    buf[0] = 0x40;   /* 后面的全是显存数据 */
    for (uint8_t p = page; p < (uint8_t)(page + count); p++) {
        OLED_SetCursor(0, p);
        memcpy(&buf[1], OLED_GRAM[p], OLED_WIDTH);
        OLED_WriteData(buf, sizeof(buf));
    }
}

/* 把一个 8x16 字符画到指定页，x 可越界（越界的列自动丢弃） */
static void OLED_DrawCharClipped(int16_t x, uint8_t page, char ch)
{
    /* 只支持可打印 ASCII；其他字符当空格 */
    if (ch < 0x20 || ch > 0x7E) {
        ch = ' ';
    }
    if (page > OLED_PAGES - 2) {
        return;
    }

    const uint8_t *glyph = OLED_F8x16[(uint8_t)ch - 0x20];
    for (uint8_t i = 0; i < 8; i++) {
        int16_t px = (int16_t)(x + i);
        if (px < 0 || px >= OLED_WIDTH) {
            continue;
        }
        OLED_GRAM[page][px]     = glyph[i];
        OLED_GRAM[page + 1][px] = glyph[i + 8];
    }
}

void OLED_ShowChar(uint8_t x, uint8_t y, char ch)
{
    if (x > OLED_WIDTH - 8) {
        return;
    }
    OLED_DrawCharClipped((int16_t)x, (uint8_t)(y >> 3), ch);
}

/* 可越界版本：x 为负或 >屏宽时，超出的部分被裁掉（滚动字幕就靠它） */
void OLED_ShowStringAt(int16_t x, uint8_t y, const char *str)
{
    uint8_t page = (uint8_t)(y >> 3);

    if (str == NULL || page > OLED_PAGES - 2) {
        return;
    }
    while (*str != '\0') {
        if (x >= OLED_WIDTH) {
            break;
        }
        OLED_DrawCharClipped(x, page, *str);
        x = (int16_t)(x + 8);
        str++;
    }
}

void OLED_ShowString(uint8_t x, uint8_t y, const char *str)
{
    if (str == NULL) {
        return;
    }
    while (*str != '\0' && x <= OLED_WIDTH - 8) {
        OLED_ShowChar(x, y, *str);
        x = (uint8_t)(x + 8);
        str++;
    }
}

/* 右对齐显示 len 位十进制数（len 大于实际位数时前面补空格，可显示负号） */
void OLED_ShowNum(uint8_t x, uint8_t y, int32_t num, uint8_t len)
{
    char digits[12];
    uint8_t cnt = 0;
    uint32_t u;

    if (num < 0) {
        digits[cnt++] = '-';
        u = (uint32_t)(-(int64_t)num);
    } else {
        u = (uint32_t)num;
    }

    char tmp[10];
    uint8_t t = 0;
    if (u == 0) {
        tmp[t++] = '0';
    }
    while (u > 0 && t < sizeof(tmp)) {
        tmp[t++] = (char)('0' + (u % 10));
        u /= 10;
    }
    for (uint8_t i = 0; i < t; i++) {
        digits[cnt++] = tmp[t - 1 - i];
    }

    if (len < cnt) {
        len = cnt;
    }

    for (uint8_t i = 0; i < len; i++) {
        int idx = (int)i - (int)(len - cnt);
        OLED_ShowChar(x, y, (idx >= 0) ? digits[idx] : ' ');
        x = (uint8_t)(x + 8);
    }
}
