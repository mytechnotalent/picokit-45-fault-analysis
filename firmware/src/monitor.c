// MIT License
//
// Copyright (c) 2026 Kevin Thomas
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Author:  Kevin Thomas
// Email:   kevin@mytechnotalent.com
// GitHub:  https://github.com/mytechnotalent/picokit-45-fault-analysis
// File:    monitor.c
// Desc:    Implements the fault monitor that reads the Cortex-M33 Configurable
//          Fault Status Register and clears it after each report.
// Created: 2026

#include "picokit_45_fault_analysis.h"
#include "monitor.h"
#include "radio.h"
#include "status_led.h"
#include "ccm.h"
#include "envelope.h"
#include "field_secrets.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Cortex-M33 Configurable Fault Status Register address.
 */
#define MONITOR_FAULT_CFSR_ADDR 0xE000ED28u

#ifndef PICO_ON_DEVICE

/**
 * @brief Host-test fault status register backing store.
 */
static uint32_t g_fault_status;

/**
 * @brief Read the host-test fault status register.
 *
 * @param void No parameters.
 * @return uint32_t Current fault status value.
 */
static uint32_t monitor_fault_read(void) {
    return g_fault_status;
}

/**
 * @brief Clear the host-test fault status register.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_fault_clear(void) {
    g_fault_status = 0u;
}

#else

/**
 * @brief Read the Cortex-M33 Configurable Fault Status Register.
 *
 * @param void No parameters.
 * @return uint32_t Current fault status register value.
 */
static uint32_t monitor_fault_read(void) {
    return *(volatile uint32_t *)MONITOR_FAULT_CFSR_ADDR;
}

/**
 * @brief Clear the Cortex-M33 Configurable Fault Status Register.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_fault_clear(void) {
    *(volatile uint32_t *)MONITOR_FAULT_CFSR_ADDR = 0u;
}

#endif

/**
 * @brief Module-ready flag.
 *
 * Set to true by monitor_init() once the peripherals are configured.
 * monitor_step() returns false while this flag is clear.
 */
static bool g_ready;

/**
 * @brief Monotonic transmit sequence number.
 */
static uint16_t g_seq;

/**
 * @brief Fault status captured for the current heartbeat.
 */
static uint32_t g_fault;

/**
 * @brief Absolute time in microseconds of the next authenticated transmit.
 */
static uint64_t g_next_tx_us;

/**
 * @brief Inbound radio line accumulator.
 */
static char g_rx_line[RADIO_LINE_BUF_LEN];

/**
 * @brief Number of bytes currently held in the inbound line accumulator.
 */
static size_t g_rx_len;

/**
 * @brief AES-128 session key for telemetry.
 */
static uint8_t g_key[CCM_KEY_LEN];

/**
 * @brief True once the telemetry session key has been loaded.
 */
static bool g_key_ready;

/**
 * @brief Configure the onboard heartbeat LED as a dark output.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_state_init_io(void) {
    gpio_init(PICOKIT_45_FAULT_ANALYSIS_LED_PIN);
    gpio_set_dir(PICOKIT_45_FAULT_ANALYSIS_LED_PIN, GPIO_OUT);
    gpio_put(PICOKIT_45_FAULT_ANALYSIS_LED_PIN, 0);
}

/**
 * @brief Reset the fault latch, sequence, and transmit timing.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_state_init(void) {
    uint64_t now_us = time_us_64();
    g_seq = 0u;
    g_fault = 0u;
    monitor_fault_clear();
    g_next_tx_us = now_us + (uint64_t)PICOKIT_45_FAULT_ANALYSIS_TX_INTERVAL_MS * 1000u;
    g_ready = true;
}

/**
 * @brief Read the fault status register and clear it when it was set.
 *
 * @param void No parameters.
 * @return uint32_t Fault status captured before the clear.
 */
static uint32_t monitor_fault_poll(void) {
    uint32_t status = monitor_fault_read();
    if (status != 0u) {
        monitor_fault_clear();
    }
    return status;
}

/**
 * @brief Load the telemetry session key from the field secret.
 *
 * LAB-ONLY: production must provision the session key through OTP rather
 * than embedding a committed key.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_load_key(void) {
    static const uint8_t key[CCM_KEY_LEN] = FIELD_SECRET_KEY;
    memcpy(g_key, key, CCM_KEY_LEN);
    g_key_ready = true;
}

/**
 * @brief Print the boot banner for the fault analysis lesson.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_banner(void) {
    printf("=== PICOKIT-45 FAULT ANALYSIS // CFSR READ + DEBUG LAB ===\n");
}

/**
 * @brief Derive the field key and announce a ready monitor.
 *
 * @param void No parameters.
 * @return bool true when the field key was derived and installed.
 */
