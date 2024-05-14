/*
 * ST7789 Buffered Display Driver for Pico with DMA Support
 * WHowe <github.com/whowechina>
 * 
 * LEDK is driven by PWM to adjust brightness
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

#include "st7789.h"

#define WIDTH 240
#define HEIGHT 320

static struct {
    spi_inst_t *spi;
    uint8_t dc;
    uint8_t rst;
    uint8_t ledk;
    int dma_tx;
    dma_channel_config dma_cfg;
} ctx;

static uint16_t vram[HEIGHT][WIDTH];

static void send_cmd(uint8_t cmd, const void *data, size_t len)
{
    spi_set_format(ctx.spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_put(ctx.dc, 0);
    spi_write_blocking(ctx.spi, &cmd, sizeof(cmd));
    gpio_put(ctx.dc, 1);

    if (len > 0) {
        spi_write_blocking(ctx.spi, data, len);
    }
}

void st7789_reset()
{
    gpio_put(ctx.dc, 1);
    gpio_put(ctx.rst, 1);
    sleep_ms(100);
    
    send_cmd(0x01, NULL, 0); // Software Reset
    sleep_ms(130);

    send_cmd(0x11, NULL, 0); // Sleep Out
    sleep_ms(10);

    send_cmd(0x3a, "\x55", 1); // 16bit RGB (5-6-5)
    send_cmd(0x36, "\x00", 1); // Regular VRam Access

    send_cmd(0x21, NULL, 0); // Display Inversion for TTF
    send_cmd(0x13, NULL, 0); // Normal Display Mode On
    send_cmd(0x29, NULL, 0); // Turn On Display
}

void st7789_init_spi(spi_inst_t *port, uint8_t sck, uint8_t tx, uint8_t csn)
{
    spi_init(port, 125 * 1000 * 1000);
    gpio_set_function(tx, GPIO_FUNC_SPI);
    gpio_set_function(sck, GPIO_FUNC_SPI);
    gpio_init(csn);
    gpio_set_dir(csn, GPIO_OUT);
    gpio_put(csn, 0);
}

static void init_gpio()
{
    gpio_init(ctx.dc);
    gpio_set_dir(ctx.dc, GPIO_OUT);

    gpio_init(ctx.rst);
    gpio_set_dir(ctx.rst, GPIO_OUT);

    gpio_init(ctx.ledk);
    gpio_set_function(ctx.ledk, GPIO_FUNC_PWM);
    gpio_set_drive_strength(ctx.ledk, GPIO_DRIVE_STRENGTH_12MA);
}

static void init_pwm()
{
    uint slice_num = pwm_gpio_to_slice_num(ctx.ledk);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.f);
    pwm_init(slice_num, &cfg, true);
    pwm_set_wrap(slice_num, 255);
    pwm_set_enabled(slice_num, true);
}

static void init_dma()
{
    ctx.dma_tx = dma_claim_unused_channel(true);
    ctx.dma_cfg = dma_channel_get_default_config(ctx.dma_tx);
    channel_config_set_transfer_data_size(&ctx.dma_cfg, DMA_SIZE_16);
    channel_config_set_dreq(&ctx.dma_cfg, spi_get_dreq(ctx.spi, true));
}

void st7789_init(spi_inst_t *port, uint8_t dc, uint8_t rst, uint8_t ledk)
{
    ctx.spi = port;
    ctx.dc = dc;
    ctx.rst = rst;
    ctx.ledk = ledk;

    init_gpio();
    init_pwm();
    init_dma();

    st7789_reset();
}

void st7789_dimmer(uint8_t level)
{
    pwm_set_gpio_level(ctx.ledk, level);
}

void st7789_vsync()
{
    dma_channel_wait_for_finish_blocking(ctx.dma_tx);
}

void st7789_render(bool vsync)
{
    send_cmd(0x2c, NULL, 0);

    spi_set_format(ctx.spi, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    dma_channel_configure(ctx.dma_tx, &ctx.dma_cfg,
                          &spi_get_hw(ctx.spi)->dr, // write address
                          vram, // read address
                          sizeof(vram) / 2, // count
                          true); // start right now
    if (vsync) {
        st7789_vsync();
    }
}

uint16_t st7789_rgb565(uint32_t rgb32)
{
    return ((rgb32 >> 8) & 0xf800) | ((rgb32 >> 5) & 0x07e0) | ((rgb32 >> 3) & 0x001f);
}

void st7789_clear()
{
    memset(vram, 0, sizeof(vram));
}

void st7789_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if ((x >= WIDTH) || (y >= HEIGHT)) {
        return;
    }
    vram[y][x] = color;
}

void st7789_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    if ((x >= WIDTH) || (y >= HEIGHT)) {
        return;
    }
    w = x + w > WIDTH ? WIDTH - x : w;

    for (int i = 0; i < w; i++) {
        vram[y][x + i] = color;
    }
}
void st7789_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    if ((x >= WIDTH) || (y >= HEIGHT)) {
        return;
    }
    h = y + h > HEIGHT ? HEIGHT - y : h;

    for (int i = 0; i < h; i++) {
        vram[y + i][x] = color;
    }
}

void st7789_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if ((x >= WIDTH) || (y >= HEIGHT)) {
        return;
    }
    w = x + w > WIDTH ? WIDTH - x : w;
    h = y + h > HEIGHT ? HEIGHT - y : h;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            vram[y + i][x + j] = color;
        }
    }
}

void st7789_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;

    for (;;) {
        st7789_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}