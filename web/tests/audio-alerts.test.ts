import test from 'node:test';
import assert from 'node:assert/strict';
import { AudioAlerts, AUDIO_PREFERENCE_KEY } from '../src/lib/audio-alerts.ts';

function harness(initial = 'false') {
  const values = new Map([[AUDIO_PREFERENCE_KEY, initial]]);
  const states: any[] = [];
  const contexts: any[] = [];
  const storage = () => ({ getItem: (key: string) => values.get(key) ?? null,
    setItem: (key: string, value: string) => { values.set(key, value); } });
  const makeContext = () => {
    const ctx: any = { state: 'suspended', onstatechange: null, calls: 0,
      resume() { this.calls++; this.state = 'running'; this.onstatechange?.(); return Promise.resolve(); },
      close() { this.state = 'closed'; return Promise.resolve(); } };
    contexts.push(ctx); return ctx as AudioContext;
  };
  const create = () => new AudioAlerts(storage, makeContext, state => states.push(state));
  return { values, states, contexts, storage, makeContext, create };
}

test('enabled preference survives navigation; restored page waits for gesture', async () => {
  const h = harness(); const first = h.create();
  first.setEnabled(true); assert.equal(await first.activate(), true);
  first.dispose(); assert.equal(h.values.get(AUDIO_PREFERENCE_KEY), 'true');
  const restored = h.create();
  assert.equal(restored.enabled, true); assert.equal(restored.context, null);
  assert.deepEqual(h.states.at(-1), { enabled: true, ready: false, pending: false });
  assert.equal(await restored.activate(), true);
  assert.equal(h.states.at(-1).ready, true);
});

test('disable is saved and reopening never creates an audio context', async () => {
  const h = harness('true'); const a = h.create();
  await a.activate(); a.setEnabled(false);
  assert.equal(h.contexts[0].state, 'closed');
  const b = h.create(); assert.equal(b.enabled, false);
  assert.equal(await b.activate(), false); assert.equal(h.contexts.length, 1);
});

test('resume is invoked synchronously inside the activation handler', async () => {
  const h = harness('true'); const a = h.create();
  const result = a.activate();
  assert.equal(h.contexts[0].calls, 1); await result;
});

test('autoplay rejection retains saved preference and permits a later gesture', async () => {
  const h = harness('true');
  const a = new AudioAlerts(h.storage, () => {
    const ctx: any = h.makeContext();
    ctx.resume = function () { this.calls++; return this.calls === 1
      ? Promise.reject(new Error('gesture required')) : (this.state = 'running', Promise.resolve()); };
    return ctx;
  }, state => h.states.push(state));
  assert.equal(await a.activate(), false);
  assert.equal(a.enabled, true); assert.equal(h.values.get(AUDIO_PREFERENCE_KEY), 'true');
  assert.equal(a.pending, false); assert.equal(await a.activate(), true);
});

test('late resume cannot enable audio after the user disables it', async () => {
  const h = harness('true'); let finish!: () => void;
  const a = new AudioAlerts(h.storage, () => {
    const ctx: any = h.makeContext();
    ctx.resume = () => new Promise<void>(resolve => { finish = resolve; }); return ctx;
  }, state => h.states.push(state));
  const pending = a.activate(); a.setEnabled(false); finish();
  assert.equal(await pending, false); assert.equal(a.context, null);
  assert.deepEqual(h.states.at(-1), { enabled: false, ready: false, pending: false });
});

test('restricted storage does not crash or prevent this session from playing', async () => {
  const h = harness();
  const a = new AudioAlerts(() => { throw new Error('storage denied'); }, h.makeContext, () => {});
  a.setEnabled(true); assert.equal(await a.activate(), true);
});

test('context suspension updates the hint; dispose preserves preference', async () => {
  const h = harness('true'); const a = h.create(); await a.activate();
  h.contexts[0].state = 'suspended'; h.contexts[0].onstatechange();
  assert.equal(h.states.at(-1).ready, false);
  const count = h.states.length; a.dispose();
  assert.equal(h.values.get(AUDIO_PREFERENCE_KEY), 'true');
  assert.equal(h.states.length, count);
});
