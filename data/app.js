// JouwWoordklok web UI: shared code for index.html (control) and admin.html.
//
// When settings are sent to the clock (the firmware writes them to EEPROM, which
// has a limited number of write cycles):
//  - mode: immediately when tapped
//  - color: immediately while picking (throttled); the firmware delays the EEPROM write
//  - brightness (day and night): when the slider is released, not while sliding
//  - night mode on/off, its times and the time zone: only with the "Save settings" button
//  - admin page (layout, branding): only with its "Save" button
const baseUrl = '';
const { createApp, ref, computed, onMounted, nextTick, watch } = Vue;
const pad = n => String(n).padStart(2, '0');
function throttle(f, delay = 50) {
  let lastCall = Number.NEGATIVE_INFINITY, handle;
  return (...args) => {
    clearTimeout(handle);
    handle = setTimeout(() => { f(...args); lastCall = Date.now(); }, Math.max(0, lastCall + delay - Date.now()));
  };
}
// language of the UI follows the clock: English when the layout contains OCLOCK
const isEnglishGlobal = ref(false);
const T = (en, nl) => (isEnglishGlobal.value ? en : nl);

// ---------------------------------------------------------------------------
// control page
// ---------------------------------------------------------------------------
function useControl() {
  const loading = ref(true), loadingSettings = ref(false), loadingBrightness = ref(false), saved = ref(false), showSettings = ref(false);
  const mode = ref(0), brightness = ref(null), nightmodeBrightness = ref(null);
  const nm = ref(false), nm_start = ref('22:00'), nm_end = ref('07:00'), timezone = ref('');
  const version = ref(''), fsversion = ref(''), klokuniek = ref(false);
  const clockWidth = ref(13), clockHeight = ref(13), clockLayout = ref('');
  const colors = ref({ time: '#ffffff', name: '#ff0000', icon: '#0000ff' }), colorType = ref('time');
  const isEnglish = computed(() => clockLayout.value.includes('OCLOCK'));
  watch(isEnglish, v => isEnglishGlobal.value = v);
  // night mode as the clock applies it: from the settings as saved on the clock (not the unsaved drawer values)
  const now = ref(new Date());
  setInterval(() => now.value = new Date(), 1000);
  const savedNight = ref({ nm: false, start: '22:00', end: '07:00' });
  const isNight = computed(() => {
    const [sh, sm] = savedNight.value.start.split(':').map(Number), [eh, em] = savedNight.value.end.split(':').map(Number);
    const t = now.value.getHours() * 60 + now.value.getMinutes(), start = sh * 60 + sm, end = eh * 60 + em;
    if (!savedNight.value.nm) return false;
    return start > end ? (t >= start || t < end) : (t >= start && t < end);
  });
  const hasName = computed(() => clockLayout.value.includes('*'));
  const hasIcon = computed(() => clockLayout.value.includes('&'));
  const pct = v => v === null ? '…' : Math.ceil(v / 255 * 100) + '%';

  const getSettings = async () => {
    try {
      loadingSettings.value = true;
      const result = await (await fetch(baseUrl + '/api/settings')).json();
      mode.value = result.mode;
      clockWidth.value = result.clock_width;
      clockHeight.value = result.clock_height;
      clockLayout.value = result.clock_layout;
      version.value = result.version;
      fsversion.value = result.fsversion;
      brightness.value = result.brightness;
      nightmodeBrightness.value = result.nm_brightness;
      nm.value = !!result.nm;
      nm_start.value = pad(result.nm_start_h) + ':' + pad(result.nm_start_m);
      nm_end.value = pad(result.nm_end_h) + ':' + pad(result.nm_end_m);
      timezone.value = result.timezone;
      savedNight.value = { nm: nm.value, start: nm_start.value, end: nm_end.value };
      klokuniek.value = !!result.klokuniek;
      if (klokuniek.value) document.title = 'KlokUniek';
    } catch (e) {
      alert('Could not retrieve settings');
      console.error(e);
    }
    loadingSettings.value = false;
  };
  const setMode = async (newMode) => {
    try {
      loadingSettings.value = true;
      mode.value = await (await fetch(baseUrl + '/api/mode', { method: 'POST', body: newMode })).json();
    } catch (e) {
      alert('Could not change mode');
      console.error(e);
    }
    loadingSettings.value = false;
  };
  const saveSettings = async () => {
    try {
      loadingSettings.value = true;
      const [sh, sm] = nm_start.value.split(':').map(Number);
      const [eh, em] = nm_end.value.split(':').map(Number);
      await fetch(baseUrl + '/api/settings', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ nm: nm.value, nm_start_h: sh, nm_start_m: sm, nm_end_h: eh, nm_end_m: em, timezone: timezone.value })
      });
      savedNight.value = { nm: nm.value, start: nm_start.value, end: nm_end.value };
      saved.value = true;
      setTimeout(() => saved.value = false, 2500);
    } catch (e) {
      alert('Could not save settings');
      console.error(e);
    }
    loadingSettings.value = false;
  };
  const saveBrightness = async (nightmode = false) => {
    try {
      loadingBrightness.value = true;
      await fetch(baseUrl + '/api/brightness?type=' + (nightmode ? 'nightmode' : 'global'), {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(parseInt(nightmode ? nightmodeBrightness.value : brightness.value))
      });
    } catch (e) {
      alert('Could not save brightness');
      console.error(e);
    }
    loadingBrightness.value = false;
  };
  const getColors = async () => {
    try {
      for (const type of ['time', 'name', 'icon']) {
        const color = await (await fetch(baseUrl + '/api/color?type=' + type)).json();
        colors.value[type] = '#' + color.toString(16).padStart(6, '0');
      }
    } catch (e) {
      alert('Could not retrieve colors');
      console.error(e);
    }
  };
  const sendColor = throttle(async (type, rgb) => {
    try {
      await fetch(baseUrl + '/api/color?type=' + type, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(rgb) });
    } catch (e) {
      alert('Could not save color');
      console.error(e);
    }
  });
  let tabChange = false;
  const mountPicker = (el, width = 260) => {
    const picker = new window.iro.ColorPicker(el, {
      width, color: colors.value[colorType.value],
      layout: [{ component: iro.ui.Wheel }, { component: iro.ui.Slider, options: { sliderType: 'value' } }]
    });
    picker.on('color:change', c => {
      if (tabChange) return;
      colors.value[colorType.value] = c.hexString;
      sendColor(colorType.value, c.rgb);
    });
    // switching the chip only changes the picker, it does not send a color
    watch(colorType, type => { tabChange = true; picker.color.hexString = colors.value[type]; setTimeout(() => tabChange = false, 100); });
  };
  onMounted(async () => { await getSettings(); await getColors(); loading.value = false; });
  return { loading, loadingSettings, loadingBrightness, saved, showSettings, mode, brightness, nightmodeBrightness, nm, nm_start, nm_end, timezone,
    version, fsversion, klokuniek, clockWidth, clockHeight, clockLayout, colors, colorType, isEnglish, hasName, hasIcon, pct,
    isNight, savedNight, setMode, saveSettings, saveBrightness, mountPicker, T };
}

