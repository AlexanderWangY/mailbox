# Mailbox firmware

ESP-IDF firmware for an Adafruit ESP32 Feather V2, ST7796 display, FT6336U
touch controller, and DRV2605L haptic driver. The HTTP backend is in `backend/`.

## App behavior

- Poll mail at startup and every ten minutes. Reconnecting Wi-Fi does not reset
  that schedule. Reading a message also requests the next unread message.
- Dim after 30 seconds without interaction; turn the display off after 45.
- Show the empty screen when the inbox is empty, otherwise the mail screen.
  Tap the mail screen to read the first message. Tap the reading screen to
  dismiss it and show either the next mail screen or the empty screen.
- The first tap on a dimmed or off display restores brightness without
  navigating. Sleeping while reading preserves that message.
- Sleep until either the next poll deadline or a touch. A timer wake polls with
  the display off and returns to sleep as soon as the work finishes. New mail
  wakes the display and plays a haptic effect.
- Tapping during a display-off poll lights the screen immediately, cancels the
  old request, and starts a fresh normal poll after the old worker completes.
  Results from the canceled poll are discarded.

## Code structure

`components/app/state.c` owns the screen, power, poll state, five-message inbox,
and absolute deadlines. `app.c` handles three events: touch, Wi-Fi ready, and
mail result. Its queue wait expires at the next deadline; there are no app
software timers.

`components/app/power.c` owns Wi-Fi shutdown, light sleep, and wake ordering.
`components/display/touch.c` owns the interrupt pin and consumes the wake press
until release. Its ISR masks the pin until the input task services it, bounding
interrupt bursts. It waits for interrupts while idle and checks for release
while a finger is held down.

`components/mail/rest.c` owns HTTP clients on one worker task. Cancellation uses
a flag during clock synchronization and connection setup, and socket shutdown
to interrupt connected reads. A mutex protects the published socket descriptor;
only the worker closes and frees the HTTP client.

Messages are stored in RAM. The backend remains the persistent source of unread
mail. Its current `/api/mail/next` endpoint returns the oldest unread message,
so ordinary use fetches one at a time; duplicate IDs are ignored locally. Failed
mark-read requests restore the message to the inbox. Battery percentage is an
estimate from GPIO 35; charging status needs an additional hardware connection.

## Build and check

```sh
source ~/esp/esp-idf/export.sh
idf.py build
sh tests/run.sh
```

The host tests run production app, state, power, and REST code against simulated
time and hardware/network effects, with undefined-behavior checks enabled. They
cover deadlines, queue navigation, failed read acknowledgments, background poll
cancellation and late results, socket lifetime, and wake ordering.

On hardware, verify a tap during Wi-Fi shutdown, a timer wake and display-off
poll, a tap during that poll, and several long sleep cycles. Host tests cannot
verify the physical touch IRQ signal or panel behavior.
