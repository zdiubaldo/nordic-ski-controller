import {test} from 'node:test';
import assert from 'node:assert/strict';
import {ControllerClient, Receiver, frames, commandText, HttpTransport, type Snapshot, type Reply} from '../src/index.ts';
const state = (revision = '1', epoch = 1, boot_id = 7): Snapshot => ({protocol_version:1, boot_id, revision, control_epoch:epoch, owner_client_id:4, mode:'idle', fault:0, requested_speed_mps:0, requested_grade_percent:0});
function fixture() {
  const sent: string[] = [];
  const c = new ControllerClient({request: async body => {
    sent.push(body);
    return body === 'connect' ? {ok:true, token:'0123456789abcdef', client_id:4, control_epoch:1} : {ok:true, control_epoch:1, revision:'10'};
  }, close: async () => {}});
  return {c, sent};
}
test('20-byte writes preserve complete headers and ASCII payload', () => {
  const body = commandText('0123456789abcdef', 42, 5, 'targets', 1.5, 3);
  const packets = frames(body, 65535);
  assert.ok(packets.every(p => p.length <= 20 && p[0] === 1 && p[1] === 1));
  assert.equal(packets.flatMap(p => p.slice(8)).map(n => String.fromCharCode(n)).join(''), body);
  assert.throws(() => frames('bad\ncommand', 1));
  assert.throws(() => commandText('0123456789abcdef', 0, 1, 'start'));
});
test('independent notification streams reassemble UTF-8; reject gaps and cross-characteristic data', () => {
  const rx = new Receiver();
  const bytes = [...new TextEncoder().encode('{"name":"Ski ⛷"}')];
  const packet = (offset: number, n: number) => [1, 4, 1, 0, bytes.length, 0, offset, 0, ...bytes.slice(offset, offset+n)];
  assert.equal(rx.feed(packet(0, 12), 4), undefined);
  assert.equal(rx.feed(packet(12, bytes.length), 4)?.value.name, 'Ski ⛷');
  assert.throws(() => rx.feed(packet(12, bytes.length), 4));
  assert.throws(() => rx.feed(packet(0, 12), 3));
});
test('same-boot revision barrier keeps stale in-flight status from undoing accepted commands', async () => {
  const {c} = fixture(); c.accept(state()); await c.register(); await c.command('claim');
  c.accept(state('9')); assert.equal(c.state?.revision, '1');
  c.accept(state('10')); assert.equal(c.state?.revision, '10');
  c.accept(state('8')); assert.equal(c.state?.revision, '10');
  assert.throws(() => c.accept(state('1', 1, 8)), /restarted/);
  assert.equal(c.token, undefined);
});
test('Stop cancels queued movement and sequence numbers remain monotonic', async () => {
  const {c, sent} = fixture(); await c.register();
  const movement = c.command('start'); const stop = c.command('stop');
  await assert.rejects(movement, /cancelled/); await stop;
  assert.equal(sent.length, 2); assert.match(sent[1], / 1 1 stop 0 0$/);
});
test('new epoch cancels previously queued movement without retry', async () => {
  const {c, sent} = fixture(); c.accept(state()); await c.register();
  const movement = c.command('targets', 1, 2); c.accept(state('2', 2));
  await assert.rejects(movement, /cancelled/); assert.equal(sent.length, 1);
});
test('unknown command outcome invalidates session and prevents queued actions', async () => {
  const {c} = fixture(); await c.register();
  c.transport.request = async () => { throw new Error('timeout'); };
  await assert.rejects(c.command('claim'), /timeout/);
  assert.equal(c.closed, true); await assert.rejects(c.command('start'), /Register/);
});
test('rejection consumes sequence and updates epoch without acquiring control', async () => {
  const {c} = fixture(); await c.register();
  c.transport.request = async () => ({ok:false, reason:'control_in_use', control_epoch:3, revision:'15'});
  await assert.rejects(c.command('claim'), /control_in_use/);
  assert.equal(c.sequence, 1); assert.equal(c.epoch, 3); assert.equal(c.ownsControl, false);
});
test('HTTP adapter uses existing endpoint, header and wire encoding', async () => {
  const original = globalThis.fetch;
  let observed: [unknown, RequestInit | undefined] | undefined;
  globalThis.fetch = async (url, options) => {
    observed = [url, options];
    return new Response(JSON.stringify({...state('5'), command_result:{reason:'not_owner'}}), {status:409});
  };
  try {
    const transport = new HttpTransport('http://controller');
    const r = await transport.request('v1 0123456789abcdef 1 1 claim 0 0');
    assert.equal(observed?.[0], 'http://controller/api/test/command');
    assert.deepEqual(observed?.[1]?.headers, {'X-Nordic-Test':'1'});
    assert.equal(r.ok, false); assert.equal(r.reason, 'not_owner'); assert.equal(r.snapshot?.revision, '5');
  } finally {globalThis.fetch = original;}
});
test('Stop bypasses a pending heartbeat result; late replies cannot restore an old epoch', async () => {
  const {c} = fixture(); c.accept(state()); await c.register();
  let resolveHeartbeat!: (reply: Reply) => void;
  const sent: string[] = [];
  c.transport.request = body => {
    sent.push(body);
    return body.includes(' heartbeat ') ? new Promise(resolve => {resolveHeartbeat = resolve;}) : Promise.resolve({ok:true, control_epoch:2, revision:'20'});
  };
  const heartbeat = c.command('heartbeat'); await Promise.resolve();
  await c.command('stop');
  assert.equal(sent.length, 2); assert.equal(c.epoch, 2);
  resolveHeartbeat({ok:true,control_epoch:1,revision:'19'}); await heartbeat;
  assert.equal(c.epoch,2); assert.equal(c.revision,20n);
});
