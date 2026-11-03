# pico_sens: DHT22 temperature and humidity sensor library

A small library for the Raspberry Pi Pico (RP2040) that reads the temperature and the humidity from a DHT22-compatible sensor, sold as AM2320. The timing-critical part runs in one of the Pico's PIO state machines, so the rest of your program (and USB) cannot disturb it.

## Wiring

Hold the sensor with the grille facing you and the pins pointing down. The pins are numbered 1 to 4 from the left. **Wire pin 4 before plugging in the USB cable.**

| AM2320 pin  | Pico pin           |
| ----------- | ------------------ |
| 1 (VDD)     | 3V3(OUT)           |
| 2 (SDA)     | GP15 (or any GPIO) |
| 3 (GND)     | GND                |
| 4 (SCL)     | GND                |

Note: Use the same wiring for DHT22.

Pin 4 must be connected to ground for the single-wire communication mode of the AM2320. You do not need an external pull-up resistor: the library turns on the Pico's own pull-up resistor on the data pin.

## Adding the library to your project

1. Copy this `pico_sens` directory into your project directory, next to your `CMakeLists.txt`:

   ```
   my_project/
   ├── CMakeLists.txt
   ├── main.c
   ├── pico_sdk_import.cmake
   └── pico_sens/          <- this directory
   ```

2. In your `CMakeLists.txt`, add this line **after** `pico_sdk_init()`:

   ```cmake
   add_subdirectory(pico_sens)
   ```

3. Add `pico_sens` to the libraries in your existing `target_link_libraries(...)` line, for example:

   ```cmake
   target_link_libraries(my_target pico_stdlib hardware_pwm hardware_adc pico_rand pico_sens)
   ```

4. In your C file:

   ```c
   #include "pico_sens/dht22.h"
   ```

`add_subdirectory` reads `pico_sens/CMakeLists.txt`, which describes the library: its source files, where its header is, and which SDK libraries it needs. Linking `pico_sens` to your program brings all of that in, exactly like `hardware_pwm` does.

The header is in the directory `include/pico_sens/`, so the `#include` starts with the library's name, just like `"pico/stdlib.h"` and `"hardware/pwm.h"` from the SDK. That way, it is always clear which library a header comes from, and two libraries can each have a header with the same name.

## Using it

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico_sens/dht22.h"

const uint DHT22_PIN = 15;

