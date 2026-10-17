export type Snapshot = {
  protocol_version: number; boot_id: number; revision: string; control_epoch: number;
  owner_client_id: number; mode: 'idle' | 'running' | 'fault'; fault: number;
  requested_speed_mps: number; requested_grade_percent: number;
  [key: string]: unknown;
};
export type Reply = {ok: boolean; reason?: string; token?: string; client_id?: number;
  control_epoch?: number; revision?: string; snapshot?: Snapshot};
export interface Transport {
  request(body: string): Promise<Reply>;
  close(): Promise<void>;
}
export const UUID = Array.from({length: 6}, (_, i) => `75e6000${i}-6b92-4e3d-a347-8bc06c51d210`);
export function commandText(token: string, sequence: number, epoch: number, action: string, a = 0, b = 0) {
  if (!/^[a-f0-9]{16}$/.test(token) || !Number.isInteger(sequence) || sequence < 1 || sequence > 0xffffffff ||
      !Number.isInteger(epoch) || epoch < 0 || epoch > 0xffffffff || !Number.isFinite(a) || !Number.isFinite(b) ||
      !/^(claim|release|start|stop|heartbeat|reset|disconnect|cancel_workout|speed_up|speed_down|incline_up|incline_down|targets|w:[a-z0-9_-]+)$/.test(action)) throw new Error('Invalid command');
  const body = `v1 ${token} ${sequence} ${epoch} ${action} ${a} ${b}`;
  if (body.length >= 160) throw new Error('Command too long');
  return body;
}
export function frames(body: string, id: number): number[][] {
  if (!/^[\x20-\x7e]{1,191}$/.test(body) || !Number.isInteger(id) || id < 1 || id > 65535) throw new Error('Invalid frame');
  const packets: number[][] = [];
  for (let offset = 0; offset < body.length; offset += 12) {
    packets.push([1, 1, id & 255, id >> 8, body.length & 255, body.length >> 8, offset & 255, offset >> 8,
      ...Array.from(body.slice(offset, offset + 12), c => c.charCodeAt(0))]);
  }
  return packets;
}
// Decode UTF-8 without relying on Node Buffer or a native TextDecoder polyfill.
export function jsonBytes(bytes: number[]): any {
  return JSON.parse(decodeURIComponent(bytes.map(b => '%' + b.toString(16).padStart(2, '0')).join('')));
}
export class Receiver {
  pending = new Map<number, {id: number; total: number; bytes: number[]; at: number}>();
  feed(packet: number[], expectedKind: number) {
    const [version, kind] = packet;
    const id = packet[2] | packet[3] << 8, total = packet[4] | packet[5] << 8, offset = packet[6] | packet[7] << 8;
    if (packet.length <= 8 || version !== 1 || kind !== expectedKind || ![2,3,4].includes(kind) || !id || !total || offset + packet.length - 8 > total) {
      this.pending.delete(expectedKind); throw new Error('Invalid notification');
    }
    if (!offset) this.pending.set(kind, {id, total, bytes: [], at: Date.now()});
    const part = this.pending.get(kind);
    if (!part || part.id !== id || part.total !== total || part.bytes.length !== offset || Date.now() - part.at > 60000) {
      this.pending.delete(kind); throw new Error('Out-of-order notification');
    }
    part.bytes.push(...packet.slice(8));
    if (part.bytes.length !== total) return;
    this.pending.delete(kind);
    return {kind, id, value: jsonBytes(part.bytes)};
  }
}
export class ControllerClient {
  transport: Transport;
  token?: string;
  clientId = 0;
  sequence = 0;
  epoch = 0;
  revision = 0n;
  state?: Snapshot;
  closed = false;
  private tail: Promise<unknown> = Promise.resolve();
  private generation = 0;
  onState: (state: Snapshot) => void = () => {};
  constructor(transport: Transport) { this.transport = transport; }
  get ownsControl() { return !!this.token && this.state?.owner_client_id === this.clientId; }
  accept(state: Snapshot) {
    if (state.protocol_version !== 1 || !/^\d+$/.test(state.revision) || !['idle','running','fault'].includes(state.mode)) throw new Error('Invalid status');
    if (this.state && state.boot_id !== this.state.boot_id) { this.invalidate(); throw new Error('Controller restarted; reconnect'); }
    if (BigInt(state.revision) < this.revision || (this.state && BigInt(state.revision) < BigInt(this.state.revision))) return;
    if (this.state && this.epoch !== state.control_epoch) this.generation++;
    this.state = state; this.epoch = state.control_epoch; this.onState(state);
  }
  invalidate() { this.closed = true; this.token = undefined; this.clientId = 0; this.generation++; }
  private enqueue<T>(work: () => Promise<T>): Promise<T> {
    const next = this.tail.then(work); this.tail = next.catch(() => {}); return next;
  }
  async register() {
    return this.enqueue(async () => {
      if (this.closed) throw new Error('Reconnect required');
      if (this.token) return;
      const r = await this.transport.request('connect');
      if (!r.ok || !r.token || !/^[a-f0-9]{16}$/.test(r.token) || !r.client_id || r.control_epoch === undefined) throw new Error(r.reason || 'Registration failed');
      this.token = r.token; this.clientId = r.client_id; this.epoch = r.control_epoch;
    });
  }
  command(action: string, a = 0, b = 0) {
    if (action === 'stop' || action === 'release') this.generation++;
    const generation = this.generation;
    const epoch = this.epoch;
    const execute = async () => {
      if (this.closed || !this.token) throw new Error('Register first');
      if (!['stop','heartbeat','release'].includes(action) && generation !== this.generation) throw new Error('Command cancelled by state change');
      let r: Reply;
      try { r = await this.transport.request(commandText(this.token, ++this.sequence, epoch, action, a, b)); }
      catch (error) { this.invalidate(); throw error; } // Unknown outcomes are never retried as movement.
      const fresh = r.revision === undefined || BigInt(r.revision) >= this.revision;
      if (r.control_epoch !== undefined && fresh) {
        if (r.control_epoch !== this.epoch) this.generation++;
        this.epoch = r.control_epoch;
      }
      if (r.revision !== undefined) this.revision = this.revision > BigInt(r.revision) ? this.revision : BigInt(r.revision);
      if (r.snapshot) this.accept(r.snapshot);
      // A priority Stop can overtake an in-flight heartbeat at the ESP.
      if (!r.ok && action === 'heartbeat' && generation !== this.generation && r.reason === 'replayed_sequence') return r;
      if (!r.ok) { if (r.reason === 'unauthorized') this.invalidate(); throw new Error(r.reason || 'Command rejected'); }
      return r;
    };
    // Stop bypasses response waits. The transport still serializes complete frame writes.
    return action === 'stop' ? execute() : this.enqueue(execute);
  }
}
export class HttpTransport implements Transport {
  base: string;
  constructor(base = '') { this.base = base; }
  async request(body: string): Promise<Reply> {
    const path = body === 'connect' ? '/api/session' : body === 'status' ? '/api/status' : body === 'workouts' ? '/api/workouts' : '/api/test/command';
    const abort = new AbortController(), timer = setTimeout(() => abort.abort(), 2000);
    try {
      const response = await fetch(this.base + path, {method: ['status','workouts'].includes(body) ? 'GET' : 'POST',
        headers: {'X-Nordic-Test': '1'}, body: body.startsWith('v1 ') ? body : undefined, signal: abort.signal, cache: 'no-store'});
      const value = await response.json();
      return {...value, ok: response.ok, reason: value.command_result?.reason, snapshot: value.mode ? value : undefined};
    } finally { clearTimeout(timer); }
  }
  async close() {}
}
