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
  return { version, fsversion, loading, saved, klokuniek, clockWidth, clockHeight, cells, layoutString, asText, layoutText, gaps, setCell, fromText, save, resetWifi, resetSettings };
}

// ---------------------------------------------------------------------------
// clock face editor (admin page): one cell per led, tap to type a letter
// ---------------------------------------------------------------------------
const ClockFace = {
  props: ['layout', 'width', 'height'],
  emits: ['edit'],
  data() { return { editing: -1 }; },
  computed: {
    cells() { return Array.from({ length: this.width * this.height }, (_, i) => this.layout[i] || ' '); }
  },
  methods: {
    // what to show in a cell: letters as they are, '*' (name/picture led) as ★, '&' (icon led) as ●, no led as nothing
    symbol(ch) { return ch === '*' ? '★' : ch === '&' ? '●' : ' .'.includes(ch) ? '' : ch; },
    startEdit(i) { this.editing = i; this.$nextTick(() => { const el = this.$el.querySelector('input'); if (el) { el.focus(); el.select(); } }); },
    typed(i, v) { this.$emit('edit', i, v); if (v && i + 1 < this.width * this.height) this.startEdit(i + 1); },
  },
  template: `<div class="face" :style="{gridTemplateColumns: 'repeat(' + width + ', 1fr)'}">
    <div v-for="(ch, i) in cells" :key="i" class="cell" :class="{noled: ' .'.includes(ch), special: '*&'.includes(ch)}" :title="ch === '*' ? 'name/picture led (*)' : ch === '&' ? 'icon led (&)' : null" @click="startEdit(i)">
      <input v-if="editing === i" :value="' .'.includes(ch) ? '' : ch" @input="typed(i, $event.target.value.slice(-1))" @keydown.backspace.prevent="typed(i, '')" @blur="editing === i && (editing = -1)">
      <span v-else>{{ symbol(ch) }}</span>
    </div></div>`,
};
