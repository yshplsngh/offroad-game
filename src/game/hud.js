/**
 * hud.js - the instrument panel.
 *
 * DOM, not canvas. The HUD is text and bars that change a few times a second,
 * and the browser's text rasteriser is better at small type than anything we
 * would draw ourselves - at the cost of nothing, because none of this touches
 * the WebGL context or the frame budget.
 *
 * What an offroader actually needs to read, in priority order: what gear and
 * range you are in, whether the diffs are locked, how much the truck is leaning,
 * and what is under the tires. Speed is nearly irrelevant below walking pace,
 * so it is present but not the centrepiece a racing HUD would make it.
 *
 * Every update is diffed against the last value before touching the DOM. A
 * layout-triggering write at 60 Hz for a number that changed in the third
 * decimal is the classic way to make a HUD cost more than the renderer.
 */

const CSS = `
.hud, .hud * { box-sizing: border-box; }
.hud {
  position: fixed; inset: 0; pointer-events: none; z-index: 10;
  font: 12px/1.45 ui-monospace, "JetBrains Mono", Menlo, Consolas, monospace;
  color: #e8ecf2; letter-spacing: .02em;
  text-shadow: 0 1px 3px rgba(0,0,0,.85);
  --accent: #d2761f; --good: #6fbf73; --warn: #e0a02a; --bad: #d14b3c;
}
.hud.hidden { display: none; }
.hud .panel {
  position: absolute; background: rgba(12,15,19,.52);
  border: 1px solid rgba(255,255,255,.09); border-radius: 6px;
  padding: 8px 10px; backdrop-filter: blur(7px);
}
.hud .k { color: #8a93a0; }

/* ---- drive cluster, bottom left ---- */
.hud .drive { left: 18px; bottom: 18px; min-width: 232px; }
.hud .speed { display: flex; align-items: baseline; gap: 6px; }
.hud .speed b { font-size: 40px; font-weight: 600; line-height: 1; letter-spacing: -.02em; }
.hud .speed span { font-size: 11px; color: #8a93a0; }
.hud .gears { display: flex; gap: 6px; align-items: center; margin-top: 8px; }
.hud .chip {
  border: 1px solid rgba(255,255,255,.16); border-radius: 4px;
  padding: 2px 7px; font-size: 11px; color: #9aa3b0;
}
.hud .chip.on { color: #0d0f12; background: var(--accent); border-color: var(--accent); font-weight: 600; }
.hud .chip.lock1 { color: #0d0f12; background: var(--warn); border-color: var(--warn); font-weight: 600; }
.hud .chip.lock2 { color: #0d0f12; background: var(--bad); border-color: var(--bad); font-weight: 600; }
.hud .gear { font-size: 20px; font-weight: 700; min-width: 26px; text-align: center; }

.hud .tach { margin-top: 8px; height: 5px; border-radius: 3px; background: rgba(255,255,255,.1); overflow: hidden; }
.hud .tach i { display: block; height: 100%; width: 0%; background: var(--good); transition: background .2s; }
.hud .tach.red i { background: var(--bad); }

.hud .bars { margin-top: 7px; display: grid; grid-template-columns: 46px 1fr; gap: 3px 8px; align-items: center; }
.hud .bar { height: 4px; border-radius: 2px; background: rgba(255,255,255,.1); overflow: hidden; }
.hud .bar i { display: block; height: 100%; width: 0%; background: var(--accent); }

/* ---- attitude, bottom right ---- */
.hud .attitude { right: 18px; bottom: 18px; width: 158px; text-align: center; }
.hud .horizon {
  position: relative; height: 92px; margin: 2px -4px 6px; overflow: hidden;
  border-radius: 5px; background: rgba(0,0,0,.28);
}
.hud .horizon .sky { position: absolute; inset: -60% -40%; transform-origin: 50% 50%; }
.hud .horizon .sky::before, .hud .horizon .sky::after { content: ''; position: absolute; left: 0; right: 0; }
.hud .horizon .sky::before { top: 0; height: 50%; background: linear-gradient(#2c4a6e, #4d7398); }
.hud .horizon .sky::after { top: 50%; height: 50%; background: linear-gradient(#5a4526, #3a2c18); }
.hud .horizon .line { position: absolute; left: 8%; right: 8%; top: 50%; height: 1px; background: rgba(255,255,255,.55); }
.hud .horizon .mark { position: absolute; left: 50%; top: 50%; width: 46px; height: 2px; margin: -1px 0 0 -23px; background: var(--accent); }
.hud .att-nums { display: flex; justify-content: space-between; font-size: 11px; }

/* ---- compass, top centre ---- */
.hud .compass {
  left: 50%; top: 16px; transform: translateX(-50%);
  width: 292px; padding: 5px 0 4px; overflow: hidden; text-align: center;
}
.hud .compass .tape { position: relative; height: 16px; overflow: hidden; }
.hud .compass .tape i {
  position: absolute; top: 0; left: 0; white-space: pre; font-size: 11px;
  color: #b9c2ce; letter-spacing: 0;
}
.hud .compass .needle {
  position: absolute; left: 50%; top: 2px; width: 1px; height: 12px;
  background: var(--accent); transform: translateX(-50%);
}
.hud .compass .head { font-size: 11px; color: #8a93a0; margin-top: 2px; }

/* ---- world readout, top left ---- */
.hud .world { left: 18px; top: 16px; min-width: 186px; }
.hud .world div { display: flex; justify-content: space-between; gap: 14px; }

/* ---- wheels, right of the drive cluster ---- */
.hud .wheels { left: 268px; bottom: 18px; padding: 9px 11px; }
.hud .grid { display: grid; grid-template-columns: repeat(2, 30px); gap: 6px 22px; }
.hud .w { position: relative; height: 30px; border-radius: 3px; background: rgba(255,255,255,.08); overflow: hidden; }
.hud .w i { position: absolute; left: 0; right: 0; bottom: 0; height: 0%; background: var(--good); }
.hud .w.air { outline: 1px solid var(--bad); }
.hud .w b { position: absolute; inset: 0; display: grid; place-items: center; font-size: 9px; font-weight: 600; }
.hud .wlabel { text-align: center; font-size: 10px; color: #8a93a0; margin-top: 5px; }

/* ---- alerts, centre ---- */
.hud .alerts {
  left: 50%; bottom: 128px; transform: translateX(-50%);
  display: flex; flex-direction: column; align-items: center; gap: 5px;
  background: none; border: 0; padding: 0; backdrop-filter: none;
}
.hud .alert {
  background: rgba(12,15,19,.72); border: 1px solid rgba(255,255,255,.1);
  border-radius: 5px; padding: 5px 11px; font-size: 12px;
}
.hud .alert.warn { color: var(--warn); border-color: rgba(224,160,42,.45); }
.hud .alert.bad { color: var(--bad); border-color: rgba(209,75,60,.45); }

.hud .toast {
  left: 50%; bottom: 84px; transform: translateX(-50%);
  opacity: 0; transition: opacity .25s; font-size: 12px; white-space: nowrap;
}
.hud .toast.show { opacity: 1; }

/* ---- help ---- */
.hud .help {
  left: 50%; top: 50%; transform: translate(-50%, -50%);
  padding: 18px 22px; display: none; background: rgba(9,11,14,.9);
}
.hud .help.show { display: block; }
.hud .help h2 { margin: 0 0 12px; font-size: 13px; letter-spacing: .1em; color: var(--accent); }
.hud .help table { border-collapse: collapse; font-size: 12px; }
.hud .help td { padding: 2px 16px 2px 0; }
.hud .help td:first-child { color: var(--accent); white-space: nowrap; }
.hud .help .cols { display: flex; gap: 28px; }

.hud .stats { right: 18px; top: 16px; font-size: 11px; color: #9aa3b0; display: none; }
.hud .stats.show { display: block; }
`;

