#!/usr/bin/env python3
"""
Wordclock simulator: test the web UI and the led display without an ESP8266.

  python3 scripts/simulator.py [--port 8080]

Serves the pages from data/ (index.html, admin.html) and mocks the firmware's
/api endpoints with in-memory state. The extra page /sim shows the led matrix
the way the firmware would light it (word clock, digital clock, rainbow and the
IP address on startup), for any time and the layout saved in the admin page.

The display logic below is a Python port of the firmware (wordclock.ino and
src/ledmatrix.cpp). Keep it in sync when the firmware changes.
"""
import argparse
import json
import os
import colorsys
from datetime import datetime
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data')

# ----------------------------------------------------------------------------
# Firmware state (what the ESP keeps in EEPROM)
# ----------------------------------------------------------------------------
PICTURE_LAYOUT = (
    "HETVISUTWAALF"
    "VEERTIENZEVEN"
    "DERTIENDRIEEN"
    "ACHT....KWART"
    "ZES.....NEGEN"
    "ELF......TWEE"
    "VIER.....VIJF"
    "TIEN.....OVER"
    "VOOR.....HALF"
    "TWEEN...NEGEN"
    "TIENVIERZEVEN"
    "ACHTWAALFVIJF"
    "DRIELFZESPUUR"
)

WORD_CLOCK, DIGITAL_CLOCK, RAINBOW = 0, 1, 2

state = {
    'version': 11,
    'fsversion': 11,
    'mode': WORD_CLOCK,
    'brightness': 255,
    'nm_brightness': 0,
    'nm': False,
    'nm_start_h': 22, 'nm_start_m': 0, 'nm_end_h': 7, 'nm_end_m': 0,
    'clock_width': 13,
    'clock_height': 13,
    'clock_layout': PICTURE_LAYOUT,
    'timezone': 'CET-1CEST,M3.5.0,M10.5.0/3_2',
    'klokuniek': False,
    'color_time': (255, 255, 255),
    'color_name': (255, 0, 0),
    'color_icon': (0, 0, 255),
}

# ----------------------------------------------------------------------------
# Port of src/font.h
# ----------------------------------------------------------------------------
FONT_WIDTH, FONT_HEIGHT = 3, 5
FONT_NUMBERS = [
    [0b111, 0b101, 0b101, 0b101, 0b111],
    [0b001, 0b011, 0b001, 0b001, 0b001],
    [0b111, 0b001, 0b111, 0b100, 0b111],
    [0b111, 0b001, 0b111, 0b001, 0b111],
    [0b101, 0b101, 0b111, 0b001, 0b001],
    [0b111, 0b100, 0b111, 0b001, 0b111],
    [0b100, 0b100, 0b111, 0b101, 0b111],
    [0b111, 0b001, 0b001, 0b001, 0b001],
    [0b111, 0b101, 0b111, 0b101, 0b111],
    [0b111, 0b101, 0b111, 0b001, 0b001],
]
FONT_I = [0b010, 0b010, 0b010, 0b010, 0b010]
FONT_P = [0b111, 0b101, 0b111, 0b100, 0b100]
FONT_I_NARROW = [0b100, 0b100, 0b100, 0b100, 0b100]
FONT_IP_WIDTH = 5  # narrow I, 1 column space, P

OFF, TIME, NAME, ICON, OTHER = 'off', 'time', 'name', 'icon', 'other'


