// Stinger Stream Deck plugin. Talks to the Stream Deck app (keys) and to Stinger (ws://127.0.0.1:52817):
// sends {"type":"press","action":...} on key down, draws every key from Stinger's {"type":"state"} messages.
// Keys are drawn as 144x144 SVG, so they look the same on every Stream Deck model.

const STINGER_URL = 'ws://127.0.0.1:52817';
const PREFIX = 'com.stinger.streamdeck.';

let streamDeck = null;
let stinger = null;
let state = null;              // last state from Stinger, null = not connected
const keys = {};               // context -> { action, settings }
const shown = {};              // context -> last image sent
const titles = {};             // context -> { text, params }: the title typed in the Stream Deck app (kept across pages)
const inspectors = {};         // context -> action, while its settings panel is open
let namesSent = '';            // the playlist / bank names the open panels last got

// Called by the Stream Deck app when it loads the plugin.
function connectElgatoStreamDeckSocket (port, pluginUUID, registerEvent) {
    streamDeck = new WebSocket ('ws://127.0.0.1:' + port);
    streamDeck.onopen = () => streamDeck.send (JSON.stringify ({ event: registerEvent, uuid: pluginUUID }));
    streamDeck.onmessage = (e) => fromStreamDeck (JSON.parse (e.data));
    connectStinger();
}

function fromStreamDeck (m) {
    switch (m.event) {
        case 'willAppear':
            keys[m.context] = { action: m.action, settings: (m.payload && m.payload.settings) || {} };
            draw (m.context);
            break;
        case 'willDisappear':
            delete keys[m.context];
            delete shown[m.context];
            break;
        case 'titleParametersDidChange':
            // the Title box (and its size / colour / font / position) in the Stream Deck app: drawn on the key by us
            titles[m.context] = { text: (m.payload && m.payload.title) || '', params: (m.payload && m.payload.titleParameters) || {} };
            if (keys[m.context]) {
                delete shown[m.context];
                draw (m.context);
            }
            break;
        case 'didReceiveSettings':
            if (keys[m.context]) {
                keys[m.context].settings = (m.payload && m.payload.settings) || {};
                delete shown[m.context];
                draw (m.context);
            }
            break;
        case 'keyDown':
            press (m.context);
            break;
        case 'propertyInspectorDidAppear':
            inspectors[m.context] = m.action;
            sendNames (m.context);
            break;
        case 'propertyInspectorDidDisappear':
            delete inspectors[m.context];
            break;
        case 'sendToPlugin':   // a settings panel asking for the names
            sendNames (m.context);
            break;
    }
}

// The settings panels list playlists and banks by their names in Stinger (empty lists = not connected).
function names () {
    return {
        playlists: state ? (state.playlists || []).map (p => p.name || '') : [],
        banks: state ? (state.banks || []) : []
    };
}

function sendNames (context) {
    const action = inspectors[context] || (keys[context] && keys[context].action);
    if (streamDeck && streamDeck.readyState === WebSocket.OPEN && action)
        streamDeck.send (JSON.stringify ({ event: 'sendToPropertyInspector', action, context, payload: names() }));
}

function namesChanged () {
    const now = JSON.stringify (names());
    if (now === namesSent)
        return;
    namesSent = now;
    for (const context in inspectors)
        sendNames (context);
}

// ---- Stinger ----

function connectStinger () {
    try {
        stinger = new WebSocket (STINGER_URL);
    } catch (e) {
        setTimeout (connectStinger, 2000);
        return;
    }
    stinger.onmessage = (e) => {
        const m = JSON.parse (e.data);
        if (m.type === 'state') {
            state = m;
            drawAll();
            namesChanged();
        }
    };
    stinger.onclose = () => {
        state = null;
        drawAll();
        namesChanged();
        setTimeout (connectStinger, 2000);
    };
    stinger.onerror = () => {};
}

