# MIDI Recorder

Pebble watchapp companion to the
[AndroidMidiRecorder](https://github.com/Sykero-Software/AndroidMidiRecorder) app.
Starts/stops MIDI recording from the wrist and shows the current recording +
BLE-device connection state. Talks to `fi.sykero.midirecorder` via PebbleKit
Android 2.

App name: **MIDI Recorder** · UUID `4F677262-4364-4F8F-800A-E0FB6C439096`.

**[Install from the Pebble appstore →](https://apps.repebble.com/midi-recorder_4e0ab343fe694b889ef1808d)**

## Building & running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator diorite     # install on an emulator (NOT basalt)
pebble install --cloudpebble app.pbw  # install to a paired phone via the cloud relay
```

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>

## Support

Questions, feedback or bug reports: <pebble.midirecord@sykero.fi>

Browse all Sykerö Software apps on the Pebble appstore:
<https://apps.repebble.com/apps/dev/syker-software_9f6c9c6e9ce88af6a0db953e>