# ----------------------------------------------------------------------------
# Port of src/ledmatrix.cpp
# ----------------------------------------------------------------------------
class Matrix:
    def __init__(self):
        self.w = state['clock_width']
        self.h = state['clock_height']
        self.layout = state['clock_layout']
        self.leds = [[OFF] * self.w for _ in range(self.h)]

    def char(self, row, col):
        i = row * self.w + col
        return self.layout[i] if i < len(self.layout) else '?'

    def set(self, row, col, t):
        if 0 <= row < self.h and 0 <= col < self.w:
            self.leds[row][col] = t

    def has_led(self, row, col):
        if not (0 <= row < self.h and 0 <= col < self.w):
            return False
        i = row * self.w + col
        if i >= len(self.layout):
            return True
        return self.layout[i] not in ' .'

    def is_letter(self, row, col):
        if not self.has_led(row, col):
            return False
        i = row * self.w + col
        if i >= len(self.layout):
            return True
        return self.layout[i] not in '*&'

    def font_fits(self, xpos, ypos):
        return all(self.is_letter(ypos + y, xpos + x)
                   for y in range(FONT_HEIGHT) for x in range(FONT_WIDTH))

    def box_has_leds(self, xpos, ypos):
        return all(self.has_led(ypos + y, xpos + x)
                   for y in range(FONT_HEIGHT) for x in range(FONT_WIDTH))

    def centered_time_fits(self):
        return all(self.box_has_leds(x, y) for x, y in ((3, 0), (7, 0), (3, 6), (7, 6)))

    def centered_ip_fits(self):
        return all(self.box_has_leds(x, y) for x, y in ((1, 0), (5, 0), (0, 6), (4, 6), (8, 6)))

    def glyph_fits(self, glyph, xpos, ypos):
        """Only the lit pixels of the glyph need to be on letters."""
        return all(not (glyph[i] & (1 << j)) or self.is_letter(i + ypos, FONT_WIDTH - j - 1 + xpos)
                   for i in range(FONT_HEIGHT) for j in range(FONT_WIDTH))

    def ip_fits(self, xpos, ypos):
        return self.glyph_fits(FONT_I_NARROW, xpos, ypos) and self.glyph_fits(FONT_P, xpos + 2, ypos)

    def corner_slot(self, corner):
        """corner: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right"""
        return self.find_slot(corner, FONT_WIDTH, self.font_fits)

    def ip_slot(self):
        return self.find_slot(0, FONT_IP_WIDTH, self.ip_fits)

    def find_slot(self, corner, width, fits):
        if self.w < width or self.h < FONT_HEIGHT:
            return None
        right = corner in (1, 3)
        bottom = corner in (2, 3)
        max_x = self.w - width
        max_y = self.h - FONT_HEIGHT
        # only the corner's own quarter, so slots never overlap
        half_w, half_h = self.w // 2, self.h // 2
        max_dx = max(0, max_x - (self.w - half_w)) if right else max(0, half_w - width)
        max_dy = max(0, max_y - (self.h - half_h)) if bottom else max(0, half_h - FONT_HEIGHT)
        for d in range(max_dx + max_dy + 1):
            for dy in range(min(d, max_dy) + 1):
                dx = d - dy
                if dx > max_dx:
                    continue
                x = max_x - dx if right else dx
                y = max_y - dy if bottom else dy
                if fits(x, y):
                    return x, y
        return None

    def print(self, glyph, xpos, ypos, t):
        for i in range(FONT_HEIGHT):
            for j in range(FONT_WIDTH):
                if glyph[i] & (1 << j):
                    self.set(i + ypos, FONT_WIDTH - j - 1 + xpos, t)


# ----------------------------------------------------------------------------
# Port of the display functions in wordclock.ino
# ----------------------------------------------------------------------------
def time_to_string_dutch(hours, minutes):
    words = ["EEN", "TWEE", "DRIE", "VIER", "VIJF", "ZES", "ZEVEN", "ACHT", "NEGEN", "TIEN", "ELF", "TWAALF",
             "DERTIEN", "VEERTIEN", "KWART", "ZES TIEN", "ZEVEN TIEN", "ACHT TIEN", "NEGEN TIEN"]
    m = "HET IS "
    if 0 < minutes < 20:
        m += words[minutes - 1] + " OVER "
    elif 20 <= minutes < 30:
        m += words[29 - minutes] + " VOOR HALF "
    elif minutes == 30:
        m += "HALF "
    elif 30 < minutes < 45:
        m += words[minutes - 31] + " OVER HALF "
    elif 45 <= minutes <= 59:
        m += words[59 - minutes] + " VOOR "
    if hours >= 12:
        hours -= 12
    if minutes >= 20:
        hours += 1
    if hours == 0:
        hours = 12
    m += words[hours - 1] + " "
    if minutes == 0:
        m += "UUR "
    return m


def time_to_string_english(hours, minutes):
    words = ["ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN", "TWELVE",
             "THIR TEEN", "FOUR TEEN", "A QUARTER", "SIX TEEN", "SEVEN TEEN", "EIGHTEEN", "NINE TEEN", "TWENTY",
             "TWENTY ONE", "TWENTY TWO", "TWENTY THREE", "TWENTY FOUR", "TWENTY FIVE", "TWENTY SIX",
             "TWENTY SEVEN", "TWENTY EIGHT", "TWENTY NINE", "HALF"]
    m = "IT IS "
    if 0 < minutes <= 30:
        m += words[minutes - 1] + " PAST "
    elif 30 < minutes <= 59:
        m += words[59 - minutes] + " TO "
    ampm = "PM" if hours >= 12 else "AM"
    if hours >= 12:
        hours -= 12
    if minutes > 30:
        hours += 1
    if hours == 0:
        hours = 12
    m += words[hours - 1] + " "
    if minutes == 0:
        m += "OCLOCK "
    return m + ampm


