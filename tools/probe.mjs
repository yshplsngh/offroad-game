/**
 * Headless driving probe for Ridgeline Offroad.
 *
 * Drives the real game in headless Chrome over the DevTools protocol, sending
 * genuine key events so the whole input -> physics -> telemetry path is
 * exercised, then prints telemetry samples.
 *
 * Usage:  node probe.mjs [url] [--keys=KeyW,KeyD] [--seconds=6]
 */
const URL_ARG = process.argv[2] || 'http://127.0.0.1:5191/index.html';
const arg = (name, dflt) => {
  const hit = process.argv.find((a) => a.startsWith(`--${name}=`));
  return hit ? hit.split('=').slice(1).join('=') : dflt;
};
const DRIVE_KEYS = arg('keys', 'KeyW').split(',').filter(Boolean);
const SECONDS = Number(arg('seconds', 6));
const PORT = Number(arg('port', 9222));

const KEY_INFO = {
  KeyW: { key: 'w', code: 'KeyW', vk: 87 },
  KeyA: { key: 'a', code: 'KeyA', vk: 65 },
  KeyS: { key: 's', code: 'KeyS', vk: 83 },
  KeyD: { key: 'd', code: 'KeyD', vk: 68 },
  KeyE: { key: 'e', code: 'KeyE', vk: 69 },
  KeyQ: { key: 'q', code: 'KeyQ', vk: 81 },
  KeyX: { key: 'x', code: 'KeyX', vk: 88 },
  KeyL: { key: 'l', code: 'KeyL', vk: 76 },
  KeyR: { key: 'r', code: 'KeyR', vk: 82 },
  Space: { key: ' ', code: 'Space', vk: 32 },
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function targets() {
  const res = await fetch(`http://127.0.0.1:${PORT}/json/list`);
  return res.json();
}

let nextId = 1;
function connect(wsUrl) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    const pending = new Map();
    const logs = [];
    ws.addEventListener('open', () => resolve({ ws, send, logs }));
    ws.addEventListener('error', reject);
    ws.addEventListener('message', (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && pending.has(msg.id)) {
        const { resolve: res, reject: rej } = pending.get(msg.id);
        pending.delete(msg.id);
        if (msg.error) rej(new Error(JSON.stringify(msg.error)));
        else res(msg.result);
      } else if (msg.method === 'Runtime.consoleAPICalled') {
        logs.push(`[${msg.params.type}] ${msg.params.args.map((a) => a.value ?? a.description).join(' ')}`);
      } else if (msg.method === 'Runtime.exceptionThrown') {
        logs.push(`[exception] ${msg.params.exceptionDetails.exception?.description
          ?? msg.params.exceptionDetails.text}`);
      }
    });
    function send(method, params = {}) {
      const id = nextId++;
      ws.send(JSON.stringify({ id, method, params }));
      return new Promise((res, rej) => pending.set(id, { resolve: res, reject: rej }));
    }
  });
}

async function evaluate(send, expression) {
  const r = await send('Runtime.evaluate', {
    expression, returnByValue: true, awaitPromise: true,
  });
  if (r.exceptionDetails) {
    throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
  }
  return r.result.value;
}

async function key(send, code, type) {
  const info = KEY_INFO[code];
  if (!info) throw new Error(`unmapped key ${code}`);
  await send('Input.dispatchKeyEvent', {
    type,
    key: info.key,
    code: info.code,
    windowsVirtualKeyCode: info.vk,
    nativeVirtualKeyCode: info.vk,
    text: type === 'keyDown' ? info.key : undefined,
  });
}

const SNAPSHOT = `(() => {
  const R = window.RIDGELINE;
  if (!R || !R.controller) return null;
  const t = R.controller.telemetry();
  const s = R.terrain.stats();
  return {
    kph: +t.kph.toFixed(2),
    fwd: +t.forwardSpeed.toFixed(2),
    rpm: Math.round(t.rpm),
    gear: t.gearName,
    low: t.lowRange,
    lock: t.lock,
    surface: t.surface.name,
    slip: +t.slip.toFixed(2),
    stuck: +t.stuck.toFixed(2),
    air: t.airborne,
    pitch: +(t.pitch * 57.3).toFixed(1),
    roll: +(t.roll * 57.3).toFixed(1),
    trip: +t.odometer.toFixed(1),
    mud: +t.mud.toFixed(2),
    steer: +R.controller.steerAngle.toFixed(3),
    pos: [+R.controller.position.x.toFixed(1), +R.controller.position.y.toFixed(2), +R.controller.position.z.toFixed(1)],
    loads: t.wheels.map((w) => Math.round(w.load)),
    travel: t.wheels.map((w) => +w.travel.toFixed(3)),
    omega: t.wheels.map((w) => +w.omega.toFixed(1)),
    contact: t.wheels.map((w) => (w.contact ? 1 : 0)),
    chunks: s.chunks,
    pending: s.pending,
    draws: R.renderer.info.render.calls,
    tris: R.renderer.info.render.triangles,
  };
})()`;

async function main() {
  const list = await targets();
  const page = list.find((t) => t.type === 'page');
  if (!page) throw new Error('no page target - is chrome running with --remote-debugging-port?');

  const { send, logs } = await connect(page.webSocketDebuggerUrl);
  await send('Runtime.enable');
  await send('Page.enable');
  await send('Page.navigate', { url: URL_ARG });

  // Wait for boot.
  let ready = false;
  for (let i = 0; i < 90; i++) {
    await sleep(500);
    ready = await evaluate(send, '!!(window.RIDGELINE && window.RIDGELINE.controller)').catch(() => false);
    if (ready) break;
  }
  if (!ready) {
    console.log('FAILED TO BOOT');
    console.log(await evaluate(send, 'document.getElementById("boot")?.innerText ?? "(no boot overlay)"'));
    console.log(logs.join('\n'));
    process.exit(1);
  }

  await sleep(1500);
  console.log('--- settled ---');
  console.log(JSON.stringify(await evaluate(send, SNAPSHOT)));

  for (const k of DRIVE_KEYS) await key(send, k, 'keyDown');
  console.log(`--- driving with ${DRIVE_KEYS.join('+')} ---`);
  for (let i = 0; i < SECONDS; i++) {
    await sleep(1000);
    console.log(`t+${i + 1}s ${JSON.stringify(await evaluate(send, SNAPSHOT))}`);
  }
  for (const k of DRIVE_KEYS) await key(send, k, 'keyUp');

  await sleep(2500);
  console.log('--- coasting ---');
  console.log(JSON.stringify(await evaluate(send, SNAPSHOT)));

  if (logs.length) {
    console.log('--- console ---');
    console.log(logs.slice(-40).join('\n'));
  }
  process.exit(0);
}

main().catch((e) => { console.error(e); process.exit(1); });
