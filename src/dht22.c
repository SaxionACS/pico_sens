// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Dawid Zalewski
//
// DHT22 (AM2302) driver. The bit-level protocol runs in a PIO state machine
// (see dht22.pio); this file sets the state machine up, starts a measurement,
// collects what the state machine received and decodes it.

#include "pico_sens/dht22.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "dht22.pio.h"  // generated from dht22.pio during the build

// Timing rules from the datasheet
static const uint32_t POWER_UP_MS = 1000;      // the sensor ignores the Pico for 1 s after power-up
static const uint32_t MIN_INTERVAL_MS = 2000;  // and needs a 2 s break after every measurement
static const uint32_t START_SIGNAL_US = 2000;  // "at least 1 ms" low wakes it up; 2 ms to be safe
static const uint32_t ANSWER_US = 7000;        // its answer takes at most about 5.6 ms

static const float PIO_CLOCK_HZ = 1000000.0f;  // 1 MHz: one PIO cycle takes 1 us

// The state machine hands over the 40 bits in 3 pieces: humidity (16 bits),
// temperature (16 bits) and checksum (8 bits). They all fit in its RX FIFO
// (4 entries), so it never has to wait for the CPU to collect them.
static const uint PIECES = 3;

bool dht22_init(dht22_t *sensor, uint pin) {
    // Find a PIO block with a free state machine and enough free instruction
    // memory, and load our program there.
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&dht22_program, &sensor->pio, &sensor->sm,
                                                          &sensor->offset, pin, 1, true)) {
        return false;
    }
    sensor->pin = pin;
    sensor->next_read = from_us_since_boot(POWER_UP_MS * 1000ull);
    sensor->started = false;
    sensor->done = false;

    pio_sm_config config = dht22_program_get_default_config(sensor->offset);
    sm_config_set_set_pins(&config, pin, 1);  // "set pindirs" switches the data pin's direction
    sm_config_set_in_pins(&config, pin);      // "wait ... pin 0" and "in pins" read the data pin
    // The sensor sends the most significant bit first, so shift the bits in
    // to the left, and push every 16 bits to the RX FIFO automatically (the
    // last 8 bits are pushed by the program itself).
    sm_config_set_in_shift(&config, false, true, 16);
    sm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / PIO_CLOCK_HZ);
    pio_sm_init(sensor->pio, sensor->sm, sensor->offset, &config);

    // The pin's output value stays 0 forever: the program only switches the
    // pin between output (line low) and input (let go of the line).
    pio_sm_set_pins_with_mask(sensor->pio, sensor->sm, 0, 1u << pin);
    pio_sm_set_pindirs_with_mask(sensor->pio, sensor->sm, 0, 1u << pin);
    gpio_pull_up(pin);
    pio_gpio_init(sensor->pio, pin);  // from now on the PIO controls the pin
    return true;
}

dht22_status_t dht22_read_start(dht22_t *sensor) {
    if (sensor->started) {
        return DHT22_BUSY;
    }
    if (!time_reached(sensor->next_read)) {
        return DHT22_ERROR_TOO_SOON;
    }
    PIO pio = sensor->pio;
    uint sm = sensor->sm;

    // Start from a clean slate, also after a failed measurement: empty FIFOs,
    // and the state machine at the first instruction of the program.
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(sensor->offset));
    pio_sm_set_enabled(pio, sm, true);

    // Go: the program waits for the length of the start signal
    pio_sm_put(pio, sm, START_SIGNAL_US);

    sensor->deadline = make_timeout_time_us(START_SIGNAL_US + ANSWER_US);
    sensor->started = true;
    sensor->done = false;
    return DHT22_OK;
}