static bool monitor_finish(void) {
    monitor_load_key();
    monitor_banner();
    return true;
}

/**
 * @brief Blink the onboard heartbeat LED exactly once.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_heartbeat(void) {
    gpio_put(PICOKIT_45_FAULT_ANALYSIS_LED_PIN, 1);
    sleep_us(MONITOR_HEARTBEAT_BLINK_US);
    gpio_put(PICOKIT_45_FAULT_ANALYSIS_LED_PIN, 0);
    sleep_us(MONITOR_HEARTBEAT_BLINK_US);
}

/**
 * @brief Format the heartbeat JSON body with the captured fault status.
 *
 * @param frame Pointer to the mutable frame output buffer.
 * @param frame_len Capacity of the frame output buffer in bytes.
 * @return size_t Number of JSON bytes written, or zero on overflow.
 */
static size_t monitor_build_frame(char *frame, size_t frame_len) {
    int written = snprintf(frame, frame_len, "{\"n\":%u,\"s\":%u,\"f\":%u}", (unsigned)PACKET_NODE_ID, (unsigned)g_seq, (unsigned)g_fault);
    return (written > 0 && (size_t)written < frame_len) ? (size_t)written : 0u;
}

/**
 * @brief Seal the current heartbeat body into a hex envelope.
 *
 * @param hex Pointer to the NUL-terminated hex output buffer.
 * @param hex_len Capacity of the hex output buffer in bytes.
 * @return bool true when the heartbeat was sealed and encoded.
 */
static bool monitor_seal_frame(char *hex, size_t hex_len) {
    char frame[PICOKIT_45_FAULT_ANALYSIS_FRAME_SIZE];
    uint8_t nonce[ENVELOPE_NONCE_LEN];
    uint8_t ad = (uint8_t)PACKET_NODE_ID;
    size_t frame_len = monitor_build_frame(frame, sizeof(frame));
    envelope_fill_nonce(nonce);
    return envelope_seal_hex(g_key, nonce, &ad, 1u, (const uint8_t *)frame, frame_len, hex, hex_len);
}

/**
 * @brief Build and transmit the authenticated heartbeat frame.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_transmit(void) {
    char hex[ENVELOPE_MAX_HEX_LEN];
    if (!g_key_ready) {
        return;
    }
    g_fault = monitor_fault_poll();
    if (monitor_seal_frame(hex, sizeof(hex))) {
        radio_send_frame(PICOKIT_45_FAULT_ANALYSIS_UART, (const uint8_t *)hex, strlen(hex));
        g_seq += 1u;
    }
}

/**
 * @brief Print one console line for the current heartbeat transmit.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_log_tx(void) {
    printf("FAULT n=%u f=0x%08X seq=%u\n", (unsigned)PACKET_NODE_ID, (unsigned)g_fault, (unsigned)g_seq);
}

/**
 * @brief Transmit one heartbeat and schedule the next transmit.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_tx_tick(uint64_t now_us) {
    monitor_transmit();
    monitor_heartbeat();
    monitor_log_tx();
    g_next_tx_us = now_us + (uint64_t)PICOKIT_45_FAULT_ANALYSIS_TX_INTERVAL_MS * 1000u;
}

/**
 * @brief Drain inbound radio lines and log every valid +RCV report.
 *
 * @param void No parameters.
 * @return void
 */
static void monitor_rx_tick(void) {
    radio_rcv_t rcv;
    while (radio_line_pump(PICOKIT_45_FAULT_ANALYSIS_UART, g_rx_line, &g_rx_len)) {
        if (radio_parse_rcv(g_rx_line, &rcv) == RADIO_RESULT_OK) {
            printf("RX from 0x%04X, %u bytes\n", (unsigned)rcv.sender, (unsigned)rcv.len);
        }
    }
}

/**
 * @brief Service the heartbeat transmit timer.
 *
 * @param now_us Current monotonic time in microseconds.
 * @return void
 */
static void monitor_service_timers(uint64_t now_us) {
    if (now_us >= g_next_tx_us) {
        monitor_tx_tick(now_us);
    }
}

bool monitor_init(void) {
    bool ok;
    ok = status_led_init() && radio_init(PICOKIT_45_FAULT_ANALYSIS_UART);
    monitor_state_init_io();
    monitor_state_init();
    return ok && monitor_finish();
}

void monitor_deinit(void) {
    g_ready = false;
}

bool monitor_step(void) {
    uint64_t now_us;
    if (!g_ready) {
        return false;
    }
    now_us = time_us_64();
    monitor_service_timers(now_us);
    monitor_rx_tick();
    return true;
}
