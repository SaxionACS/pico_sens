// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Dawid Zalewski

/**
 * \file pico_sens/dht22.h
 * \brief Read a DHT22 (AM2302) temperature and humidity sensor on the RP2040.
 *
 * The timing-critical part of the protocol runs in a PIO state machine, so
 * interrupts (USB, timers) cannot disturb a measurement.
 *
 * There are two ways to measure. dht22_read() waits for the result (about 6 ms):
 *
 * \code
 * dht22_t sensor;
 * dht22_init(&sensor, 15);              // data line on GPIO 15
 *
 * while (true) {
 *     dht22_reading_t reading;
 *     if (dht22_read(&sensor, &reading) == DHT22_OK) {
 *         printf("%.1f C, %.1f %%\n", reading.temperature, reading.humidity);
 *     }
 * }
 * \endcode
 *
 * Or start a measurement, let the PIO do it while your program does other
 * work, and collect the result when it is done (no function waits):
 *
 * \code
 * while (true) {
 *     dht22_read_start(&sensor);         // starts one whenever the sensor allows it
 *
 *     if (dht22_read_done(&sensor)) {
 *         dht22_reading_t reading;
 *         if (dht22_read_result(&sensor, &reading) == DHT22_OK) {
 *             printf("%.1f C, %.1f %%\n", reading.temperature, reading.humidity);
 *         }
 *     }
 * }
 * \endcode
 */

#ifndef PICO_SENS_DHT22_H
#define PICO_SENS_DHT22_H

#include <stdbool.h>

#include "hardware/pio.h"
#include "pico/time.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief What happened when you asked the sensor for a measurement. */
typedef enum {
    DHT22_OK = 0,            /**< Success, the reading is filled in. */
    DHT22_ERROR_TOO_SOON,    /**< The sensor needs 1 s after power-up and 2 s between measurements. Try again later. */
    DHT22_ERROR_NO_RESPONSE, /**< The sensor did not answer: check the wiring, the power and the pin number. */
    DHT22_ERROR_INCOMPLETE,  /**< The sensor stopped answering halfway through (loose wire?). */
    DHT22_ERROR_CHECKSUM,    /**< The data arrived, but was corrupted on the way (noise on the wire?). */
    DHT22_BUSY,              /**< A measurement is running, or its result has not been collected yet. */
    DHT22_ERROR_NOT_STARTED, /**< dht22_read_result() was called, but no measurement was started. */
} dht22_status_t;

/** \brief One measurement. */
typedef struct {
    float temperature;  /**< Degrees Celsius, -40.0 to 80.0, in steps of 0.1. */
    float humidity;     /**< Relative humidity in percent, 0.0 to 100.0, in steps of 0.1. */
} dht22_reading_t;

/**
 * \brief One DHT22 sensor.
 *
 * Fill it in with dht22_init(), then pass it to the other functions. You do
 * not need to touch its fields yourself.
 */
typedef struct {
    PIO pio;                    /**< PIO block that runs the protocol (pio0 or pio1). */
    uint sm;                    /**< State machine in that block. */
    uint offset;                /**< Where the PIO program sits in the block's instruction memory. */
    uint pin;                   /**< GPIO connected to the sensor's data line. */
    absolute_time_t next_read;  /**< The sensor may not be asked for a measurement before this time. */
    absolute_time_t deadline;   /**< A started measurement has failed if it is not done by this time. */
    bool started;               /**< A measurement was started and its result was not collected yet. */
    bool done;                  /**< That measurement is over: the result is waiting to be collected. */
} dht22_t;

/**
 * \brief Set up a sensor on a GPIO pin.
 *
 * Claims a free PIO state machine, loads the protocol program into it and
 * enables the pin's internal pull-up, so no external resistor is needed.
 *
 * \param sensor the sensor to set up
 * \param pin GPIO connected to the sensor's data line (pin 2 of the DHT22)
 * \return false if no PIO state machine or instruction memory was free
 *         (each sensor needs a state machine and 18 of a PIO block's 32
 *         instructions, so two sensors at most).
 */
bool dht22_init(dht22_t *sensor, uint pin);

/**
 * \brief Measure temperature and humidity.
 *
 * Takes about 6 ms when it talks to the sensor. The sensor needs 1 second
 * after power-up before its first measurement, and a 2 second break after
 * every measurement. If you call dht22_read() sooner than that, it returns
 * DHT22_ERROR_TOO_SOON at once, without talking to the sensor.
 *
 * \param sensor a sensor set up with dht22_init()
 * \param[out] reading filled in when the result is DHT22_OK, untouched otherwise
 * \return DHT22_OK, or what went wrong (DHT22_BUSY if a measurement started
 *         with dht22_read_start() has not been collected yet)
 */
dht22_status_t dht22_read(dht22_t *sensor, dht22_reading_t *reading);

/**
 * \brief Start a measurement, without waiting for it.
 *
 * The PIO state machine does the measurement on its own (about 6 ms).
 * Meanwhile, check with dht22_read_done() whether it is over, and then
 * collect the result with dht22_read_result().
 *
 * \param sensor a sensor set up with dht22_init()
 * \return DHT22_OK when the measurement started; DHT22_ERROR_TOO_SOON within
 *         1 s of power-up or 2 s after the previous measurement (as for
 *         dht22_read()); DHT22_BUSY if the previous result was not collected yet
 */
dht22_status_t dht22_read_start(dht22_t *sensor);

/**
 * \brief Check whether a measurement started with dht22_read_start() is over.
 *
 * Returns at once. When it returns true, dht22_read_result() has the result.
 *
 * \param sensor a sensor set up with dht22_init()
 * \return true when the result is waiting to be collected; false while the
 *         measurement is running, and when no measurement was started
 */
bool dht22_read_done(dht22_t *sensor);

/**
 * \brief Collect the result of a measurement started with dht22_read_start().
 *
 * Returns at once. You can collect a result as late as you like: the sensor's
 * 2 s break counts from the end of the measurement (at the latest 9 ms after
 * its start), not from when you collect the result.
 *
 * \param sensor a sensor set up with dht22_init()
 * \param[out] reading filled in when the result is DHT22_OK, untouched otherwise
 * \return DHT22_OK, or what went wrong (as for dht22_read()); DHT22_BUSY while
 *         the measurement is still running (try again later);
 *         DHT22_ERROR_NOT_STARTED if no measurement was started, or its result
 *         was already collected
 */
dht22_status_t dht22_read_result(dht22_t *sensor, dht22_reading_t *reading);

/**
 * \brief Describe a status in words, e.g. for printf().
 */
const char *dht22_status_str(dht22_status_t status);

/**
 * \brief Release the PIO state machine, its program memory and the pin.
 */
void dht22_deinit(dht22_t *sensor);

#ifdef __cplusplus
}
#endif

#endif // PICO_SENS_DHT22_H