// searchable time zone picker, grouped by continent (data: timezoneOptions in index.html)
const TimezonePicker = {
  props: ['modelValue'],
  emits: ['update:modelValue'],
  data() { return { open: false, q: '' }; },
  computed: {
    display() { const o = timezoneOptions.find(o => o.value === this.modelValue); return o ? o.name : (this.modelValue || '—'); },
    groups() {
      const q = this.q.toLowerCase(), g = {};
      for (const o of timezoneOptions) if (!q || o.name.toLowerCase().includes(q)) (g[o.continent] = g[o.continent] || []).push(o);
      return Object.entries(g).map(([continent, timezones]) => ({ continent, timezones }));
    }
  },
  methods: { T },
  template: `<div class="tzp">
    <button type="button" class="tzp-btn" @click="open = !open"><span>{{ display }}</span><span>▾</span></button>
    <div v-if="open" class="tzp-menu">
      <input class="tzp-search" v-model="q" :placeholder="T('Search timezone…', 'Tijdzone zoeken…')">
      <div class="tzp-list"><template v-for="g in groups"><div class="tzp-group">{{ g.continent }}</div>
        <div v-for="t in g.timezones" class="tzp-item" :class="{sel: t.value === modelValue}" @click="$emit('update:modelValue', t.value); open = false">{{ t.name }}</div></template></div>
    </div></div>`,
};

