// motd markup: [b], [i], [u], [c=#hex], [c=name], [g=#a,#b], [g=#a,#b,#c],
//              [pulse], [flash], [wave], [scroll], [center], [right], \n

var MOTD_MAX_RAW = 256;
var MOTD_MAX_LINES = 4;

var MOTD_COLOR_PRESETS = {
    red: '#FF5555', green: '#55FF55', blue: '#5555FF', yellow: '#FFFF55',
    gold: '#FFAA00', aqua: '#55FFFF', pink: '#FF55FF', orange: '#FF8C00',
    white: '#FFFFFF', gray: '#AAAAAA', dark_red: '#AA0000', dark_green: '#00AA00',
    dark_blue: '#0000AA', dark_aqua: '#00AAAA', dark_purple: '#AA00AA', black: '#000000'
};

function parseMotd(raw) {
    if (!raw) return [];
    var lines = raw.split('\n').slice(0, MOTD_MAX_LINES);
    var result = [];
    for (var li = 0; li < lines.length; li++) {
        result.push(parseMotdLine(lines[li]));
    }
    return result;
}

function parseMotdLine(line) {
    var align = 'left';
    var m;
    if ((m = line.match(/^\[center\]([\s\S]*)$/))) {
        align = 'center'; line = m[1].replace(/\[\/center\]$/, '');
    } else if ((m = line.match(/^\[right\]([\s\S]*)$/))) {
        align = 'right'; line = m[1].replace(/\[\/right\]$/, '');
    }
    return { align: align, nodes: parseMotdTokens(line) };
}

var MOTD_TAGS = {
    'b':      { type: 'bold' },
    'i':      { type: 'italic' },
    'u':      { type: 'underline' },
    'pulse':  { type: 'anim', anim: 'pulse' },
    'flash':  { type: 'anim', anim: 'flash' },
    'wave':   { type: 'anim', anim: 'wave' },
    'center': { type: 'align', align: 'center' },
    'right':  { type: 'align', align: 'right' },
};

function tokenizeMotd(str) {
    var tokens = [];
    var i = 0;
    var tagRe = /\[\/?[a-z]+(?:=[^\[\]]{0,80})?\]/g;
    var match;
    while ((match = tagRe.exec(str)) !== null) {
        if (match.index > i) {
            tokens.push({ type: 'text', text: str.substring(i, match.index) });
        }
        tokens.push(parseTagToken(match[0]));
        i = tagRe.lastIndex;
    }
    if (i < str.length) {
        tokens.push({ type: 'text', text: str.substring(i) });
    }
    return tokens;
}

function parseTagToken(tag) {

    var cm = tag.match(/^\[\/([a-z]+)\]$/);
    if (cm) return { type: 'close', name: cm[1] };
    var vm = tag.match(/^\[([a-z]+)=([^\[\]]*)\]$/);
    if (vm) return { type: 'open', name: vm[1], value: vm[2] };
    var sm = tag.match(/^\[([a-z]+)\]$/);
    if (sm) return { type: 'open', name: sm[1], value: null };
    return { type: 'text', text: tag };
}

function parseMotdTokens(str) {
    var tokens = tokenizeMotd(str);
    var root = [];
    var stack = [{ children: root }];

    for (var ti = 0; ti < tokens.length; ti++) {
        var tok = tokens[ti];
        var current = stack[stack.length - 1];

        if (tok.type === 'text') {
            current.children.push({ type: 'text', text: tok.text });
            continue;
        }

        if (tok.type === 'open') {
            var node = buildOpenNode(tok);
            if (node) {
                node.children = [];
                current.children.push(node);
                stack.push(node);
            } else {
    
                current.children.push({ type: 'text', text: '[' + tok.name + (tok.value != null ? '=' + tok.value : '') + ']' });
            }
            continue;
        }

        if (tok.type === 'close') {
            var found = -1;
            for (var si = stack.length - 1; si >= 1; si--) {
                if (stack[si]._tagName === tok.name) { found = si; break; }
            }
            if (found >= 1) {
                while (stack.length > found) stack.pop();
            } else {
                current.children.push({ type: 'text', text: '[/' + tok.name + ']' });
            }
            continue;
        }
    }

    return root;
}