function actionOf (context) {
    const k = keys[context];
    if (! k)
        return null;
    const kind = k.action.substring (PREFIX.length);
    const s = k.settings;
    switch (kind) {
        case 'button': {
            const position = clamp (parseInt (s.position || '1'), 1, 24);
            if (s.mode === 'fixed')
                return 'cue:' + (clamp (parseInt (s.bank || '0'), 0, 7) * 24 + position - 1);
            return 'slot' + position;
        }
        case 'go':       return s.playlist && s.playlist !== 'selected' ? 'go' + (clamp (parseInt (s.playlist), 0, 7) + 1) : 'go';
        case 'fadenext': return 'fadeNext';
        case 'stopall':  return 'stopAll';
        case 'fadeall':  return 'fadeAll';
        case 'selfdestruct':  return 'selfDestruct';
        case 'resetcolours':  return 'resetColours';
        case 'bank':
            if (! s.bank || s.bank === 'next') return 'bankNext';
            if (s.bank === 'previous') return 'bankPrev';
            return 'bank' + (clamp (parseInt (s.bank || '0'), 0, 7) + 1);
    }
    return null;
}

function press (context) {
    if (keys[context] && keys[context].action === PREFIX + 'countdown')
        return;   // display only
    const action = actionOf (context);
    if (! action || ! stinger || stinger.readyState !== WebSocket.OPEN) {
        streamDeck.send (JSON.stringify ({ event: 'showAlert', context }));
        return;
    }
    stinger.send (JSON.stringify ({ type: 'press', action }));
}

// ---- drawing ----

function drawAll () {
    for (const context in keys)
        draw (context);
}

function draw (context) {
    const svg = render (context);
    if (shown[context] === svg)
        return;
    shown[context] = svg;
    streamDeck.send (JSON.stringify ({
        event: 'setImage', context,
        payload: { image: 'data:image/svg+xml;charset=utf8,' + encodeURIComponent (svg), target: 0 }
    }));
}

function render (context) {
    const k = keys[context];
    if (! k)
        return '';
    const kind = k.action.substring (PREFIX.length);
    if (! state)
        return key ('#15181c', '#2c323a', [ text ('STINGER', 72, 62, 18, '#6e7885', true), text ('not running', 72, 92, 16, '#6e7885') ]);
    return renderKind (kind, k.settings, actionOf (context), titleOf (context));
}

/** The key's title from the Stream Deck app (the Title box and its T menu: size, colour, font, position), drawn in
    Stinger's style in place of the key's own label. null = no title: the key's own label. */
function titleOf (context) {
    const t = titles[context];
    if (! t || ! t.params || ! String (t.text || '').trim())   // (not showTitle: the manifest hides Stream Deck's own title, so it reads off)
        return null;
    const p = t.params;
    const style = String (p.fontStyle || '');
    return { text: t.text, size: (p.fontSize || 12) * 2,   // Stream Deck sizes are for 72 px keys, ours are drawn at 144
             colour: validColour (p.titleColor) || '#ffffff', align: p.titleAlignment || 'middle', family: p.fontFamily || '',
             bold: style === '' || style.indexOf ('Bold') >= 0, italic: style.indexOf ('Italic') >= 0, underline: !! p.fontUnderline };
}

