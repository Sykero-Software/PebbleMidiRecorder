var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// Pause the watch's idle auto-exit while the phone config page is open. No watch
// buttons are pressed during config, so the idle timer would otherwise fire and
// kill the app (and PKJS with it), closing the config page and losing unsaved
// changes. Clay's own showConfiguration/webviewclosed listeners still run;
// Pebble allows multiple listeners per event.
Pebble.addEventListener('showConfiguration', function () {
  Pebble.sendAppMessage({ CFG_OPEN: 1 });
});
Pebble.addEventListener('webviewclosed', function () {
  Pebble.sendAppMessage({ CFG_OPEN: 0 }); // resume idle (also on cancel)
});