function buildOpenNode(tok) {
    var simple = MOTD_TAGS[tok.name];
    if (simple && tok.value == null) {
        var node = { type: simple.type, _tagName: tok.name };
        if (simple.anim) node.anim = simple.anim;
        if (simple.align) node.align = simple.align;
        return node;
    }

    if (tok.name === 'c' && tok.value) {
        var color = tok.value.charAt(0) === '#' ? tok.value : (MOTD_COLOR_PRESETS[tok.value] || '#FFFFFF');
        if (!/^#[0-9A-Fa-f]{3,8}$/.test(color)) color = '#FFFFFF';
        return { type: 'color', color: color, _tagName: 'c' };
    }

    if (tok.name === 'g' && tok.value) {
        var parts = tok.value.split(',');
        if (parts.length >= 2 && parts.length <= 3) {
            var stops = parts.map(function(s) { return s.trim(); });
            if (stops.every(function(s) { return /^#[0-9A-Fa-f]{3,8}$/.test(s); })) {
                return { type: 'gradient', stops: stops, _tagName: 'g' };
            }
        }
    }

    if (tok.name === 'scroll' && tok.value) {
        var sparts = tok.value.split(',');
        if (sparts.length >= 2 && sparts.length <= 3) {
            var sstops = sparts.map(function(s) { return s.trim(); });
            if (sstops.every(function(s) { return /^#[0-9A-Fa-f]{3,8}$/.test(s); })) {
                return { type: 'scroll', stops: sstops, _tagName: 'scroll' };
            }
        }
    }

    return null;
}

function stripMotdTags(raw) {
    if (!raw) return '';
    return raw.replace(/\[\/?(?:b|i|u|c(?:=[^[\]]*)?|g(?:=[^[\]]*)?|pulse|flash|wave|scroll(?:=[^[\]]*)?|center|right)\]/gi, '');
}

function renderMotd(raw) {
    if (!raw || typeof raw !== 'string') return escapeHtml(raw || '');
    if (raw.indexOf('[') === -1 && raw.indexOf('\n') === -1) return escapeHtml(raw);
    var parsed = parseMotd(raw);
    var html = '';
    for (var i = 0; i < parsed.length; i++) {
        var line = parsed[i];
        var cls = 'motd-line';
        if (line.align === 'center') cls += ' motd-center';
        else if (line.align === 'right') cls += ' motd-right';
        html += '<span class="' + cls + '">' + renderMotdNodes(line.nodes) + '</span>';
    }
    return html;
}

function renderMotdNodes(nodes) {
    var html = '';
    for (var i = 0; i < nodes.length; i++) {
        html += renderMotdNode(nodes[i]);
    }
    return html;
}

function renderMotdNode(node) {
    if (node.type === 'text') return escapeHtml(node.text);
    var inner = renderMotdNodes(node.children || []);
    switch (node.type) {
        case 'bold':
            return '<span class="motd-bold">' + inner + '</span>';
        case 'italic':
            return '<span class="motd-italic">' + inner + '</span>';
        case 'underline':
            return '<span class="motd-underline">' + inner + '</span>';
        case 'color':
            return '<span style="color:' + sanitizeCSSColor(node.color) + '">' + inner + '</span>';
        case 'gradient':
            var grad = 'linear-gradient(90deg,' + node.stops.map(sanitizeCSSColor).join(',') + ')';
            return '<span class="motd-gradient" style="background-image:' + grad + '">' + inner + '</span>';
        case 'anim':
            if (node.anim === 'wave') return renderWaveNode(node);
            return '<span class="motd-' + node.anim + '">' + inner + '</span>';
        case 'scroll':
            var sgrad = 'linear-gradient(90deg,' + node.stops.map(sanitizeCSSColor).join(',') + ',' + sanitizeCSSColor(node.stops[0]) + ')';
            return '<span class="motd-scroll" style="background-image:' + sgrad + '">' + inner + '</span>';
        case 'align':
            var acls = node.align === 'center' ? 'motd-center' : 'motd-right';
            return '<span class="motd-align ' + acls + '">' + inner + '</span>';
        default:
            return inner;
    }
}

function renderWaveNode(node) {
    var chars = flattenToStyledChars(node.children || []);
    var html = '<span class="motd-wave">';
    for (var i = 0; i < chars.length; i++) {
        var delay = (i * 0.08).toFixed(2);
        var ch = chars[i].ch === ' ' ? '&nbsp;' : escapeHtml(chars[i].ch);
        var style = 'animation-delay:' + delay + 's';
        if (chars[i].color) style += ';color:' + sanitizeCSSColor(chars[i].color);
        var cls = 'motd-char';
        if (chars[i].bold) cls += ' motd-bold';
        if (chars[i].italic) cls += ' motd-italic';
        if (chars[i].underline) cls += ' motd-underline';
        html += '<span class="' + cls + '" style="' + style + '">' + ch + '</span>';
    }
    html += '</span>';
    return html;
}

function flattenToStyledChars(nodes, inherit) {
    var result = [];
    var style = inherit || {};
    for (var i = 0; i < nodes.length; i++) {
        var n = nodes[i];
        if (n.type === 'text') {
            for (var ci = 0; ci < n.text.length; ci++) {
                result.push({ ch: n.text[ci], bold: style.bold, italic: style.italic, underline: style.underline, color: style.color });
            }
        } else {
            var child = {};
            child.bold = n.type === 'bold' ? true : style.bold;
            child.italic = n.type === 'italic' ? true : style.italic;
            child.underline = n.type === 'underline' ? true : style.underline;
            child.color = n.type === 'color' ? n.color : style.color;
            result = result.concat(flattenToStyledChars(n.children || [], child));
        }
    }
    return result;
}

function getPlainText(node) {
    if (node.type === 'text') return node.text;
    var t = '';
    if (node.children) {
        for (var i = 0; i < node.children.length; i++) t += getPlainText(node.children[i]);
    }
    return t;
}

function sanitizeCSSColor(c) {
    if (!c) return '#FFFFFF';
    // only allow hex colors
    if (/^#[0-9A-Fa-f]{3,8}$/.test(c)) return c;
    return '#FFFFFF';
}

function motdPlainLength(raw) {
    if (!raw) return 0;
    // strip all tags
    return raw.replace(/\[\/?\w+(?:=[^\]]+)?\]/g, '').replace(/\n/g, '').length;
}

// MOTD editor

var motdEditorMode = 'markup'; // 'markup'

function initMotdEditor() {
    var wrap = document.getElementById('motdEditorWrap');
    if (!wrap) return;
    var textarea = document.getElementById('motdTextarea');
    if (!textarea) return;

    textarea.addEventListener('input', onMotdEditorInput);
    updateMotdPreview();
    updateMotdCharCount();
}

function onMotdEditorInput() {
    updateMotdPreview();
    updateMotdCharCount();
}

function updateMotdPreview() {
    var textarea = document.getElementById('motdTextarea');
    var preview = document.getElementById('motdPreview');
    if (!textarea || !preview) return;
    var raw = textarea.value;
    preview.innerHTML = '<div class="motd-rendered">' + renderMotd(raw) + '</div>';
}

function updateMotdCharCount() {
    var textarea = document.getElementById('motdTextarea');
    var counter = document.getElementById('motdCharCount');
    if (!textarea || !counter) return;
    var raw = textarea.value;
    var plain = motdPlainLength(raw);
    var lineCount = (raw.match(/\n/g) || []).length + 1;
    counter.textContent = plain + ' chars · ' + Math.min(lineCount, MOTD_MAX_LINES) + '/' + MOTD_MAX_LINES + ' lines';
    counter.classList.toggle('over', raw.length > MOTD_MAX_RAW);
}

function getMotdRaw() {
    var textarea = document.getElementById('motdTextarea');
    return textarea ? textarea.value : '';
}

function setMotdRaw(val) {
    var textarea = document.getElementById('motdTextarea');
    if (textarea) {
        textarea.value = val || '';
        updateMotdPreview();
        updateMotdCharCount();
    }
}

// toolbar actions

function motdWrapSelection(before, after) {
    var textarea = document.getElementById('motdTextarea');
    if (!textarea) return;
    var start = textarea.selectionStart;
    var end = textarea.selectionEnd;
    var text = textarea.value;
    var selected = text.substring(start, end) || 'text';
    textarea.value = text.substring(0, start) + before + selected + after + text.substring(end);
    textarea.selectionStart = start + before.length;
    textarea.selectionEnd = start + before.length + selected.length;
    textarea.focus();
    onMotdEditorInput();
}

function motdInsertBold() { motdWrapSelection('[b]', '[/b]'); }
function motdInsertItalic() { motdWrapSelection('[i]', '[/i]'); }
function motdInsertUnderline() { motdWrapSelection('[u]', '[/u]'); }

function motdInsertColor(color) {
    if (!color) return;
    motdWrapSelection('[c=' + color + ']', '[/c]');
    closeMotdColorPresets();
}

function motdInsertGradient() {
    var c1 = document.getElementById('motdGradColor1');
    var c2 = document.getElementById('motdGradColor2');
    var c3 = document.getElementById('motdGradColor3');
    if (!c1 || !c2) return;
    var stops = c1.value + ',' + c2.value;
    if (c3 && c3.value && c3.value !== '#000000') stops += ',' + c3.value;
    motdWrapSelection('[g=' + stops + ']', '[/g]');
    closeMotdGradientPicker();
}

function motdInsertAnim(type) {
    if (type === 'scroll') {
        motdWrapSelection('[scroll=#FF5555,#5555FF]', '[/scroll]');
    } else {
        motdWrapSelection('[' + type + ']', '[/' + type + ']');
    }
}

function motdInsertNewline() {
    var textarea = document.getElementById('motdTextarea');
    if (!textarea) return;
    var pos = textarea.selectionStart;
    textarea.value = textarea.value.substring(0, pos) + '\n' + textarea.value.substring(pos);
    textarea.selectionStart = textarea.selectionEnd = pos + 1;
    textarea.focus();
    onMotdEditorInput();
}

function motdInsertAlign(align) {
    motdWrapSelection('[' + align + ']', '[/' + align + ']');
}

// color presets panel
function toggleMotdColorPresets() {
    var el = document.getElementById('motdColorPresets');
    if (el) el.classList.toggle('open');
}

function closeMotdColorPresets() {
    var el = document.getElementById('motdColorPresets');
    if (el) el.classList.remove('open');
}

function onMotdColorPickerChange(input) {
    motdInsertColor(input.value);
    closeMotdColorPresets();
}

// gradient picker panel
function toggleMotdGradientPicker() {
    var el = document.getElementById('motdGradientPicker');
    if (el) el.classList.toggle('open');
    updateMotdGradientPreview();
}

function closeMotdGradientPicker() {
    var el = document.getElementById('motdGradientPicker');
    if (el) el.classList.remove('open');
}

function updateMotdGradientPreview() {
    var c1 = document.getElementById('motdGradColor1');
    var c2 = document.getElementById('motdGradColor2');
    var c3 = document.getElementById('motdGradColor3');
    var bar = document.getElementById('motdGradPreviewBar');
    if (!c1 || !c2 || !bar) return;
    var stops = c1.value + ', ' + c2.value;
    if (c3 && c3.value && c3.value !== '#000000') stops += ', ' + c3.value;
    bar.style.background = 'linear-gradient(90deg, ' + stops + ')';
}

// build color preset swatches
function buildMotdColorPresets() {
    var container = document.getElementById('motdColorPresets');
    if (!container || container.querySelector('.motd-color-preset[title="red"]')) return;
    var keys = Object.keys(MOTD_COLOR_PRESETS);
    for (var i = 0; i < keys.length; i++) {
        var swatch = document.createElement('div');
        swatch.className = 'motd-color-preset';
        swatch.style.background = MOTD_COLOR_PRESETS[keys[i]];
        swatch.title = keys[i];
        swatch.setAttribute('onclick', 'motdInsertColor("' + keys[i] + '")');
        container.appendChild(swatch);
    }
}
