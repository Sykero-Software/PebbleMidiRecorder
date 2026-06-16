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
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
