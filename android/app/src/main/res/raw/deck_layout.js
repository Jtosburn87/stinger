// Stinger's DeX deck (Android only): put into the Wi-Fi remote's page by DeckActivity once it has loaded, so the
// page itself (also served to phones by the computer app) doesn't change. It makes the most of the phone's screen:
//  - the keys are laid out in whichever columns x rows makes them biggest (on a landscape phone 24 keys go
//    8 x 3 rather than Stinger's 6 x 4), in Stinger's order (left to right, top to bottom)
//  - the header and bank tabs are slimmer, so the keys get the height
(function () {
    if (window.stingerDeck)
        return;
    window.stingerDeck = true;

    var css = document.createElement ('style');
    css.textContent =
        'header { padding: 4px 8px; }'
      + 'header button { padding: 4px 9px; font-size: 12px; }'
      + '.logo { font-size: 13px; letter-spacing: 1px; }'
      + '#banks { padding: 4px 6px; }'
      + '#banks button { padding: 5px 4px; font-size: 12px; }'
      + 'main { padding: 4px; }';
    document.head.appendChild (css);

    var gap = 6;

    // the arrangement that gives the biggest square keys in w x h
    function best (n, w, h) {
        var result = { cols: 1, rows: n, cell: 0 };
        for (var cols = 1; cols <= n; ++cols) {
            var rows = Math.ceil (n / cols);
            if ((rows - 1) * cols >= n)
                continue;   // a whole empty row: never the best
            var cell = Math.floor (Math.min ((w - (cols - 1) * gap) / cols, (h - (rows - 1) * gap) / rows));
            if (cell > result.cell)
                result = { cols: cols, rows: rows, cell: cell };
        }
        return result;
    }

    window.fit = function () {
        var main = document.querySelector ('main');
        var grid = document.getElementById ('grid');
        if (! main || ! grid || typeof SIZES === 'undefined')
            return;
        var b = best (SIZES[device].keys, main.clientWidth - 8, main.clientHeight - 8);
        grid.style.gap = gap + 'px';
        grid.style.gridGap = gap + 'px';
        grid.style.gridTemplateColumns = 'repeat(' + b.cols + ', ' + b.cell + 'px)';
        grid.style.gridAutoRows = b.cell + 'px';
    };

    // the page's own resize handler runs its old layout first: this one runs after it
    addEventListener ('resize', function () { setTimeout (window.fit, 0); });
    window.fit();
}) ();
