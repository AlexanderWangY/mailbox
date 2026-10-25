#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_build_dir=$(mktemp -d /tmp/mailbox-tests.XXXXXX)
trap 'rm -rf "$test_build_dir"' EXIT HUP INT TERM

# Small platform declarations let the production app and state code run on the
# host with simulated time, display effects, and REST completions.
for header in board.h esp_err.h esp_event.h esp_log.h esp_netif.h esp_timer.h esp_sleep.h \
              freertos/FreeRTOS.h freertos/queue.h freertos/task.h freertos/semphr.h; do
  mkdir -p "$test_build_dir/$(dirname "$header")"
  printf '#include "test_platform.h"\n' > "$test_build_dir/$header"
done
for header in esp_crt_bundle.h esp_http_client.h esp_heap_caps.h esp_netif_sntp.h lwip/sockets.h; do
  mkdir -p "$test_build_dir/$(dirname "$header")"
  printf '#include "test_http_platform.h"\n' > "$test_build_dir/$header"
done

cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -fsanitize=undefined -g -Wl,--gc-sections \
  -I"$test_build_dir" -Itests -Icomponents/app/include -Icomponents/mail/include \
  -Icomponents/display/include -Icomponents/haptic/include \
  tests/test_app.c components/app/state.c -o "$test_build_dir/test_app"
"$test_build_dir/test_app"

cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -fsanitize=undefined -g -Wl,--gc-sections \
  -I"$test_build_dir" -Itests -Icomponents/mail/include \
  tests/test_rest.c -o "$test_build_dir/test_rest"
"$test_build_dir/test_rest"

cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -fsanitize=undefined -g -Wl,--gc-sections \
  -I"$test_build_dir" -Itests -Icomponents/app/include -Icomponents/mail/include \
  -Icomponents/display/include tests/test_power.c -o "$test_build_dir/test_power"
"$test_build_dir/test_power"