/** A key for an action. s = its settings (playlist, bank, keyColour); custom = your own text (null = the key's own label). */
function renderKind (kind, s, action, custom) {
    const own = (label) => custom ? customText (custom) : label;   // your text in place of the key's own label
    switch (kind) {
        case 'button':    return renderButton (action, custom);
        case 'countdown': return renderCountdown (custom);
        case 'go': {
            // the selected playlist, or the one picked in the key's settings; any colour (default green)
            const lists = state.playlists || [];
            const fixed = s.playlist && s.playlist !== 'selected';
            const index = fixed ? clamp (parseInt (s.playlist), 0, 7) : (state.go.playlist || 0);
            const list = fixed ? lists[index] : { name: (lists[index] || {}).name, next: state.go.next, looping: state.go.looping };
            if (! list)
                return key ('#15181c', '#2c323a', [ glyph ('playlist', 10, 8, 18, '#6e7885'), ...own ([ text ('GO', 72, 70, 44, '#4a5260', true) ]),
                                                    text ('no playlist ' + (index + 1), 72, 104, 14, '#6e7885') ]);
            const back = validColour (s.keyColour) || '#2f8f5b';
            const fore = validColour (s.keyColour) ? textOn (back) : '#ffffff';
            const sub = list.looping ? 'finishes loop' : (list.next || 'end of list');
            const parts = [ glyph ('playlist', 10, 8, 18, fore), ...own ([ text ('GO', 72, 74, 44, fore, true) ]),
                            ...lines (sub, 72, 110, 15, fore, 2, 15).map (t => t.replace ('fill-opacity="1"', 'fill-opacity="0.85"')) ];
            if (list.name && (fixed || lists.length > 1))
                parts.push (text (list.name.length > 11 ? list.name.substring (0, 10) + '…' : list.name, 78, 24, 14, fore, true, 'middle', 0.8));
            return key (back, back, parts);
        }
        case 'fadenext':
            return key ('#172636', '#3f6a95', own ([ text ('FADE TO', 72, 62, 22, '#a9d0f7', true), text ('NEXT', 72, 90, 22, '#a9d0f7', true) ]));
        case 'stopall':
            return key (state.anyPlaying ? '#c93a3f' : '#3a1c1e', '#c93a3f', own ([ text ('STOP', 72, 64, 28, '#ffffff', true), text ('ALL', 72, 96, 28, '#ffffff', true) ]));
        case 'fadeall':
            return key ('#262b32', state.anyPlaying ? '#f2a33a' : '#4a5260', own ([ text ('FADE', 72, 64, 26, '#e8eaed', true), text ('ALL', 72, 96, 26, '#e8eaed', true) ]));
        case 'selfdestruct':
            // lit red while the mode is on
            return state.selfDestruct
                ? key ('#8a1c1f', '#e5484d', [ ...own ([ text ('SELF', 72, 52, 24, '#ffffff', true), text ('DESTRUCT', 72, 80, 22, '#ffffff', true) ]), text ('ON', 72, 116, 20, '#ffd1d2', true) ], 8)
                : key ('#1e2228', '#4a5260', [ ...own ([ text ('SELF', 72, 52, 24, '#a7b0bb', true), text ('DESTRUCT', 72, 80, 22, '#a7b0bb', true) ]), text ('OFF', 72, 116, 20, '#6e7885', true) ]);
        case 'resetcolours': {
            const spent = (state.cues || []).filter (c => c.spent).length;
            return key (spent ? '#262b32' : '#15181c', spent ? '#c9d0d8' : '#2c323a',
                        [ ...own ([ text ('RESET', 72, 60, 26, spent ? '#e8eaed' : '#4a5260', true), text ('COLOURS', 72, 90, 20, spent ? '#e8eaed' : '#4a5260', true) ]),
                          text (spent ? spent + ' black' : 'none black', 72, 118, 14, spent ? '#a7b0bb' : '#4a5260') ]);
        }
        case 'bank': {
            if (! s.bank || s.bank === 'next' || s.bank === 'previous')
                return key ('#1e2228', '#2c323a', [ ...own ([ text (s.bank !== 'previous' ? 'BANK ▶' : '◀ BANK', 72, 66, 24, '#c9d0d8', true) ]),
                                                    text (bankLetter (state.bank) + ' · ' + (state.banks[state.bank] || ''), 72, 98, 14, '#a7b0bb') ]);
            const b = clamp (parseInt (s.bank || '0'), 0, 7);
            const current = b === state.bank;
            return key (current ? '#2a2418' : '#1e2228', current ? '#f2a33a' : '#2c323a',
                        [ ...own ([ text (bankLetter (b), 72, 70, 44, current ? '#f5c77e' : '#c9d0d8', true) ]),
                          ...lines (state.banks[b] || '', 72, 106, 15, current ? '#f5c77e' : '#a7b0bb', 2, 15) ]);
        }
    }
    return key ('#15181c', '#2c323a', []);
}