def show_name(mx):
    for i, c in enumerate(mx.layout):
        if c == '*':
            mx.set(i // mx.w, i % mx.w, NAME)
        elif c == '&':
            mx.set(i // mx.w, i % mx.w, ICON)


def show_time_string(mx, time_string):
    last = 0
    missing = []
    for word in time_string.split():
        pos = mx.layout.find(word, last)
        if pos >= 0:
            for i in range(len(word)):
                mx.set((pos + i) // mx.w, (pos + i) % mx.w, TIME)
            last = pos + len(word)
        else:
            missing.append(word)
    show_name(mx)
    return missing


def show_digital_time(mx, hours, minutes):
    digits = [hours // 10, hours % 10, minutes // 10, minutes % 10]
    if mx.centered_time_fits():
        mx.print(FONT_NUMBERS[digits[0]], 3, 0, TIME)
        mx.print(FONT_NUMBERS[digits[1]], 7, 0, TIME)
        mx.print(FONT_NUMBERS[digits[2]], 3, 6, TIME)
        mx.print(FONT_NUMBERS[digits[3]], 7, 6, TIME)
    else:
        for i in range(4):
            slot = mx.corner_slot(i)
            if slot:
                mx.print(FONT_NUMBERS[digits[i]], slot[0], slot[1], TIME)
    show_name(mx)


def print_ip(mx, address):
    digits = []
    if address >= 100:
        digits.append(address // 100)
    if address >= 10:
        digits.append((address // 10) % 10)
    digits.append(address % 10)
    if mx.centered_ip_fits():
        mx.print(FONT_I, 1, 0, OTHER)
        mx.print(FONT_P, 5, 0, OTHER)
        for i, d in enumerate(digits):
            mx.print(FONT_NUMBERS[d], i * 4, 6, OTHER)
    else:
        corner = 0
        slot = mx.ip_slot()
        if slot:
            mx.print(FONT_I_NARROW, slot[0], slot[1], OTHER)
            mx.print(FONT_P, slot[0] + 2, slot[1], OTHER)
            corner = 1
        for d in digits:
            slot = mx.corner_slot(corner)
            if slot:
                mx.print(FONT_NUMBERS[d], slot[0], slot[1], OTHER)
            corner += 1


_check_cache = {}


def check_layout():
    """Check what the saved layout cannot show: words for any time of the day,
    the digital time, and the IP address. Returns a list of problems."""
    key = (state['clock_width'], state['clock_height'], state['clock_layout'])
    if key in _check_cache:
        return _check_cache[key]
    mx = Matrix()
    problems = []
    english = 'OCLOCK' in mx.layout

    # word clock: every minute of the day
    missing = {}
    for h in range(24):
        for m in range(60):
            ts = time_to_string_english(h, m) if english else time_to_string_dutch(h, m)
            for w in show_time_string(Matrix(), ts):
                missing.setdefault(w, []).append('%02d:%02d' % (h, m))
    for w, times in sorted(missing.items(), key=lambda kv: -len(kv[1])):
        problems.append({'what': 'word clock', 'text': 'word "%s" cannot be shown (%d times, e.g. %s)'
                         % (w, len(times), ', '.join(times[:4]))})

    corners = ['top-left', 'top-right', 'bottom-left', 'bottom-right']
    if not mx.centered_time_fits():
        # digital clock: one digit per corner
        for i in range(4):
            if not mx.corner_slot(i):
                problems.append({'what': 'digital clock', 'text': 'no 3x5 block of letters near the %s corner for digit %d' % (corners[i], i + 1)})
        # IP: "IP" top-left and up to 3 digits in the other corners
        first = 1
        if not mx.ip_slot():
            first = 0
            problems.append({'what': 'IP address', 'text': '"IP" does not fit in the top-left corner, only the digits are shown', 'level': 'info'})
        for i in range(3):
            if not mx.corner_slot(first + i):
                problems.append({'what': 'IP address', 'text': 'no 3x5 block of letters near the %s corner for IP digit %d' % (corners[first + i], i + 1)})
    elif not mx.centered_ip_fits():
        problems.append({'what': 'IP address', 'text': 'the digital time is centered but "IP" + 3 digits do not fit centered: the IP address is shown in the corners', 'level': 'info'})
    _check_cache[key] = problems
    return problems


def print_check(problems):
    if not problems:
        print('  -> layout check: all times, the digital time and the IP address can be shown')
    for p in problems:
        print('  -> layout check [%s] %s' % (p['what'], p['text']))


def is_night(hours, minutes):
    """Port of setNightMode() in wordclock.ino."""
    if not state['nm']:
        return False
    t = hours * 60 + minutes
    start = state['nm_start_h'] * 60 + state['nm_start_m']
    end = state['nm_end_h'] * 60 + state['nm_end_m']
    return (t >= start or t < end) if start > end else (t >= start and t < end)


def render(hours, minutes, ip=None, hue=0.0):
    """Returns what the firmware would show: per led a letter and a color, plus the brightness."""
    mx = Matrix()
    info = {}
    mode = state['mode']
    night = is_night(hours, minutes)
    info['night'] = night
    info['brightness'] = state['nm_brightness'] if night else state['brightness']
    if ip is not None:
        print_ip(mx, ip)
        info['text'] = 'IP address ...%d' % ip
    elif mode == DIGITAL_CLOCK:
        show_digital_time(mx, hours, minutes)
        info['text'] = '%02d:%02d' % (hours, minutes)
    else:
        english = 'OCLOCK' in mx.layout
        ts = time_to_string_english(hours, minutes) if english else time_to_string_dutch(hours, minutes)
        missing = show_time_string(mx, ts)
        info['text'] = ts
        if missing:
            info['warning'] = 'word is not possible to show on clock: ' + ', '.join(missing)
    info['corner_mode'] = not mx.centered_time_fits()

    colors = {TIME: state['color_time'], NAME: state['color_name'], ICON: state['color_icon'], OTHER: (0, 255, 0)}
    rows = []
    n = mx.w * mx.h
    for r in range(mx.h):
        row = []
        for c in range(mx.w):
            t = mx.leds[r][c]
            has_led = mx.has_led(r, c)
            color = None
            if has_led:
                if t in (TIME, NAME, ICON) and mode == RAINBOW and ip is None:
                    color = (255, 255, 255)
                elif t != OFF:
                    color = colors[t]
                elif mode == RAINBOW and ip is None:
                    i = r * mx.w + (mx.w - 1 - c if r % 2 else c)
                    color = tuple(int(v * 255) for v in colorsys.hsv_to_rgb((hue + i / n) % 1.0, 1, 1))
            ch = mx.char(r, c)
            # '*' (name/picture led) and '&' (icon led) are shown as symbols
            row.append({'letter': ('★' if ch == '*' else '●' if ch == '&' else ch) if has_led else '', 'led': has_led, 'color': color,
                        'special': ch in '*&'})
        rows.append(row)
    return {'rows': rows, 'width': mx.w, 'height': mx.h, 'info': info, 'brightness': info['brightness']}


# ----------------------------------------------------------------------------
# HTTP server: static files from data/ + mocked /api
# ----------------------------------------------------------------------------
SIM_PAGE = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>Wordclock simulator</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { font-family: sans-serif; background: #222; color: #ddd; margin: 0; padding: 16px; }
  .controls { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; margin-bottom: 12px; }
  .controls label { display: flex; gap: 6px; align-items: center; }
  .clock { display: inline-grid; gap: 4px; background: #111; padding: 16px; border: 10px solid #999; border-radius: 6px; }
  .cell { width: 28px; height: 28px; display: flex; align-items: center; justify-content: center;
          font-family: monospace; font-size: 18px; font-weight: bold; color: #333; border-radius: 3px; }
  .cell.noled { background: #1a1a1a; }
  .cell.lit { text-shadow: 0 0 8px currentColor; }
  .cell.special { font-size: 14px; }
  .legend { color: #888; font-size: 12px; margin-top: 8px; }
  .info { margin: 12px 0; font-family: monospace; }
  .warn { color: #f80; }
  .ok { color: #6d6; }
  .note { color: #8cf; }
  a { color: #8cf; }
</style></head>
<body>
<h2>Wordclock simulator</h2>
<div class="controls">
  <label>Time <input type="time" id="time"></label>
  <label><input type="checkbox" id="live" checked> follow real time</label>
  <label>Mode
    <select id="mode"><option value="word_clock">word clock</option><option value="digital_clock">digital clock</option><option value="rainbow">rainbow</option></select>
  </label>
  <label>IP ends in <input type="number" id="ip" min="0" max="255" value="105" style="width:5em"></label>
  <button id="showip">show IP (10 s, like after WiFi connect)</button>
</div>
<div class="info" id="info"></div>
<div class="clock" id="clock"></div>
<div class="legend">★ = name/picture led (<code>*</code> in the layout), ● = icon led (<code>&amp;</code>), dark cell = no led (space or <code>.</code>)</div>
<div class="info" id="check"></div>
<p>Change the layout on the <a href="/admin.html">admin page</a>, colors and mode on the <a href="/">control page</a>; this page follows the saved state.</p>
<script>
  const clockEl = document.getElementById('clock'), infoEl = document.getElementById('info');
  const timeEl = document.getElementById('time'), liveEl = document.getElementById('live');
  const modeEl = document.getElementById('mode'), ipEl = document.getElementById('ip');
  let showIpUntil = 0, hue = 0;
  document.getElementById('showip').onclick = () => { showIpUntil = Date.now() + 10000; refresh(); };
  modeEl.onchange = async () => { await fetch('/api/mode', { method: 'POST', body: modeEl.value }); refresh(); };
  timeEl.oninput = () => { liveEl.checked = false; refresh(); };
  async function refresh() {
    if (liveEl.checked) { const d = new Date(); timeEl.value = d.toTimeString().slice(0, 5); }
    const [h, m] = timeEl.value.split(':').map(Number);
    let url = `/api/sim/render?h=${h}&m=${m}&hue=${hue}`;
    if (Date.now() < showIpUntil) url += `&ip=${ipEl.value}`;
    const r = await (await fetch(url)).json();
    const modes = ['word_clock', 'digital_clock', 'rainbow'];
    if (document.activeElement !== modeEl) modeEl.value = modes[r.mode];
    clockEl.style.gridTemplateColumns = `repeat(${r.width}, 28px)`;
    clockEl.innerHTML = '';
    for (const row of r.rows) for (const c of row) {
      const d = document.createElement('div');
      d.className = 'cell' + (c.led ? '' : ' noled') + (c.color ? ' lit' : '') + (c.special ? ' special' : '');
      if (c.special) d.title = c.letter === '★' ? 'name/picture led (*)' : 'icon led (&)';
      d.textContent = c.letter;
      if (c.color) { d.style.color = `rgb(${c.color.join(',')})`; d.style.opacity = Math.max(0.06, r.brightness / 255); }
      clockEl.appendChild(d);
    }
    infoEl.innerHTML = r.info.text + (r.info.corner_mode ? ' &nbsp;(layout has gaps: digits in the corners)' : '')
      + ` &nbsp;brightness ${Math.ceil(r.brightness / 255 * 100)}%` + (r.info.night ? ' (night mode active)' : '')
      + (r.info.warning ? `<div class="warn">${r.info.warning}</div>` : '');
    const checkEl = document.getElementById('check');
    if (!r.check.length) checkEl.innerHTML = '<div class="ok">Layout check: all times of the day, the digital time and the IP address can be shown.</div>';
    else checkEl.innerHTML = '<b>Layout check:</b>' + r.check.map(p =>
      `<div class="${p.level === 'info' ? 'note' : 'warn'}">[${p.what}] ${p.text}</div>`).join('');
  }
  refresh();
  setInterval(() => { if (modeEl.value === 'rainbow') hue = (hue + 0.02) % 1; refresh(); }, 500);
</script>
</body></html>
"""


def rgb_to_uint32(c):
    return (c[0] << 16) | (c[1] << 8) | c[2]


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DATA_DIR, **kwargs)

    def log_message(self, fmt, *args):
        if '/api/sim/' not in str(args[0] if args else ''):
            super().log_message(fmt, *args)

    # the clock streams files without caching headers, so browsers always fetch them fresh;
    # SimpleHTTPRequestHandler adds Last-Modified, which lets browsers keep an old app.js/app.css
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def send_json(self, body, status=200):
        data = json.dumps(body).encode() if not isinstance(body, str) else body.encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        n = int(self.headers.get('Content-Length', 0))
        return self.rfile.read(n).decode() if n else ''

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Headers', '*')
        self.end_headers()

    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        if url.path == '/sim':
            data = SIM_PAGE.encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif url.path == '/api/sim/render':
            now = datetime.now()
            h = int(q.get('h', now.hour))
            m = int(q.get('m', now.minute))
            ip = int(q['ip']) if 'ip' in q else None
            r = render(h, m, ip, float(q.get('hue', 0)))
            r['mode'] = state['mode']
            r['check'] = check_layout()
            self.send_json(r)
        elif url.path == '/api/settings':
            s = {k: v for k, v in state.items() if not k.startswith('color_')}
            s['nm'] = int(s['nm'])
            s['klokuniek'] = int(s['klokuniek'])
            self.send_json(s)
        elif url.path == '/api/color':
            key = 'color_' + q.get('type', '')
            if key not in state:
                return self.send_json('Color type unknown', 400)
            self.send_json(rgb_to_uint32(state[key]))
        elif url.path == '/api/brightness':
            t = q.get('type')
            if t == 'global':
                self.send_json(state['brightness'])
            elif t == 'nightmode':
                self.send_json(state['nm_brightness'])
            else:
                self.send_json('Type unknown', 400)
        elif url.path == '/api/admin/reset-wifi':
            print('  -> firmware would reset WiFi settings and restart')
            self.send_json('OK')
        elif url.path == '/api/admin/reset-settings':
            print('  -> firmware would erase the EEPROM and restart')
            state.update(mode=WORD_CLOCK, clock_width=13, clock_height=13, clock_layout='', klokuniek=False)
            self.send_json('OK')
        elif url.path.startswith('/api/'):
            self.send_json('Unknown endpoint', 404)
        else:
            super().do_GET()

    def do_POST(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        raw = self.body()
        print('  POST %s  body=%s' % (self.path, raw[:200]))
        if url.path == '/api/mode':
            modes = {'word_clock': WORD_CLOCK, 'digital_clock': DIGITAL_CLOCK, 'rainbow': RAINBOW}
            if raw in modes:
                state['mode'] = modes[raw]
            self.send_json(state['mode'])
        elif url.path == '/api/color':
            key = 'color_' + q.get('type', '')
            if key not in state:
                return self.send_json('Color type unknown', 400)
            c = json.loads(raw)
            state[key] = (int(c['r']), int(c['g']), int(c['b']))
            self.send_json(raw)
        elif url.path == '/api/brightness':
            t = q.get('type')
            if t == 'global':
                state['brightness'] = int(raw)
            elif t == 'nightmode':
                state['nm_brightness'] = int(raw)
            else:
                return self.send_json('Type unknown', 400)
            self.send_json(raw)
        elif url.path == '/api/settings':
            d = json.loads(raw)
            for k in ('timezone', 'nm', 'nm_start_h', 'nm_start_m', 'nm_end_h', 'nm_end_m'):
                if k in d:
                    state[k] = d[k]
            self.send_json(raw)
        elif url.path == '/api/admin/settings':
            d = json.loads(raw)
            w, h = d.get('clock_width', 0), d.get('clock_height', 0)
            if 0 < w <= 20 and 0 < h <= 20:
                state['clock_width'], state['clock_height'] = w, h
                if d.get('clock_layout'):
                    state['clock_layout'] = d['clock_layout']
                    print('  -> layout (%dx%d):' % (w, h))
                    for r in range(h):
                        print('     |' + state['clock_layout'][r * w:(r + 1) * w] + '|')
                    print_check(check_layout())
            if 'klokuniek' in d:
                state['klokuniek'] = bool(d['klokuniek'])
            self.send_json(raw)
        else:
            self.send_json('Unknown endpoint', 404)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=8080)
    ap.add_argument('--full-grid', action='store_true', help='start with a full 13x13 grid instead of the picture layout')
    args = ap.parse_args()
    if args.full_grid:
        state['clock_layout'] = PICTURE_LAYOUT.replace('.', 'X')
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    print_check(check_layout())
    print('Wordclock simulator running:')
    print('  control page : http://127.0.0.1:%d/' % args.port)
    print('  admin page   : http://127.0.0.1:%d/admin.html' % args.port)
    print('  led preview  : http://127.0.0.1:%d/sim' % args.port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
