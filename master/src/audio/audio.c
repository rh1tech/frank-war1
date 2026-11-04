/*
 * audio.c - TDA1387 I2S output, fed from a ring.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The PIO program and the ping-pong DMA arrangement come from frank-test's
 * drivers/audio.c, which took them from pico-extras. What is different here is
 * how the buffers are filled.
 *
 * There, the producer calls i2s_dma_write() and it blocks until a DMA buffer
 * comes free -- fine for an emulator whose main loop is the audio loop. Here
 * the producer is the link service, which also carries the console, the disk
 * and the network, and a millisecond spent waiting for a DAC is a millisecond
 * the console is not being served. So the link drops PCM into a ring and
 * leaves; the DMA completion interrupt is what moves it into the buffer the
 * hardware just finished with, and plays silence when the ring runs dry.
 *
 * Taken from frank-linux firmware/master-ioserver/src/audio.c. Resources in
 * frank-amiga: PIO1 (the link is on PIO2, HDMI is HSTX), two dynamically
 * claimed DMA channels, DMA_IRQ_0 (DMA_IRQ_1 is the HSTX scanout's).
 *
 * Board: TDA1387 (U8), DATA GPIO9, BCLK GPIO10, LRCLK GPIO11.
 */

#include "audio.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"

#include <string.h>

#include "audio_i2s.pio.h"

#define AUDIO_PIO        pio1
#define AUDIO_DMA_IRQ    DMA_IRQ_0
#define FRANK_I2S_DATA_PIN       9
#define FRANK_I2S_CLOCK_PIN_BASE 10   /* BCLK 10, LRCLK 11 */

static uint AUDIO_DMA_CH_A, AUDIO_DMA_CH_B;

/*
 * One DMA buffer is 512 stereo frames, 10.7 ms at 48 kHz. That is the
 * interrupt rate as well, which at ~86 Hz is nothing next to the display, and
 * it is short enough that a track stopping is not audibly late.
 */
#define DMA_FRAMES       512u
#define DMA_BUFFERS      2u

/*
 * The ring is 32 kB, 170 ms of 48 kHz stereo. It has to cover the gap
 * between link transfers, which are whatever size the slave sends and arrive
 * whenever the service loop gets to them -- and the loop also answers block
 * requests that can take milliseconds against a busy card.
 */
/* frank-war1: the sound is made on core 1 itself, right next to this ring
 * (no link), so 8 kB -- 41 ms at 49716 Hz -- covers the gaps; the SRAM
 * goes to the third frame buffer. */
#define RING_BYTES       (8u * 1024u)
#define RING_MASK        (RING_BYTES - 1u)      /* a power of two, so no divide */

/*
 * Consecutive silent buffers before the state machine is stopped.
 *
 * It has to stop. This interrupt shares DMA_IRQ_0 with the ESP's receive
 * handling, and a handler that keeps running once the music has ended is a
 * permanent tax on the one path with no flow control anywhere on it -- the
 * symptom was PPP failing to negotiate at all, with
 *
 *   frank-esp: 1 bytes lost in the master's UART ring
 *
 * for every attempt. 20 buffers is about a quarter of a second, long enough
 * that a gap between tracks does not stop and restart the clocks.
 */
#define IDLE_BUFFERS     20u

static uint32_t __attribute__((aligned(4))) dma_buf[DMA_BUFFERS][DMA_FRAMES];
static uint8_t  __attribute__((aligned(4))) ring[RING_BYTES];

/* Single producer (the link service, on core 1) and single consumer (the DMA
 * interrupt). head is the producer's, tail is the consumer's, so neither needs
 * a lock -- the same discipline as the rings in frank_ring.h. */
static volatile uint32_t ring_head;
static volatile uint32_t ring_tail;

static uint audio_sm;
static uint32_t audio_rate = 48000;
static volatile bool audio_started;

static uint32_t ring_used(void)
{
    return (ring_head - ring_tail) & RING_MASK;
}

uint32_t audio_space(void)
{
    /* One byte short of full, so head == tail always means empty. */
    return RING_BYTES - 1u - ring_used();
}

uint32_t audio_queued(void)
{
    return ring_used();
}

/*
 * Fill one DMA buffer from the ring, padding with silence.
 *
 * Silence rather than stopping the DMA: a stopped state machine has to be
 * restarted in step with the word clock, and getting that wrong puts the
 * channels the wrong way round. Feeding zeroes costs a few hundred bytes of
 * bus traffic per interrupt and cannot desynchronise anything.
 */
/* frank-amiga: buffers the ring could not fill while sound was playing
 * (the emulation or the stream fell behind: heard as a gap). */
volatile uint32_t audio_starved;