function renderButton (action, custom) {
    let cue = null, label = '';
    if (action.startsWith ('cue:')) {
        const id = parseInt (action.substring (4));
        cue = state.cues.find (c => c.id === id) || null;
        label = bankLetter (Math.floor (id / 24)) + ((id % 24) + 1);
    } else {
        const slot = parseInt (action.substring (4)) - 1;
        cue = state.cues.find (c => c.bank === state.bank && c.slot === slot) || null;
        label = bankLetter (state.bank) + (slot + 1);
    }
    if (! cue)
        return key ('#0e1013', '#2c323a', [ glyph ('button', 10, 10, 18, '#4a5260'),
                                            ...(custom ? customText (custom) : [ text (label, 72, 66, 22, '#4a5260', true), text ('empty', 72, 94, 15, '#4a5260') ]) ]);

    const parts = [];
    if (cue.playing && cue.waitLeft !== undefined) {
        // over, waiting before the playlist's next song
        parts.push (...(custom ? customText (custom) : lines (cue.name, 72, 34, 16, '#a9d0f7', 2, 16)));
        parts.push (text ('NEXT IN', 72, 76, 16, '#a9d0f7', true));
        parts.push (text (time (cue.waitLeft), 72, 112, 34, '#ffffff', true));
        return key ('#172636', '#5aa9f0', parts, 8);
    }
    if (cue.playing) {
        // playing: the whole key in the button's colour, the countdown big
        const back = cue.stopping ? shade (cue.colour, 0.55) : cue.colour;
        const fore = textOn (back);
        parts.push (...(custom ? customText (custom) : lines (cue.name, 72, 34, 16, fore, 2, 16)));
        parts.push (text (cue.looping ? '↻ LOOP' : '-' + time (cue.remaining), 72, 98, cue.looping ? 28 : 38, fore, true));
        parts.push ('<rect x="12" y="116" width="120" height="10" rx="5" fill="' + fore + '" fill-opacity="0.25"/>');
        parts.push ('<rect x="12" y="116" width="' + Math.round (120 * Math.min (1, cue.progress || 0)) + '" height="10" rx="5" fill="' + fore + '"/>');
        return key (back, fore, parts, 8);
    }
    // idle: a dark shade of the button's colour with its name (Self Destruct: spent = solid black)
    const back = cue.spent ? '#000000' : shade (cue.colour, 0.42);
    const fore = textOn (back);
    parts.push ('<rect x="0" y="0" width="144" height="14" fill="' + cue.colour + '"/>');
    parts.push (glyph ('button', 10, 20, 18, fore));
    parts.push (...(custom ? customText (custom) : lines (cue.name, 72, 54, 20, fore, 3, 22)));
    parts.push (text (label, 14, 132, 13, fore, false, 'start', 0.6));
    if (cue.next) {
        parts.push ('<rect x="84" y="118" width="52" height="20" rx="4" fill="#2f8f5b"/>');
        parts.push (text ('NEXT', 110, 133, 13, '#ffffff', true));
    }
    return key (back, cue.next ? '#2f8f5b' : cue.spent ? '#2c323a' : shade (cue.colour, 0.8), parts);
}

/** The sting started last, in its colour: name (or your text), time left (tenths in the last 10 s), progress. */
function renderCountdown (custom) {
    const now = state.now;
    if (! now)
        return key ('#0e1013', '#2c323a', [ ...(custom ? customText (custom) : []), text ('--:--', 72, 78, 34, '#4a5260', true), text ('nothing playing', 72, 106, 14, '#4a5260') ]);
    if (now.waitLeft !== undefined) {
        // a playlist Wait: counting down to the next song
        const big = now.waitLeft < 10 ? Number (now.waitLeft).toFixed (1) : time (now.waitLeft);
        return key ('#172636', '#5aa9f0', [ ...(custom ? customText (custom) : [ text ('NEXT IN', 72, 30, 16, '#a9d0f7', true) ]),
                                            text (big, 72, 80, 40, '#ffffff', true),
                                            ...lines (now.nextName || 'next song', 72, 110, 14, '#a9d0f7', 2, 15) ], 8);
    }
    const ending = ! now.looping && now.remaining < 10;
    const back = now.stopping ? shade (now.colour, 0.55) : now.colour;
    const fore = textOn (back);
    const parts = [];
    parts.push (...(custom ? customText (custom) : lines (now.name, 72, 34, 16, fore, 2, 16)));
    let big;
    if (now.looping)
        big = '↻ LOOP';
    else if (now.remaining < 10)
        big = '-' + Number (now.remaining).toFixed (1);
    else
        big = '-' + time (now.remaining);
    parts.push (text (big, 72, 96, now.looping ? 28 : 38, fore, true));
    parts.push ('<rect x="12" y="114" width="120" height="10" rx="5" fill="' + fore + '" fill-opacity="0.25"/>');
    parts.push ('<rect x="12" y="114" width="' + Math.round (120 * Math.min (1, now.progress || 0)) + '" height="10" rx="5" fill="' + fore + '"/>');
    if (now.others > 0)
        parts.push (text ('+' + now.others + ' more', 132, 138, 11, fore, false, 'end', 0.8));
    // last 10 seconds: a red frame
    return key (back, ending && ! now.stopping ? '#ff2d2d' : fore, parts, 8);
}