const DIRS = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];

const HELP_LEFT = [
  ['W / S', 'throttle / brake'],
  ['A / D', 'steer'],
  ['Space', 'handbrake'],
  ['Q / E', 'gear down / up'],
  ['L', 'high / low range'],
  ['X', 'diff lock: open - centre - full'],
  ['R', 'recover (right the truck)'],
];
const HELP_RIGHT = [
  ['F / G', 'winch: anchor / reel in'],
  ['C', 'camera mode'],
  ['drag', 'look around'],
  ['H / J', 'headlights / light bar'],
  ['T / Y', 'time of day'],
  ['B', 'wash the truck'],
  ['V', 'next vehicle'],
  ['P', 'pause'],
  ['/', 'this panel'],
  ['`', 'stats'],
];

function el(tag, cls, html) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (html !== undefined) n.innerHTML = html;
  return n;
}

export function createHUD({ mount = document.body } = {}) {
  if (!document.getElementById('hud-css')) {
    const style = el('style');
    style.id = 'hud-css';
    style.textContent = CSS;
    document.head.appendChild(style);
  }

  const root = el('div', 'hud');

  /* --- drive cluster --- */
  const drive = el('div', 'panel drive', `
    <div class="speed"><b id="h-kph">0</b><span>km/h</span>
      <span style="margin-left:auto" class="k" id="h-rpm">0 rpm</span></div>
    <div class="gears">
      <span class="gear" id="h-gear">1</span>
      <span class="chip" id="h-range">LOW</span>
      <span class="chip" id="h-lock">OPEN</span>
      <span class="chip" id="h-lights">LIGHTS</span>
    </div>
    <div class="tach" id="h-tach"><i></i></div>
    <div class="bars">
      <span class="k">throttle</span><span class="bar" id="h-thr"><i></i></span>
      <span class="k">brake</span><span class="bar" id="h-brk"><i></i></span>
      <span class="k">mud</span><span class="bar" id="h-mud"><i></i></span>
    </div>`);
  root.appendChild(drive);

  /* --- wheels --- */
  const wheelsPanel = el('div', 'panel wheels', `
    <div class="grid">
      <div class="w" id="h-w0"><i></i><b></b></div><div class="w" id="h-w1"><i></i><b></b></div>
      <div class="w" id="h-w2"><i></i><b></b></div><div class="w" id="h-w3"><i></i><b></b></div>
    </div>
    <div class="wlabel">load / slip</div>`);
  root.appendChild(wheelsPanel);

  /* --- attitude --- */
  const attitude = el('div', 'panel attitude', `
    <div class="horizon"><div class="sky" id="h-sky"><span class="line"></span></div><span class="mark"></span></div>
    <div class="att-nums"><span id="h-pitch">0&deg;</span><span class="k" id="h-alt">0 m</span><span id="h-roll">0&deg;</span></div>`);
  root.appendChild(attitude);

  /* --- compass --- */
  const compass = el('div', 'panel compass', `
    <div class="tape"><i id="h-tape"></i></div>
    <span class="needle"></span>
    <div class="head" id="h-head">000&deg; N</div>`);
  root.appendChild(compass);

  /* --- world --- */
  const world = el('div', 'panel world', `
    <div><span class="k">time</span><span id="h-clock">08:30</span></div>
    <div><span class="k">ground</span><span id="h-surface">dirt</span></div>
    <div><span class="k">biome</span><span id="h-biome">meadow</span></div>
    <div><span class="k">grade</span><span id="h-grade">0%</span></div>
    <div><span class="k">trip</span><span id="h-trip">0.00 km</span></div>`);
  root.appendChild(world);

  /* --- alerts / toast / help / stats --- */
  const alerts = el('div', 'panel alerts');
  root.appendChild(alerts);
  const toast = el('div', 'panel toast');
  root.appendChild(toast);

  const cols = (rows) => `<table>${rows.map(
    ([k, v]) => `<tr><td>${k}</td><td>${v}</td></tr>`).join('')}</table>`;
  const help = el('div', 'panel help', `
    <h2>RIDGELINE OFFROAD</h2>
    <div class="cols">${cols(HELP_LEFT)}${cols(HELP_RIGHT)}</div>`);
  root.appendChild(help);

  const stats = el('div', 'panel stats');
  root.appendChild(stats);

  mount.appendChild(root);

  const $ = (id) => root.querySelector(`#${id}`);
  const dom = {
    kph: $('h-kph'), rpm: $('h-rpm'), gear: $('h-gear'), range: $('h-range'),
    lock: $('h-lock'), lights: $('h-lights'), tach: $('h-tach'),
    thr: $('h-thr').firstElementChild, brk: $('h-brk').firstElementChild,
    mud: $('h-mud').firstElementChild,
    sky: $('h-sky'), pitch: $('h-pitch'), roll: $('h-roll'), alt: $('h-alt'),
    tape: $('h-tape'), head: $('h-head'),
    clock: $('h-clock'), surface: $('h-surface'), biome: $('h-biome'),
    grade: $('h-grade'), trip: $('h-trip'),
    wheels: [0, 1, 2, 3].map((i) => {
      const n = $(`h-w${i}`);
      return { box: n, fill: n.firstElementChild, txt: n.lastElementChild };
    }),
  };

  // Diff cache: every write below goes through set(), so the DOM only changes
  // when the value does.
  const last = new Map();
  function set(node, prop, value) {
    const key = node.__hudId ?? (node.__hudId = Math.random());
    const ck = `${key}:${prop}`;
    if (last.get(ck) === value) return;
    last.set(ck, value);
    if (prop === 'text') node.textContent = value;
    else if (prop === 'class') node.className = value;
    else node.style[prop] = value;
  }

  // 360 degrees of tape, drawn once and slid. One string, one transform.
  let tape = '';
  for (let d = -180; d < 540; d += 15) {
    const n = ((d % 360) + 360) % 360;
    tape += n % 90 === 0 ? DIRS[n / 45].padStart(2, ' ').padEnd(4, ' ')
      : n % 45 === 0 ? DIRS[n / 45].padStart(2, ' ').padEnd(4, ' ') : ' .  ';
  }
  dom.tape.textContent = tape;
  const TAPE_PX = 4 * 6.62;   // 4 monospace chars per 15 degrees, ~6.62px each

  let toastTimer = 0;
  let helpOn = false;
  let statsOn = false;

  return {
    root,

    toggle() { root.classList.toggle('hidden'); },
    toggleHelp() { helpOn = !helpOn; help.classList.toggle('show', helpOn); },
    toggleStats() { statsOn = !statsOn; stats.classList.toggle('show', statsOn); },
    get helpVisible() { return helpOn; },

    /** @param {string} msg @param {number} seconds */
    say(msg, seconds = 1.6) {
      toast.textContent = msg;
      toast.classList.add('show');
      toastTimer = seconds;
    },

    /**
     * @param {object} t   Controller telemetry.
     * @param {object} ctx { dt, clock, biome, lights, throttle, brake, statsText }
     */
    update(t, ctx = {}) {
      const dt = ctx.dt ?? 0.016;
      if (toastTimer > 0) {
        toastTimer -= dt;
        if (toastTimer <= 0) toast.classList.remove('show');
      }

      /* drive */
      set(dom.kph, 'text', Math.round(t.kph).toString());
      set(dom.rpm, 'text', `${Math.round(t.rpm / 10) * 10} rpm`);
      set(dom.gear, 'text', t.gearName);
      set(dom.range, 'class', `chip${t.lowRange ? ' on' : ''}`);
      set(dom.range, 'text', t.lowRange ? 'LOW' : 'HIGH');
      const lockCls = t.lock === 'FULL' ? ' lock2' : t.lock === 'CENTRE' ? ' lock1' : '';
      set(dom.lock, 'class', `chip${lockCls}`);
      set(dom.lock, 'text', t.lock);
      set(dom.lights, 'class', `chip${ctx.lights ? ' on' : ''}`);

      const rev = Math.min(1, t.rpm / t.redline);
      set(dom.tach.firstElementChild, 'width', `${(rev * 100).toFixed(0)}%`);
      set(dom.tach, 'class', `tach${rev > 0.92 ? ' red' : ''}`);
      set(dom.thr, 'width', `${Math.round((ctx.throttle ?? 0) * 100)}%`);
      set(dom.brk, 'width', `${Math.round((ctx.brake ?? 0) * 100)}%`);
      set(dom.mud, 'width', `${Math.round(t.mud * 100)}%`);

      /* wheels: height is load, colour is slip, outline means airborne */
      for (let i = 0; i < 4; i++) {
        const w = t.wheels[i];
        const d = dom.wheels[i];
        const load = Math.min(1, w.load / (t.nominalLoad ?? 9000));
        set(d.fill, 'height', `${Math.round(load * 100)}%`);
        set(d.fill, 'background', w.slip > 0.85 ? 'var(--bad)'
          : w.slip > 0.55 ? 'var(--warn)' : 'var(--good)');
        set(d.box, 'class', `w${w.contact ? '' : ' air'}`);
        set(d.txt, 'text', w.sink > 0.05 ? `${Math.round(w.sink * 100)}` : '');
      }

      /* attitude */
      const pitchDeg = t.pitch * 57.2958;
      const rollDeg = t.roll * 57.2958;
      set(dom.sky, 'transform', `rotate(${rollDeg.toFixed(1)}deg) translateY(${(pitchDeg * 1.1).toFixed(1)}%)`);
      set(dom.pitch, 'text', `${pitchDeg > 0 ? '+' : ''}${pitchDeg.toFixed(0)}\u00B0`);
      set(dom.roll, 'text', `${rollDeg > 0 ? '+' : ''}${rollDeg.toFixed(0)}\u00B0`);
      set(dom.alt, 'text', `${t.altitude.toFixed(0)} m`);
      set(dom.pitch, 'color', Math.abs(pitchDeg) > 28 ? 'var(--warn)' : '');
      set(dom.roll, 'color', Math.abs(rollDeg) > 32 ? 'var(--bad)'
        : Math.abs(rollDeg) > 22 ? 'var(--warn)' : '');

      /* compass: heading 0 = +Z = north, increasing eastward */
      const headDeg = ((-t.heading * 57.2958) % 360 + 360) % 360;
      set(dom.tape, 'transform',
        `translateX(${(146 - (headDeg + 180) / 15 * TAPE_PX).toFixed(1)}px)`);
      set(dom.head, 'text',
        `${headDeg.toFixed(0).padStart(3, '0')}\u00B0 ${DIRS[Math.round(headDeg / 45) % 8]}`);

      /* world */
      if (ctx.clock !== undefined) {
        const h = Math.floor(ctx.clock);
        const m = Math.floor((ctx.clock - h) * 60);
        set(dom.clock, 'text', `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`);
      }
      set(dom.surface, 'text', t.wetness > 0.35 ? `${t.surface.name} (wet)` : t.surface.name);
      if (ctx.biome) set(dom.biome, 'text', ctx.biome);
      set(dom.grade, 'text', `${(Math.tan(-t.pitch) * 100).toFixed(0)}%`);
      set(dom.trip, 'text', `${(t.odometer / 1000).toFixed(2)} km`);

      /* alerts */
      const want = [];
      if (t.airborne === 4) want.push(['AIRBORNE', 'warn']);
      if (Math.abs(rollDeg) > 40) want.push(['ROLLOVER RISK', 'bad']);
      if (t.stuck > 0.55) want.push([`STUCK ${Math.round(t.stuck * 100)}% - R to recover, F to winch`, 'bad']);
      if (t.winch) want.push([t.winchTension > 0.05
        ? `WINCH ${Math.round(t.winchTension * 100)}%` : 'WINCH ANCHORED', 'warn']);
      if (t.surface.name === 'water') want.push(['FORDING', 'warn']);
      const sig = want.map((w) => w[0]).join('|');
      if (sig !== alerts.__sig) {
        alerts.__sig = sig;
        alerts.textContent = '';
        for (const [msg, cls] of want) alerts.appendChild(el('div', `alert ${cls}`, msg));
      }

      if (statsOn && ctx.statsText) stats.textContent = ctx.statsText;
    },

    dispose() { root.remove(); },
  };
}