// ---------------------------------------------------------------------------
// admin page
// ---------------------------------------------------------------------------
function useAdmin() {
  const version = ref(''), fsversion = ref(''), loading = ref(false), saved = ref(false), klokuniek = ref(false);
  const clockWidth = ref(13), clockHeight = ref(13);
  const cells = ref([]);  // one character per led; ' ' = no letter (never lit)
  const load = async () => {
    loading.value = true;
    try {
      const result = await (await fetch(baseUrl + '/api/settings')).json();
      version.value = result.version;
      fsversion.value = result.fsversion;
      klokuniek.value = !!result.klokuniek;
      if (klokuniek.value) document.title = 'KlokUniek';
      // same fallback as before: an unset (0) or 1 size means the default 13x13
      clockWidth.value = result.clock_width > 1 ? result.clock_width : 13;
      clockHeight.value = result.clock_height > 1 ? result.clock_height : 13;
      cells.value = Array.from({ length: clockWidth.value * clockHeight.value }, (_, i) => (result.clock_layout[i] || ' ').replace('.', ' '));
    } catch (e) {
      alert('Could not retrieve settings');
      console.error(e);
    }
    loading.value = false;
  };
  // the size fields change on every keystroke, so only ever grow the cell array here; the layout that is
  // shown and saved is always exactly width*height characters (see layoutString)
  watch([clockWidth, clockHeight], () => {
    const n = (clockWidth.value || 0) * (clockHeight.value || 0);
    if (n > cells.value.length) cells.value = Array.from({ length: n }, (_, i) => cells.value[i] || ' ');
  });
  const layoutString = computed(() => Array.from({ length: (clockWidth.value || 0) * (clockHeight.value || 0) }, (_, i) => (cells.value[i] || ' ').charAt(0)).join(''));
  const asText = computed(() => Array.from({ length: clockHeight.value }, (_, r) =>
    layoutString.value.slice(r * clockWidth.value, (r + 1) * clockWidth.value).replace(/ /g, '.')).join('\n'));
  const gaps = computed(() => cells.value.filter(c => !c || c === ' ').length);
  // preview: what the clock shows in words / digital / for the IP address, for the layout as it is being edited
  const preview = ref('edit'), live = ref(true), previewTime = ref('12:00');
  const ipGuess = /^\d+\.\d+\.\d+\.(\d+)$/.exec(location.hostname);
  const previewIp = ref(ipGuess ? parseInt(ipGuess[1]) : 100);
  const tick = () => { if (live.value) { const d = new Date(); previewTime.value = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`; } };
  tick(); setInterval(tick, 1000);
  const sim = computed(() => {
    const mx = new SimMatrix(layoutString.value, clockWidth.value || 0, clockHeight.value || 0);
    const [h, m] = (previewTime.value || '0:0').split(':').map(Number);
    let missing = [];
    if (preview.value === 'words') missing = mx.showTimeString(timeToString(layoutString.value, h, m));
    else if (preview.value === 'digital') mx.showDigitalTime(h, m);
    else if (preview.value === 'ip') mx.printIP(Math.min(255, Math.max(0, previewIp.value || 0)));
    return { leds: mx.leds, missing, text: preview.value === 'words' ? timeToString(layoutString.value, h, m).trim() : '' };
  });
  const previewLeds = computed(() => preview.value === 'edit' ? null : sim.value.leds);
  const previewMissing = computed(() => sim.value.missing);
  const previewText = computed(() => sim.value.text);
  const problems = computed(() => checkLayout(layoutString.value, clockWidth.value || 0, clockHeight.value || 0));
  const setCell = (i, v) => { cells.value[i] = (v || ' ').toUpperCase().charAt(0); };
  // layout as text: one row per line, '.' or space = no letter; size follows the text.
  // layoutText is what the textarea shows; it is applied with fromText when the field is left.
  const layoutText = ref('');
  watch(asText, v => layoutText.value = v);
  const fromText = (text) => {
    const lines = text.replace(/\r/g, '').split('\n');
    while (lines.length && !lines[lines.length - 1].trim().length) lines.pop();
    if (!lines.length) { layoutText.value = asText.value; return; }
    clockHeight.value = lines.length;
    clockWidth.value = Math.max(...lines.map(l => l.length));
    const out = [];
    for (const l of lines) for (let c = 0; c < clockWidth.value; c++) out.push((l[c] || ' ').replace('.', ' ').toUpperCase());
    cells.value = out;
    layoutText.value = asText.value;
  };
  const save = async () => {
    loading.value = true;
    try {
      await fetch(baseUrl + '/api/admin/settings', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ clock_width: parseInt(clockWidth.value), clock_height: parseInt(clockHeight.value), clock_layout: layoutString.value, klokuniek: klokuniek.value })
      });
      saved.value = true;
      setTimeout(() => saved.value = false, 2500);
    } catch (e) {
      alert('Could not save settings');
      console.error(e);
    }
    loading.value = false;
  };
  // both resets make the clock restart (it answers first, then restarts after a second)
  const resetWifi = async () => {
    if (confirm('Are you sure you want to reset the WiFi settings?')) {
      try { await (await fetch(baseUrl + '/api/admin/reset-wifi')).json(); alert('Restarting wordclock..'); }
      catch (e) { alert('Could not reset wifi'); console.error(e); }
    }
  };
  const resetSettings = async () => {
    if (confirm('Are you sure you want to reset all settings (including clock layout)?')) {
      try { await (await fetch(baseUrl + '/api/admin/reset-settings')).json(); alert('Restarting wordclock..'); }
      catch (e) { alert('Could not reset settings'); console.error(e); }
    }
  };
  onMounted(load);
  return { version, fsversion, loading, saved, klokuniek, clockWidth, clockHeight, cells, layoutString, asText, layoutText, gaps, setCell, fromText, save, resetWifi, resetSettings,
    preview, live, previewTime, previewIp, previewLeds, previewMissing, previewText, problems };
}

// ---------------------------------------------------------------------------
// clock simulation (admin page): what the firmware lights for a time or for the IP address
// (port of src/font.h, src/ledmatrix.cpp and the display functions in wordclock.ino)
// ---------------------------------------------------------------------------
const FONT_W = 3, FONT_H = 5;
const FONT_NUMBERS = [[7, 5, 5, 5, 7], [1, 3, 1, 1, 1], [7, 1, 7, 4, 7], [7, 1, 7, 1, 7], [5, 5, 7, 1, 1], [7, 4, 7, 1, 7], [4, 4, 7, 5, 7], [7, 1, 1, 1, 1], [7, 5, 7, 5, 7], [7, 5, 7, 1, 1]];
const FONT_I = [2, 2, 2, 2, 2], FONT_P = [7, 5, 7, 4, 4], FONT_I_NARROW = [4, 4, 4, 4, 4], FONT_IP_WIDTH = 5;
function timeToStringDutch(hours, minutes) {
  const w = ["EEN", "TWEE", "DRIE", "VIER", "VIJF", "ZES", "ZEVEN", "ACHT", "NEGEN", "TIEN", "ELF", "TWAALF", "DERTIEN", "VEERTIEN", "KWART", "ZES TIEN", "ZEVEN TIEN", "ACHT TIEN", "NEGEN TIEN"];
  let message = "HET IS ";
  if (minutes > 0 && minutes < 20) message += w[minutes - 1] + " OVER ";
  else if (minutes >= 20 && minutes < 30) message += w[29 - minutes] + " VOOR HALF ";
  else if (minutes == 30) message += "HALF ";
  else if (minutes > 30 && minutes < 45) message += w[minutes - 31] + " OVER HALF ";
  else if (minutes >= 45) message += w[59 - minutes] + " VOOR ";
  if (hours >= 12) hours -= 12;
  if (minutes >= 20) hours++;
  if (hours == 0) hours = 12;
  message += w[hours - 1] + " ";
  if (minutes == 0) message += "UUR ";
  return message;
}
function timeToStringEnglish(hours, minutes) {
  const w = ["ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN", "TWELVE", "THIR TEEN", "FOUR TEEN", "A QUARTER", "SIX TEEN", "SEVEN TEEN", "EIGHTEEN", "NINE TEEN", "TWENTY", "TWENTY ONE", "TWENTY TWO", "TWENTY THREE", "TWENTY FOUR", "TWENTY FIVE", "TWENTY SIX", "TWENTY SEVEN", "TWENTY EIGHT", "TWENTY NINE", "HALF"];
  let message = "IT IS ";
  if (minutes > 0 && minutes <= 30) message += w[minutes - 1] + " PAST ";
  else if (minutes > 30) message += w[59 - minutes] + " TO ";
  const ampm = hours >= 12 ? "PM" : "AM";
  if (hours >= 12) hours -= 12;
  if (minutes > 30) hours++;
  if (hours == 0) hours = 12;
  message += w[hours - 1] + " ";
  if (minutes == 0) message += "OCLOCK ";
  return message + ampm;
}
const timeToString = (layout, h, m) => layout.includes('OCLOCK') ? timeToStringEnglish(h, m) : timeToStringDutch(h, m);
class SimMatrix {
  constructor(layout, w, h) { this.layout = layout; this.w = w; this.h = h; this.leds = new Array(w * h).fill(''); }
  set(row, col, t) { if (row >= 0 && row < this.h && col >= 0 && col < this.w) this.leds[row * this.w + col] = t; }
  // a cell has a led unless the layout says ' ' or '.'; positions beyond the layout string count as leds (like the firmware)
  hasLed(row, col) { if (row < 0 || row >= this.h || col < 0 || col >= this.w) return false; const i = row * this.w + col; return i >= this.layout.length || !' .'.includes(this.layout[i]); }
  isLetter(row, col) { if (!this.hasLed(row, col)) return false; const i = row * this.w + col; return i >= this.layout.length || !'*&'.includes(this.layout[i]); }
  box(x, y, test) { for (let i = 0; i < FONT_H; i++) for (let j = 0; j < FONT_W; j++) if (!test(y + i, x + j)) return false; return true; }
  fontFits(x, y) { return this.box(x, y, (r, c) => this.isLetter(r, c)); }
  boxHasLeds(x, y) { return this.box(x, y, (r, c) => this.hasLed(r, c)); }
  centeredTimeFits() { return [[3, 0], [7, 0], [3, 6], [7, 6]].every(([x, y]) => this.boxHasLeds(x, y)); }
  centeredIpFits() { return [[1, 0], [5, 0], [0, 6], [4, 6], [8, 6]].every(([x, y]) => this.boxHasLeds(x, y)); }
  // only the lit pixels of a glyph need to be on letters
  glyphFits(g, x, y) { for (let i = 0; i < FONT_H; i++) for (let j = 0; j < FONT_W; j++) if ((g[i] & (1 << j)) && !this.isLetter(i + y, FONT_W - j - 1 + x)) return false; return true; }
  ipFits(x, y) { return this.glyphFits(FONT_I_NARROW, x, y) && this.glyphFits(FONT_P, x + 2, y); }
  // nearest fitting slot to a corner (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right), within the corner's own quarter
  findSlot(corner, width, fits) {
    if (this.w < width || this.h < FONT_H) return null;
    const right = corner === 1 || corner === 3, bottom = corner >= 2;
    const maxX = this.w - width, maxY = this.h - FONT_H, halfW = Math.floor(this.w / 2), halfH = Math.floor(this.h / 2);
    const maxDx = right ? Math.max(0, maxX - (this.w - halfW)) : Math.max(0, halfW - width);
    const maxDy = bottom ? Math.max(0, maxY - (this.h - halfH)) : Math.max(0, halfH - FONT_H);
    for (let d = 0; d <= maxDx + maxDy; d++) for (let dy = 0; dy <= Math.min(d, maxDy); dy++) {
      const dx = d - dy; if (dx > maxDx) continue;
      const x = right ? maxX - dx : dx, y = bottom ? maxY - dy : dy;
      if (fits(x, y)) return [x, y];
    }
    return null;
  }
  cornerSlot(c) { return this.findSlot(c, FONT_W, (x, y) => this.fontFits(x, y)); }
  ipSlot() { return this.findSlot(0, FONT_IP_WIDTH, (x, y) => this.ipFits(x, y)); }
  print(g, x, y, t) { for (let i = 0; i < FONT_H; i++) for (let j = 0; j < FONT_W; j++) if (g[i] & (1 << j)) this.set(i + y, FONT_W - j - 1 + x, t); }
  showName() { for (let i = 0; i < this.layout.length; i++) { if (this.layout[i] === '*') this.set(Math.floor(i / this.w), i % this.w, 'name'); else if (this.layout[i] === '&') this.set(Math.floor(i / this.w), i % this.w, 'icon'); } }
  // words are looked up left to right, each after the previous one; returns the words that were not found
  showTimeString(ts) {
    const missing = []; let last = 0;
    for (const word of ts.trim().split(/ +/)) {
      const pos = this.layout.indexOf(word, last);
      if (pos >= 0) { for (let i = 0; i < word.length; i++) this.set(Math.floor((pos + i) / this.w), (pos + i) % this.w, 'time'); last = pos + word.length; }
      else missing.push(word);
    }
    this.showName();
    return missing;
  }
  showDigitalTime(hours, minutes) {
    const digits = [Math.floor(hours / 10), hours % 10, Math.floor(minutes / 10), minutes % 10];
    if (this.centeredTimeFits()) { [[3, 0], [7, 0], [3, 6], [7, 6]].forEach(([x, y], i) => this.print(FONT_NUMBERS[digits[i]], x, y, 'time')); }
    else digits.forEach((d, i) => { const s = this.cornerSlot(i); if (s) this.print(FONT_NUMBERS[d], s[0], s[1], 'time'); });
    this.showName();
  }
  // last part of the IP address without leading zeros: "IP" on top and the digits below, or in the corners
  printIP(address) {
    const digits = [];
    if (address >= 100) digits.push(Math.floor(address / 100));
    if (address >= 10) digits.push(Math.floor(address / 10) % 10);
    digits.push(address % 10);
    if (this.centeredIpFits()) {
      this.print(FONT_I, 1, 0, 'other'); this.print(FONT_P, 5, 0, 'other');
      digits.forEach((d, i) => this.print(FONT_NUMBERS[d], i * 4, 6, 'other'));
    } else {
      let corner = 0; const s = this.ipSlot();
      if (s) { this.print(FONT_I_NARROW, s[0], s[1], 'other'); this.print(FONT_P, s[0] + 2, s[1], 'other'); corner = 1; }
      for (const d of digits) { const c = this.cornerSlot(corner++); if (c) this.print(FONT_NUMBERS[d], c[0], c[1], 'other'); }
    }
  }
}
// everything a layout cannot show: words for any minute of the day, the digital time and the IP address
function checkLayout(layout, w, h) {
  const problems = [], mx = new SimMatrix(layout, w, h), missing = {};
  for (let hh = 0; hh < 24; hh++) for (let mm = 0; mm < 60; mm++) {
    for (const word of new SimMatrix(layout, w, h).showTimeString(timeToString(layout, hh, mm))) (missing[word] = missing[word] || []).push(`${String(hh).padStart(2, "0")}:${String(mm).padStart(2, "0")}`);
  }
  Object.entries(missing).sort((a, b) => b[1].length - a[1].length).forEach(([word, times]) =>
    problems.push({ what: 'word clock', text: `word "${word}" cannot be shown (${times.length} times, e.g. ${times.slice(0, 4).join(', ')})` }));
  const corners = ['top-left', 'top-right', 'bottom-left', 'bottom-right'];
  if (!mx.centeredTimeFits()) {
    for (let i = 0; i < 4; i++) if (!mx.cornerSlot(i)) problems.push({ what: 'digital clock', text: `no 3x5 block of letters near the ${corners[i]} corner for digit ${i + 1}` });
    let first = 1;
    if (!mx.ipSlot()) { first = 0; problems.push({ what: 'IP address', text: '"IP" does not fit in the top-left corner, only the digits are shown', info: true }); }
    for (let i = 0; i < 3; i++) if (!mx.cornerSlot(first + i)) problems.push({ what: 'IP address', text: `no 3x5 block of letters near the ${corners[first + i]} corner for IP digit ${i + 1}` });
  } else if (!mx.centeredIpFits()) {
    problems.push({ what: 'IP address', text: 'the digital time is centered but "IP" + 3 digits do not fit centered: the IP address is shown in the corners', info: true });
  }
  return problems;
}

// ---------------------------------------------------------------------------
// clock face editor (admin page): one cell per led, tap to type a letter
// ---------------------------------------------------------------------------
const SIM_COLORS = { time: '#ffffff', other: '#00ff00', name: '#ff4d4d', icon: '#4d8dff' };  // the IP address is always green, like the firmware
const ClockFace = {
  props: ['layout', 'width', 'height', 'lit'],
  emits: ['edit'],
  data() { return { editing: -1 }; },
  computed: {
    cells() { return Array.from({ length: this.width * this.height }, (_, i) => this.layout[i] || ' '); }
  },
  methods: {
    // what to show in a cell: letters as they are, '*' (name/picture led) as ★, '&' (icon led) as ●, no led as nothing
    symbol(ch) { return ch === '*' ? '★' : ch === '&' ? '●' : ' .'.includes(ch) ? '' : ch; },
    color(t) { return SIM_COLORS[t]; },
    startEdit(i) { if (this.lit) return; this.editing = i; this.$nextTick(() => { const el = this.$el.querySelector('input'); if (el) { el.focus(); el.select(); } }); },
    typed(i, v) { this.$emit('edit', i, v); if (v && i + 1 < this.width * this.height) this.startEdit(i + 1); },
  },
  template: `<div class="face" :class="{preview: !!lit}" :style="{gridTemplateColumns: 'repeat(' + width + ', 1fr)'}">
    <div v-for="(ch, i) in cells" :key="i" class="cell" :class="{noled: ' .'.includes(ch), special: '*&'.includes(ch), lit: lit && lit[i]}" :style="lit && lit[i] ? {'--c': color(lit[i])} : {}" :title="ch === '*' ? 'name/picture led (*)' : ch === '&' ? 'icon led (&)' : null" @click="startEdit(i)">
      <input v-if="!lit && editing === i" :value="' .'.includes(ch) ? '' : ch" @input="typed(i, $event.target.value.slice(-1))" @keydown.backspace.prevent="typed(i, '')" @blur="editing === i && (editing = -1)">
      <span v-else>{{ symbol(ch) }}</span>
    </div></div>`,
};