/** Text as key lines: Enter starts a new line, a line too wide for the key wraps at a word (a word too long on its own is split). */
function wrapText (s, size) {
    const perLine = Math.max (3, Math.floor (126 / (size * 0.58)));
    const out = [];
    for (const raw of String (s).split (/\r?\n/)) {
        let line = '';
        for (let w of raw.split (/\s+/).filter (w => w.length > 0)) {
            const candidate = line ? line + ' ' + w : w;
            if (candidate.length <= perLine) {
                line = candidate;
                continue;
            }
            if (line)
                out.push (line);
            while (w.length > perLine) {
                out.push (w.substring (0, perLine));
                w = w.substring (perLine);
            }
            line = w;
        }
        out.push (line);
    }
    return out;
}

/** Your own text: lines (long ones wrap at words), size (px on the 144 px key), colour, top / middle / bottom, font,
    bold / italic / underline. */
function customText (t) {
    const size = clamp (Math.round (t.size), 8, 72);
    const all = wrapText (t.text, size).slice (0, 8);
    const lineHeight = size * 1.1;
    const block = lineHeight * (all.length - 1);
    let y;
    if (t.align === 'top')
        y = 10 + size * 0.85;
    else if (t.align === 'bottom')
        y = 134 - size * 0.22 - block;
    else
        y = 72 + size * 0.35 - block / 2;
    const family = t.family ? escape (t.family) + ', ' : '';
    return all.map ((l, i) => '<text x="72" y="' + (y + i * lineHeight).toFixed (1) + '" font-family="' + family + 'Segoe UI, -apple-system, Helvetica, Arial, sans-serif"'
                            + ' font-size="' + size + '" font-weight="' + (t.bold === false ? '400' : '700') + '"'
                            + (t.italic ? ' font-style="italic"' : '') + (t.underline ? ' text-decoration="underline"' : '')
                            + ' fill="' + t.colour + '" text-anchor="middle">' + escape (l) + '</text>');
}

/** A small corner icon saying what kind of key it is (a button / a playlist), so keys with your own text or colour stay recognisable. */
function glyph (kind, x, y, size, colour) {
    const k = size / 24;
    const n = (v) => (v * k).toFixed (1);
    const at = '<g transform="translate(' + x + ',' + y + ')" opacity="0.8"';
    if (kind === 'button')   // a pad
        return at + ' fill="none" stroke="' + colour + '" stroke-width="' + n (2.2) + '">'
             + '<rect x="' + n (2) + '" y="' + n (2) + '" width="' + n (20) + '" height="' + n (20) + '" rx="' + n (4.5) + '"/>'
             + '<rect x="' + n (7.5) + '" y="' + n (7.5) + '" width="' + n (9) + '" height="' + n (9) + '" rx="' + n (2) + '" fill="' + colour + '" stroke="none"/></g>';
    // a playlist: a list with a play triangle
    return at + ' stroke="' + colour + '" stroke-width="' + n (2.6) + '" stroke-linecap="round" fill="' + colour + '">'
         + '<line x1="' + n (2) + '" y1="' + n (4) + '" x2="' + n (16) + '" y2="' + n (4) + '"/>'
         + '<line x1="' + n (2) + '" y1="' + n (11) + '" x2="' + n (16) + '" y2="' + n (11) + '"/>'
         + '<line x1="' + n (2) + '" y1="' + n (18) + '" x2="' + n (9) + '" y2="' + n (18) + '"/>'
         + '<path d="M' + n (13) + ' ' + n (14) + ' L' + n (23) + ' ' + n (19) + ' L' + n (13) + ' ' + n (24) + ' Z" stroke="none"/></g>';
}

