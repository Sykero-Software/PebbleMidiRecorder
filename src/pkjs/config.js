module.exports = [
  {
    "type": "heading",
    "defaultValue": "MIDI Recorder"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Behavior"
      },
      {
        "type": "toggle",
        "messageKey": "CFG_AUTO_CLOSE",
        "label": "Close app after starting/stopping recording",
        "description": "When on, the app closes itself as soon as a Start or Stop command has been sent to the phone.",
        "defaultValue": true
      },
      {
        "type": "select",
        "messageKey": "CFG_IDLE_EXIT_SEC",
        "label": "Return to watchface when idle",
        "description": "Close back to the watchface after this many seconds with no button press. Recording continues on the phone. Off disables it.",
        "defaultValue": "15",
        "options": [
          { "label": "Off", "value": "0" },
          { "label": "10 seconds", "value": "10" },
          { "label": "15 seconds", "value": "15" },
          { "label": "30 seconds", "value": "30" },
          { "label": "60 seconds", "value": "60" }
        ]
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
