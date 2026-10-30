# Slap Timer (Pebble Time 2)

| Input | Action |
|---|---|
| Up / Down | add / subtract 5 seconds (hold to repeat) |
| Tap anywhere | count down from the set value to 0, vibrate, then wait |
| Tap again | restart the same countdown |

Taps are ignored while counting; Up/Down are ignored while counting. Back exits.
The chosen value is remembered between launches. Times of 60s or more show as m:ss.

## Build
    cd slap-timer
    pebble build
    pebble install --emulator emery     # or --phone <ip>
