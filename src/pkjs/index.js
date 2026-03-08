var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');

var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

Pebble.addEventListener('showConfiguration', function() {
    Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
    if (!e || !e.response) return;

    var settings = clay.getSettings(e.response, false);

    function val(key, fallback) {
        var s = settings[key];
        if (s === null || s === undefined) return fallback;
        var v = (typeof s === 'object' && 'value' in s) ? s.value : s;
        var n = parseInt(v, 10);
        return isNaN(n) ? fallback : n;
    }

    function boolVal(key, fallback) {
        var s = settings[key];
        if (s === null || s === undefined) return fallback ? 1 : 0;
        var v = (typeof s === 'object' && 'value' in s) ? s.value : s;
        return (v === true || v === 1 || v === '1') ? 1 : 0;
    }

    function strVal(key, fallback) {
        var s = settings[key];
        if (s === null || s === undefined) return fallback;
        var v = (typeof s === 'object' && 'value' in s) ? s.value : s;
        return (v === null || v === undefined || v === '') ? fallback : String(v);
    }

    // Keys must match messageKeys order in package.json (10000+)
    var msg = {
        10000: val('theme',            0),
        10001: val('custom_bg',        0xFFFFFF),
        10002: val('custom_lg',        0xAAAAAA),
        10003: val('custom_dg',        0x555555),
        10004: val('custom_fg',        0x000000),
        10005: boolVal('AddZero12h',   false),
        10006: boolVal('RemoveZero24h',false),
        10007: boolVal('BTVibeOn',     true),
        10008: boolVal('showlocalAMPM',true),
        10009: val('time_text_color',  0x000000),
        10010: val('other_text_color', 0x000000),
        10011: val('line_color',       0x000000),
        10012: strVal('LogoText',     'DIVERGENCE'),
        10013: boolVal('ShowLine',     true),
        10014: boolVal('ShowText',     true),
        10015: boolVal('ShowTime',     true),
        10016: boolVal('ShowInfo',     true),
    };

    console.log('Sending: ' + JSON.stringify(msg));

    Pebble.sendAppMessage(msg, function() {
        console.log('Sent OK');
    }, function(err) {
        console.log('Failed: ' + JSON.stringify(err));
    });
});