int main(void) {
    stdio_init_all();

    dht22_t sensor;
    dht22_init(&sensor, DHT22_PIN);

    while (true) {
        dht22_reading_t reading;
        dht22_status_t status = dht22_read(&sensor, &reading);

        if (status == DHT22_OK) {
            printf("temperature: %.1f C, humidity: %.1f %%\n", reading.temperature, reading.humidity);
        } else if (status != DHT22_ERROR_TOO_SOON) {
            printf("DHT22 error: %s\n", dht22_status_str(status));
        }
        // ... the rest of your superloop ...
    }
}
```

| Function | What it does |
| --- | --- |
| `bool dht22_init(dht22_t *sensor, uint pin)` | Sets up a sensor on a GPIO pin. Returns `false` if the PIO has no room left (see [Limits](#limits)). |
| `dht22_status_t dht22_read(dht22_t *sensor, dht22_reading_t *reading)` | Measures. On `DHT22_OK`, `reading->temperature` (°C) and `reading->humidity` (%) are filled in. Takes about 6 ms. |
| `dht22_status_t dht22_read_start(dht22_t *sensor)` | Starts a measurement and returns at once (see [Without waiting](#without-waiting)). |
| `bool dht22_read_done(dht22_t *sensor)` | `true` when the started measurement is over and its result is waiting. |
| `dht22_status_t dht22_read_result(dht22_t *sensor, dht22_reading_t *reading)` | Collects the result of the started measurement, the same way `dht22_read()` returns it. |
| `const char *dht22_status_str(dht22_status_t status)` | Describes a status in words, for `printf()`. |
| `void dht22_deinit(dht22_t *sensor)` | Releases the PIO state machine and the pin. |

The functions return one of these:

| Status | Meaning |
| --- | --- |
| `DHT22_OK` | Success: the reading is filled in. |
| `DHT22_ERROR_TOO_SOON` | Not yet: the sensor needs 1 s after power-up, and a 2 s break after every measurement. Nothing is wrong, try again later. |
| `DHT22_ERROR_NO_RESPONSE` | The sensor did not answer. Check the wiring, the power and the pin number. |
| `DHT22_ERROR_INCOMPLETE` | The sensor stopped answering halfway (a loose wire?). |
| `DHT22_ERROR_CHECKSUM` | Data arrived, but it was corrupted on the way (noise?). |
| `DHT22_BUSY` | Only when you use `dht22_read_start()`: a measurement is running, or its result was not collected with `dht22_read_result()` yet. |
| `DHT22_ERROR_NOT_STARTED` | `dht22_read_result()` was called, but no measurement was started (or its result was already collected). |

On any status other than `DHT22_OK`, `reading` is left as it was.

`DHT22_ERROR_TOO_SOON` is what makes the superloop above work: `dht22_read()` returns it at once (without talking to the sensor) until the sensor is ready again, so the loop never waits for the sensor's 2 s break. If you only call `dht22_read()` every 2 seconds anyway, e.g. with `sleep_ms(2000)` in the loop, you will only see it once: on the very first call, if your program reaches it within the first second after power-up.

### Without waiting

`dht22_read()` waits about 6 ms for the sensor's answer. Usually that is fine. If your superloop must react faster (to a button, for example), let the measurement run on its own instead: the PIO does it anyway, so the CPU only has to start it and collect the result.

```c
while (true) {
    dht22_read_start(&sensor);   // starts a measurement whenever the sensor allows it

    // do other things in the meantime

    // check if the measurement is ready
    if (dht22_read_done(&sensor)) {
        dht22_reading_t reading;
        // read the result
        dht22_status_t status = dht22_read_result(&sensor, &reading);

        if (status == DHT22_OK) {
            printf("temperature: %.1f C, humidity: %.1f %%\n", reading.temperature, reading.humidity);
        } else {
            printf("DHT22 error: %s\n", dht22_status_str(status));
        }
    }
    // ... the rest of your superloop ...
}
```

1. `dht22_read_start()` starts a measurement and returns at once. When it cannot start one, it says why: `DHT22_ERROR_TOO_SOON` (the sensor needs its break) or `DHT22_BUSY` (a measurement is running, or its result was not collected yet). In the loop above, both just mean "not now", so the loop ignores them.
2. `dht22_read_done()` returns `true` when the measurement is over, about 6 ms after the start.
3. `dht22_read_result()` returns the result exactly as `dht22_read()` would: `DHT22_OK` with the reading, or an error. If you call it too early, it returns `DHT22_BUSY`, and you can try again later.

None of the three functions waits. You can collect the result as late as you like, but until you do, `dht22_read_start()` does not start a new measurement. The sensor's 2 s break counts from the end of the measurement, not from when you collect the result.

## How it works

### The protocol

The DHT22 talks over a single wire, in both directions. A pull-up resistor keeps the wire high (3.3 V) when nobody pulls it low. The Pico and the sensor take turns pulling it low:

```
    start            response      bit = 0     bit = 1
────┐         ┌──────┐      ┌──────┐     ┌─────┐     ┌────────────┐
    │         │      │      │      │     │     │     │            │
    └─────────┘      └──────┘      └─────┘     └─────┘            └──── ...
     >= 1 ms   20-40  80 us  80 us  50 us 26-28 50 us 70 us
     (Pico)    us                         us