function validColour (c) {
    return /^#[0-9a-fA-F]{6}$/.test (String (c || '')) ? String (c) : null;
}

// ---- colours ----

function rgb (hex) {
    const h = String (hex).replace ('#', '');
    return [ parseInt (h.substring (0, 2), 16) || 0, parseInt (h.substring (2, 4), 16) || 0, parseInt (h.substring (4, 6), 16) || 0 ];
}

/** The button colour mixed into the dark background (amount 0 = background, 1 = the colour). */
function shade (hex, amount) {
    const ground = [14, 16, 19], c = rgb (hex);
    return '#' + c.map ((v, i) => Math.round (ground[i] + (v - ground[i]) * amount).toString (16).padStart (2, '0')).join ('');
}

/** Black or white, whichever reads better on the colour. */
function textOn (hex) {
    const [r, g, b] = rgb (hex);
    return (0.299 * r + 0.587 * g + 0.114 * b) / 255 > 0.62 ? '#0e1013' : '#ffffff';
}

// ---- SVG helpers ----

function key (background, border, parts, borderWidth) {
    const w = borderWidth || 6;
    return '<svg xmlns="http://www.w3.org/2000/svg" width="144" height="144" viewBox="0 0 144 144">'
         + '<rect x="0" y="0" width="144" height="144" fill="' + background + '"/>'
         + parts.join ('')
         + '<rect x="' + (w / 2) + '" y="' + (w / 2) + '" width="' + (144 - w) + '" height="' + (144 - w) + '" rx="10" fill="none" stroke="' + border + '" stroke-width="' + w + '"/>'
         + '</svg>';
}

function text (s, x, y, size, colour, bold, anchor, opacity) {
    return '<text x="' + x + '" y="' + y + '" font-family="Segoe UI, Helvetica, Arial, sans-serif" font-size="' + size
         + '" font-weight="' + (bold ? '700' : '400') + '" fill="' + colour + '" fill-opacity="' + (opacity || 1) + '" text-anchor="' + (anchor || 'middle') + '">'
         + escape (s) + '</text>';
}

/** Word-wraps s into at most maxLines centred lines starting at y (the last one gets an ellipsis if it's cut). */
function lines (s, x, y, size, colour, maxLines, lineHeight) {
    const perLine = Math.max (6, Math.floor (126 / (size * 0.56)));
    const words = String (s).split (/\s+/).filter (w => w.length > 0);
    const out = [];
    let line = '';
    for (const w of words) {
        const candidate = line ? line + ' ' + w : w;
        if (candidate.length <= perLine) {
            line = candidate;
            continue;
        }
        if (line)
            out.push (line);
        line = w.length > perLine ? w.substring (0, perLine) : w;
    }
    if (line)
        out.push (line);
    if (out.length > maxLines) {
        out.length = maxLines;
        out[maxLines - 1] = out[maxLines - 1].substring (0, perLine - 1) + '…';
    }
    return out.map ((l, i) => text (l, x, y + i * lineHeight, size, colour, true));
}

function escape (s) {
    return String (s).replace (/&/g, '&amp;').replace (/</g, '&lt;').replace (/>/g, '&gt;').replace (/"/g, '&quot;');
}

function time (seconds) {
    const s = Math.max (0, Math.ceil (seconds || 0));
    return Math.floor (s / 60) + ':' + String (s % 60).padStart (2, '0');
}

function bankLetter (b) {
    return String.fromCharCode (65 + b);
}

function clamp (v, lo, hi) {
    return isNaN (v) ? lo : Math.max (lo, Math.min (hi, v));
}
