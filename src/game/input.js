/**
 * input.js - keyboard and gamepad, normalised into one small state object.
 *
 * Two kinds of control, kept apart on purpose:
 *
 *   AXES are continuous and read every frame - throttle, brake, steer. The
 *   controller does its own rate limiting (tune.steerRate), so a key here is
 *   simply 0 or 1 and a stick passes straight through. Smoothing input twice
 *   is how you get a truck that feels like it is steering through treacle.
 *
 *   ACTIONS are edge-triggered and consumed once - shift, lock, lights. They
 *   queue rather than latch, so a shift pressed during a slow frame is never
 *   dropped and never fires twice.
 *
 * Key repeat is ignored (`e.repeat`), because holding E must not run you up
 * through every gear.
 */

const KEY_AXES = {
  KeyW: ['throttle', 1], ArrowUp: ['throttle', 1],
  KeyS: ['brake', 1], ArrowDown: ['brake', 1],
  KeyA: ['steer', -1], ArrowLeft: ['steer', -1],
  KeyD: ['steer', 1], ArrowRight: ['steer', 1],
  Space: ['handbrake', 1],
  KeyG: ['winch', 1],
};

const KEY_ACTIONS = {
  KeyE: 'gearUp',
  KeyQ: 'gearDown',
  KeyX: 'lock',
  KeyL: 'range',
  KeyH: 'lights',
  KeyJ: 'aux',
  KeyC: 'camera',
  KeyR: 'recover',
  KeyF: 'winch',
  KeyT: 'timeForward',
  KeyY: 'timeBack',
  KeyP: 'pause',
  KeyM: 'map',
  KeyB: 'wash',
  KeyV: 'nextVehicle',
  Slash: 'help',
  Backquote: 'debug',
  Escape: 'pause',
};

/** Standard-mapping gamepad. Anything else falls back to the keyboard. */
const PAD_ACTIONS = {
  0: 'recover',      // A / cross
  1: 'lock',         // B / circle
  2: 'winch',        // X / square
  3: 'camera',       // Y / triangle
  4: 'gearDown',     // LB
  5: 'gearUp',       // RB
  8: 'map',          // back
  9: 'pause',        // start
  10: 'range',       // L3
};

export function createInput({ domElement = window } = {}) {
  const held = new Set();
  const queue = [];
  const axes = { throttle: 0, brake: 0, steer: 0, handbrake: 0, winch: 0 };
  const padPrev = [];
  let padIndex = -1;
  let usingPad = false;

  const onKeyDown = (e) => {
    if (e.target && /^(INPUT|TEXTAREA)$/.test(e.target.tagName)) return;
    if (e.repeat) return;
    if (KEY_AXES[e.code] || KEY_ACTIONS[e.code]) e.preventDefault();
    held.add(e.code);
    const action = KEY_ACTIONS[e.code];
    if (action) queue.push(action);
  };
  const onKeyUp = (e) => { held.delete(e.code); };
  const onBlur = () => held.clear();

  window.addEventListener('keydown', onKeyDown, { passive: false });
  window.addEventListener('keyup', onKeyUp);
  window.addEventListener('blur', onBlur);
  window.addEventListener('gamepadconnected', (e) => { padIndex = e.gamepad.index; });
  window.addEventListener('gamepaddisconnected', () => { padIndex = -1; usingPad = false; });

  function readKeyboard() {
    axes.throttle = 0; axes.brake = 0; axes.steer = 0;
    axes.handbrake = 0; axes.winch = 0;
    for (const code of held) {
      const a = KEY_AXES[code];
      if (!a) continue;
      if (a[0] === 'steer') axes.steer += a[1];
      else axes[a[0]] = Math.max(axes[a[0]], a[1]);
    }
    axes.steer = Math.max(-1, Math.min(1, axes.steer));
  }

  function readPad() {
    const pads = navigator.getGamepads?.() ?? [];
    const pad = padIndex >= 0 ? pads[padIndex] : pads.find?.((p) => p && p.connected);
    if (!pad) return false;

    const dead = (v, d = 0.12) => (Math.abs(v) < d ? 0 : (v - Math.sign(v) * d) / (1 - d));
    const steer = dead(pad.axes[0] ?? 0);
    const throttle = pad.buttons[7]?.value ?? 0;
    const brake = pad.buttons[6]?.value ?? 0;
    const handbrake = pad.buttons[1]?.pressed ? 1 : 0;

    const active = Math.abs(steer) > 0 || throttle > 0.02 || brake > 0.02;
    if (active) usingPad = true;
    if (usingPad) {
      axes.steer = steer;
      axes.throttle = throttle;
      axes.brake = brake;
      axes.handbrake = Math.max(axes.handbrake, handbrake);
    }

    for (const [i, action] of Object.entries(PAD_ACTIONS)) {
      const now = !!pad.buttons[i]?.pressed;
      if (now && !padPrev[i]) queue.push(action);
      padPrev[i] = now;
    }
    // D-pad up/down nudges the clock; left/right is the winch.
    if (pad.buttons[12]?.pressed && !padPrev[12]) queue.push('timeForward');
    padPrev[12] = !!pad.buttons[12]?.pressed;
    axes.winch = Math.max(axes.winch, pad.buttons[14]?.pressed ? 1 : 0);
    return true;
  }

  return {
    axes,
    /** Any key currently down, by KeyboardEvent.code. */
    isDown: (code) => held.has(code),
    get usingGamepad() { return usingPad; },

    /** Refresh the axes. Call once per rendered frame, before the physics. */
    poll() {
      readKeyboard();
      readPad();
      return axes;
    },

    /**
     * Drain the queued edge actions. The caller gets each press exactly once.
     * @returns {string[]}
     */
    actions() {
      if (queue.length === 0) return queue;
      const out = queue.slice();
      queue.length = 0;
      return out;
    },

    dispose() {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      window.removeEventListener('blur', onBlur);
    },
  };
}