```

1. **Start signal.** The Pico pulls the wire low for at least 1 ms (this library: 2 ms), then lets go.
2. **Response.** The sensor pulls the wire low for 80 µs, then lets go for 80 µs: "I'm here, data follows".
3. **40 bits of data.** Every bit starts with 50 µs low. Then the wire is high for a short time (26-28 µs) for a `0`, or a long time (70 µs) for a `1`. After the last bit, the sensor lets go and the wire goes high again.

The 40 bits are 5 bytes, most significant bit first:

| Bytes | Meaning | Example |
| --- | --- | --- |
| 1 and 2 | humidity in tenths of a percent | `0x02 0x8C` = 652 → 65.2 % |
| 3 and 4 | temperature in tenths of a degree; the top bit is the sign (1 = below zero) | `0x01 0x5F` = 351 → 35.1 °C (`0x81 0x5F` would be −35.1 °C) |
| 5 | checksum: the sum of bytes 1 to 4, cut to 8 bits | `0x02 + 0x8C + 0x01 + 0x5F` = `0xEE` |

If the checksum does not match, a bit got flipped on the way. The checksum has a blind spot, though: when every bit is read as 0, the sum is 0 as well. A real sensor never measures 0.0 % at 0.0 °C, so the library treats all zeros as an error too.

> **Datasheets can be wrong.** The DHT22 datasheet describes the data as "8 bit integral RH data + 8 bit decimal RH data + ...". That is how the DHT22's little brother, the DHT11, works. The text was copied from the DHT11 datasheet, and it is wrong for the DHT22, which sends 16-bit numbers in tenths, as in the table above. The datasheet does not mention the sign bit either. When a datasheet does not make sense, look for a second source: here, the AM2302 datasheet from the same manufacturer.

### Why PIO?

The only difference between a `0` and a `1` is how long a pulse lasts: 27 µs or 70 µs. The CPU could measure that with a loop that watches the pin and the timer, but only if nothing interrupts it for more than about 20 µs, and the USB connection that `printf()` uses interrupts the CPU all the time.

The RP2040 has two PIO blocks with 4 state machines each: tiny processors that run very short programs, one instruction per clock cycle, independent of the CPU. They are made for exactly this: bit-level protocols with precise timing. The library runs its state machine at 1 MHz, so every PIO cycle takes 1 µs.

### The PIO program (`src/dht22.pio`)

The program has three parts, matching the protocol:

1. It waits (`pull block`) until the CPU puts the length of the start signal in its TX FIFO, pulls the wire low, and counts down (`jmp x--`, 1 µs per round).
2. It lets go of the wire and follows the response with `wait 0 pin 0` / `wait 1 pin 0`: each one stalls until the pin has the given level.
3. For each of the 40 bits, it waits for the wire to go high, waits 48 µs more (the `[31]` and `[15]` after the instructions are extra delay cycles), and then samples the pin with `in pins, 1`. Still high means a long pulse, so a `1`. The 48 µs sits halfway between the longest `0` (30 µs) and the shortest `1` (68 µs). After every 16 bits, the state machine pushes them into its RX FIFO automatically (autopush): first the humidity, then the temperature. The last 8 bits (the checksum) do not make a full 16, so the program's last instruction (`push`) pushes them itself.

Why 3 pieces and not 5 bytes? The RX FIFO holds only 4 entries. With 5 bytes, the state machine would have to stop and wait until the CPU makes room for the last one. Three pieces always fit, so the CPU can collect them whenever it likes. That is what makes `dht22_read_start()` possible.

The C side (`src/dht22.c`): `dht22_read_start()` restarts the state machine and writes the start-signal length to the TX FIFO. `dht22_read_done()` checks whether all 3 pieces are in the RX FIFO, or 9 ms have passed (then something went wrong). `dht22_read_result()` takes the pieces out of the RX FIFO and decodes them. `dht22_read()` does all three, and waits in between.

### Why the Pico never drives the wire high

The program only ever switches the pin between *output* (with the output value fixed at 0, so the wire is pulled low) and *input* (the Pico lets go). It never drives the wire high. If it did, at a moment when the sensor pulls the wire low, the two would be fighting each other: a short circuit. Letting go and leaving the "high" to the pull-up resistor is how all such shared wires work (it is called *open drain*; I²C works the same way).

## Limits

- Needs Pico SDK 2.0 or newer.
- Each sensor needs one PIO state machine and 18 of a PIO block's 32 instruction slots, so at most two sensors (one per PIO block), and fewer if your program uses PIO for other things too.
- The PIO clock is set from the system clock in `dht22_init()`. If you change the system clock (`set_sys_clock_khz()`), do it before `dht22_init()`.

## License

MIT, see [LICENSE](LICENSE). If you use this library, keep the license file and the copyright notices in the source files.

The library was inspired by [pico_dht](https://github.com/vmilea/pico_dht) by Valentin Milea (MIT license), but works differently (it samples each bit instead of timing it, and needs no DMA) and contains no code from it.