bool dht22_read_done(dht22_t *sensor) {
    if (!sensor->started || sensor->done) {
        return sensor->done;
    }
    bool all_in = pio_sm_get_rx_fifo_level(sensor->pio, sensor->sm) == PIECES;
    if (!all_in && !time_reached(sensor->deadline)) {
        return false;  // still measuring
    }
    // The program has let go of the line by now, so it is safe to stop it
    pio_sm_set_enabled(sensor->pio, sensor->sm, false);
    sensor->done = true;
    // Whatever the outcome, the sensor needs a break now. If we notice the end
    // only after the deadline, the measurement was over by the deadline.
    absolute_time_t end = absolute_time_min(get_absolute_time(), sensor->deadline);
    sensor->next_read = delayed_by_ms(end, MIN_INTERVAL_MS);
    return true;
}

dht22_status_t dht22_read_result(dht22_t *sensor, dht22_reading_t *reading) {
    if (!sensor->started) {
        return DHT22_ERROR_NOT_STARTED;
    }
    if (!dht22_read_done(sensor)) {
        return DHT22_BUSY;
    }
    sensor->started = false;  // collected: the next measurement may start
    sensor->done = false;

    PIO pio = sensor->pio;
    uint sm = sensor->sm;
    uint pieces = pio_sm_get_rx_fifo_level(pio, sm);
    if (pieces == 0) {
        return DHT22_ERROR_NO_RESPONSE;  // not even 16 bits arrived
    }
    if (pieces < PIECES) {
        return DHT22_ERROR_INCOMPLETE;
    }
    uint16_t humidity = (uint16_t)pio_sm_get(pio, sm);
    uint16_t temperature = (uint16_t)pio_sm_get(pio, sm);
    uint8_t checksum = (uint8_t)pio_sm_get(pio, sm);

    // The checksum is the sum of the 4 data bytes, cut to 8 bits.
    // It has a blind spot: if every bit was read as 0, the sum is 0 as well.
    // A real sensor never measures 0.0 % at 0.0 C, so all zeros is an error too.
    uint8_t sum = (uint8_t)((humidity >> 8) + (humidity & 0xFF) + (temperature >> 8) + (temperature & 0xFF));
    if (sum != checksum || (humidity == 0 && temperature == 0)) {
        return DHT22_ERROR_CHECKSUM;
    }
    // Humidity: in tenths of a percent (e.g. 0x028C = 652 -> 65.2 %)
    reading->humidity = humidity / 10.0f;
    // Temperature: 15 bits in tenths of a degree, and the top bit is the sign
    // (e.g. 0x8065 -> 0x0065 = 101 -> -10.1 C)
    float magnitude = (temperature & 0x7FFF) / 10.0f;
    reading->temperature = (temperature & 0x8000) ? -magnitude : magnitude;
    return DHT22_OK;
}

dht22_status_t dht22_read(dht22_t *sensor, dht22_reading_t *reading) {
    dht22_status_t status = dht22_read_start(sensor);
    if (status != DHT22_OK) {
        return status;
    }
    while (!dht22_read_done(sensor)) {
        tight_loop_contents();
    }
    return dht22_read_result(sensor, reading);
}

const char *dht22_status_str(dht22_status_t status) {
    switch (status) {
    case DHT22_OK:
        return "OK";
    case DHT22_ERROR_TOO_SOON:
        return "too soon: the sensor needs 1 s after power-up and 2 s between measurements";
    case DHT22_ERROR_NO_RESPONSE:
        return "no response: check the wiring, the power and the pin number";
    case DHT22_ERROR_INCOMPLETE:
        return "incomplete answer: the sensor stopped halfway (loose wire?)";
    case DHT22_ERROR_CHECKSUM:
        return "checksum mismatch: the data got corrupted on the way (noise?)";
    case DHT22_BUSY:
        return "busy: the measurement is still running, or its result was not collected yet";
    case DHT22_ERROR_NOT_STARTED:
        return "not started: call dht22_read_start() before dht22_read_result()";
    }
    return "unknown status";
}

void dht22_deinit(dht22_t *sensor) {
    pio_sm_set_enabled(sensor->pio, sensor->sm, false);
    pio_remove_program_and_unclaim_sm(&dht22_program, sensor->pio, sensor->sm, sensor->offset);
    gpio_deinit(sensor->pin);
    sensor->started = false;
    sensor->done = false;
}