static void __not_in_flash_func(fill_from_ring)(uint32_t *dst)
{
    static bool playing;
    uint32_t want = DMA_FRAMES * sizeof(uint32_t);
    uint32_t have = ring_used();
    uint32_t take = have < want ? have : want;
    if (take < want && playing)
        audio_starved++;
    playing = take == want;
    uint8_t *out = (uint8_t *)dst;
    uint32_t tail = ring_tail;

    /*
     * Two memcpys at most, rather than a byte loop with a divide in it.
     *
     * The first version copied 2048 bytes one at a time, each with a modulo,
     * inside an interrupt that shares its line with the ESP's UART. That is
     * roughly 176,000 divides a second spent in an interrupt handler, and what
     * it cost was not audio quality -- it was the netcard, which started
     * losing bytes and could not bring PPP up at all.
     */
    if (take) {
        uint32_t first = RING_BYTES - tail;

        if (first > take)
            first = take;
        memcpy(out, &ring[tail], first);
        if (take > first)
            memcpy(out + first, &ring[0], take - first);
        ring_tail = (tail + take) & RING_MASK;
    }

    if (take < want)
        memset(out + take, 0, want - take);
}

static volatile uint32_t idle_buffers;

/* Stop the clocks when there has been nothing to play for a while. Called from
 * the interrupt, so it only touches the two channels and the state machine. */
static void __not_in_flash_func(audio_stop)(void)
{
    dma_channel_abort(AUDIO_DMA_CH_A);
    dma_channel_abort(AUDIO_DMA_CH_B);
    pio_sm_set_enabled(AUDIO_PIO, audio_sm, false);
    pio_sm_clear_fifos(AUDIO_PIO, audio_sm);
    audio_started = false;
    idle_buffers = 0;
}

volatile uint32_t audio_frames_played;

static void __not_in_flash_func(audio_dma_irq)(void)
{
    audio_frames_played += DMA_FRAMES;
    bool silent = false;

    if (dma_hw->ints0 & (1u << AUDIO_DMA_CH_A)) {
        dma_hw->ints0 = 1u << AUDIO_DMA_CH_A;
        silent = (ring_used() == 0);
        fill_from_ring(dma_buf[0]);
        dma_channel_set_read_addr(AUDIO_DMA_CH_A, dma_buf[0], false);
    }
    if (dma_hw->ints0 & (1u << AUDIO_DMA_CH_B)) {
        dma_hw->ints0 = 1u << AUDIO_DMA_CH_B;
        silent = (ring_used() == 0);
        fill_from_ring(dma_buf[1]);
        dma_channel_set_read_addr(AUDIO_DMA_CH_B, dma_buf[1], false);
    }

    if (!silent)
        idle_buffers = 0;
    else if (++idle_buffers >= IDLE_BUFFERS)
        audio_stop();
}

void audio_set_rate(uint32_t hz)
{
    if (hz < 8000u || hz > 96000u)
        return;
    audio_rate = hz;

    /* Four PIO clocks per bit, 32 bits per frame: the divider is
     * sys_clk * 4 / rate in 8.8 fixed point, which is what the reference
     * driver computes and what the program's timing assumes. */
    uint32_t divider = clock_get_hz(clk_sys) * 4u / hz;
    pio_sm_set_clkdiv_int_frac(AUDIO_PIO, audio_sm,
                               (uint16_t)(divider >> 8u), (uint8_t)(divider & 0xffu));
}

uint32_t audio_get_rate(void)
{
    return audio_rate;
}

void audio_flush(void)
{
    uint32_t s = save_and_disable_interrupts();
    ring_tail = ring_head;
    restore_interrupts(s);
}

/*
 * Volume, in 256ths, applied on the way into the ring.
 *
 * Here rather than in the interrupt, which is where it looked like it belonged:
 * the interrupt shares its line with the netcard's UART and has already cost
 * PPP once by doing arithmetic per sample (see fill_from_ring). This runs on
 * core 0 in the link service, does the same number of multiplies, and does
 * them where nothing is waiting.
 *
 * The default is half, and that is a measurement rather than a preference.
 * A full-scale 1 kHz tone comes back off this board at 3.4% distortion; the
 * same tone at half amplitude comes back at 0.75%, and a quarter of that is
 * the analyser and the capture path. Something between the DAC's output and
 * the line it drives runs out of headroom before digital full scale does, so
 * anything mastered loud -- which is everything -- arrives clipped. Six dB
 * back from full scale is what makes an MP3 sound like the MP3.
 */
static volatile int32_t audio_vol = 128;        /* 256 = unity */

void audio_set_volume(uint32_t vol)
{
    if (vol > 256u)
        vol = 256u;
    audio_vol = (int32_t)vol;
}

uint32_t audio_get_volume(void)
{
    return (uint32_t)audio_vol;
}

/* Scale a run of 16-bit samples into the ring. `n` is bytes and is always a
 * multiple of four here -- the link pads to a word and a frame is two
 * samples -- but the odd tail is handled rather than assumed. */
static void ring_put_scaled(uint32_t at, const uint8_t *src, uint32_t n)
{
    int32_t vol = audio_vol;

    if (vol == 256) {
        memcpy(&ring[at], src, n);
        return;
    }

    for (uint32_t i = 0; i + 1 < n; i += 2) {
        int16_t v = (int16_t)((uint16_t)src[i] | ((uint16_t)src[i + 1] << 8));
        int32_t scaled = ((int32_t)v * vol) >> 8;

        ring[at + i]     = (uint8_t)scaled;
        ring[at + i + 1] = (uint8_t)(scaled >> 8);
    }
    if (n & 1)
        ring[at + n - 1] = src[n - 1];
}

uint32_t audio_write(const void *pcm, uint32_t bytes)
{
    const uint8_t *in = (const uint8_t *)pcm;
    uint32_t room = audio_space();
    uint32_t take = bytes < room ? bytes : room;
    uint32_t head = ring_head;

    if (take) {
        uint32_t first = RING_BYTES - head;

        if (first > take)
            first = take;
        ring_put_scaled(head, in, first);
        if (take > first)
            ring_put_scaled(0, in + first, take - first);
        head = (head + take) & RING_MASK;
    }
    __dmb();
    ring_head = head;

    /* Start on the first write, and again after a silence long enough to have
     * stopped the clocks. A machine that never plays anything never runs the
     * state machine at all, which matters because this interrupt shares a line
     * with the netcard. */
    if (!audio_started && take) {
        audio_started = true;
        idle_buffers = 0;
        fill_from_ring(dma_buf[0]);
        fill_from_ring(dma_buf[1]);
        dma_channel_set_read_addr(AUDIO_DMA_CH_A, dma_buf[0], false);
        dma_channel_set_read_addr(AUDIO_DMA_CH_B, dma_buf[1], false);
        pio_sm_set_enabled(AUDIO_PIO, audio_sm, true);
        dma_channel_start(AUDIO_DMA_CH_A);
    }
    return take;
}

void audio_init(void)
{
    ring_head = ring_tail = 0;
    audio_started = false;
    memset(dma_buf, 0, sizeof(dma_buf));

    gpio_set_function(FRANK_I2S_DATA_PIN, GPIO_FUNC_PIO1);
    gpio_set_function(FRANK_I2S_CLOCK_PIN_BASE, GPIO_FUNC_PIO1);
    gpio_set_function(FRANK_I2S_CLOCK_PIN_BASE + 1, GPIO_FUNC_PIO1);

    /* 12 mA, as the reference driver does: three pins switching at the bit
     * clock into a DAC across the board. */
    gpio_set_drive_strength(FRANK_I2S_DATA_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(FRANK_I2S_CLOCK_PIN_BASE, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(FRANK_I2S_CLOCK_PIN_BASE + 1, GPIO_DRIVE_STRENGTH_12MA);

    audio_sm = pio_claim_unused_sm(AUDIO_PIO, true);
    uint offset = pio_add_program(AUDIO_PIO, &audio_i2s_program);
    audio_i2s_program_init(AUDIO_PIO, audio_sm, offset,
                           FRANK_I2S_DATA_PIN, FRANK_I2S_CLOCK_PIN_BASE);
    pio_sm_clear_fifos(AUDIO_PIO, audio_sm);
    audio_set_rate(audio_rate);

    AUDIO_DMA_CH_A = (uint)dma_claim_unused_channel(true);
    AUDIO_DMA_CH_B = (uint)dma_claim_unused_channel(true);

    dma_channel_config a = dma_channel_get_default_config(AUDIO_DMA_CH_A);
    channel_config_set_read_increment(&a, true);
    channel_config_set_write_increment(&a, false);
    channel_config_set_transfer_data_size(&a, DMA_SIZE_32);
    channel_config_set_dreq(&a, pio_get_dreq(AUDIO_PIO, audio_sm, true));
    channel_config_set_chain_to(&a, AUDIO_DMA_CH_B);
    dma_channel_configure(AUDIO_DMA_CH_A, &a, &AUDIO_PIO->txf[audio_sm],
                          dma_buf[0], DMA_FRAMES, false);

    dma_channel_config b = dma_channel_get_default_config(AUDIO_DMA_CH_B);
    channel_config_set_read_increment(&b, true);
    channel_config_set_write_increment(&b, false);
    channel_config_set_transfer_data_size(&b, DMA_SIZE_32);
    channel_config_set_dreq(&b, pio_get_dreq(AUDIO_PIO, audio_sm, true));
    channel_config_set_chain_to(&b, AUDIO_DMA_CH_A);
    dma_channel_configure(AUDIO_DMA_CH_B, &b, &AUDIO_PIO->txf[audio_sm],
                          dma_buf[1], DMA_FRAMES, false);

    dma_hw->ints0 = (1u << AUDIO_DMA_CH_A) | (1u << AUDIO_DMA_CH_B);
    dma_channel_set_irq0_enabled(AUDIO_DMA_CH_A, true);
    dma_channel_set_irq0_enabled(AUDIO_DMA_CH_B, true);

    /* Shared, because the ESP UART is already on this interrupt. */
    irq_add_shared_handler(AUDIO_DMA_IRQ, audio_dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(AUDIO_DMA_IRQ, true);
}
