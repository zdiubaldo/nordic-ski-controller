import {PermissionsAndroid, Platform} from 'react-native';
import BleManager, {type Peripheral} from 'react-native-ble-manager';
import {frames, Receiver, UUID, jsonBytes, type Reply, type Snapshot, type Transport} from '@nordic/controller-client';

let initialization: Promise<void> | undefined;
export async function initialize() {
  if (Platform.OS === 'android') {
    const permissions = Number(Platform.Version) >= 31
      ? [PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN, PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT]
      : [PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION];
    const granted = await PermissionsAndroid.requestMultiple(permissions);
    if (permissions.some(p => granted[p] !== PermissionsAndroid.RESULTS.GRANTED)) throw new Error('Bluetooth permissions are required');
  }
  initialization ??= BleManager.start({showAlert: false});
  await initialization;
  if (await BleManager.checkState() !== 'on') throw new Error('Turn on Bluetooth and try again');
}
export async function scan(onDevice: (device: Peripheral) => void) {
  await initialize();
  const listener = BleManager.onDiscoverPeripheral(onDevice);
  try { await BleManager.scan({serviceUUIDs: [UUID[0]], seconds: 8, allowDuplicates: false}); }
  catch (error) { listener.remove(); throw error; }
  return listener;
}
export class BleTransport implements Transport {
  id: string;
  receiver = new Receiver();
  nextId = 0;
  closed = false;
  bootId?: number;
  pending = new Map<number, {resolve: (reply: Reply) => void; reject: (error: Error) => void; timer: ReturnType<typeof setTimeout>}>();
  listeners: {remove(): void}[] = [];
  startedStreams = new Set<number>();
  writes: Promise<unknown> = Promise.resolve();
  onState: (state: Snapshot) => void = () => {};
  onCatalog: (catalog: unknown) => void = () => {};
  onDisconnect: (reason: string) => void = () => {};
  constructor(id: string) { this.id = id; }
  async open() {
    // iOS connect can wait indefinitely; cancel the native operation on timeout.
    let timer: ReturnType<typeof setTimeout>;
    const expired = new Promise<never>((_resolve, reject) => {
      timer = setTimeout(() => { reject(new Error('Connection or pairing timed out')); void this.close(); }, 120000);
    });
    try {
      await Promise.race([expired, (async () => {
      await BleManager.connect(this.id);
      if (this.closed) throw new Error('Connection timed out');
      await BleManager.retrieveServices(this.id);
      const info = jsonBytes(await BleManager.read(this.id, UUID[0], UUID[1]));
      if (info.protocol_version !== 1 || info.framing !== 1 || !Number.isInteger(info.boot_id)) throw new Error('Unsupported controller protocol');
      this.bootId = info.boot_id;
      if (Platform.OS === 'android') await BleManager.createBond(this.id);
      if (this.closed) throw new Error('Connection cancelled');
      // Android starts at MTU 23. Negotiate before subscriptions so the ESP can
      // deliver complete status snapshots promptly instead of 12-byte fragments.
      if (Platform.OS === 'android') await BleManager.requestMTU(this.id, 247);
      if (this.closed) throw new Error('Connection cancelled');
      this.listeners.push(BleManager.onDisconnectPeripheral(event => {
        if (event.peripheral === this.id) this.fail('Bluetooth disconnected; reconnect explicitly');
      }));
      this.listeners.push(BleManager.onDidUpdateValueForCharacteristic(event => {
        if (event.peripheral !== this.id || event.service.toLowerCase() !== UUID[0]) return;
        const index = UUID.indexOf(event.characteristic.toLowerCase());
        if (index < 3 || index > 5) return;
        try {
          // Bonded CCCDs can resume a stream before Android finishes subscribing.
          // Join at its next complete message, then enforce strict framing.
          const offset = event.value[6] | event.value[7] << 8;
          if (!this.startedStreams.has(index)) {
            if (offset !== 0) {
              console.info(`[NordicBLE] waiting for message boundary: stream=${index - 1} offset=${offset}`);
              return;
            }
            this.startedStreams.add(index);
          }
          const message = this.receiver.feed(event.value, index - 1);
          if (!message) return;
          if (message.kind === 2) {
            const pending = this.pending.get(message.id);
            if (pending) { clearTimeout(pending.timer); this.pending.delete(message.id); pending.resolve(message.value); }
          } else if (message.kind === 3) {
            if (message.value.boot_id !== this.bootId) throw new Error('Controller restarted');
            this.onState(message.value);
          }
          else this.onCatalog(message.value);
        } catch (error) {
          const reason = error instanceof Error ? error.message : String(error);
          console.warn(`[NordicBLE] notification rejected: ${reason}; stream=${index - 1} bytes=${event.value.length} offset=${event.value[6] | event.value[7] << 8}`);
          this.fail(`Invalid Bluetooth data: ${reason}`); void BleManager.disconnect(this.id).catch(() => {});
        }
      }));
      for (const characteristic of UUID.slice(3)) {
        if (this.closed) throw new Error('Connection cancelled');
        await BleManager.startNotification(this.id, UUID[0], characteristic);
      }
      })()]);
    } catch (error) { await this.close(); throw error; }
    finally { clearTimeout(timer!); }
  }
  request(body: string): Promise<Reply> {
    if (this.closed) return Promise.reject(new Error('Bluetooth disconnected'));
    const id = this.nextId = this.nextId % 65535 + 1;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id); reject(new Error('Response timed out; outcome unknown. Reconnect and check status.'));
      }, body === 'connect' ? 120000 : 10000);
      this.pending.set(id, {resolve, reject, timer});
      const write = async () => {
        if (this.closed) throw new Error('Bluetooth disconnected');
        // ATT 17 explicitly rejects the fragment before the ESP queues a command.
        // Restart its frame assembly only for that response, never ambiguous errors.
        for (let attempt=0; attempt<4; attempt++) {
          try {
            for (const packet of frames(body, id)) {
              if (this.closed || !this.pending.has(id)) throw new Error('Request expired');
              await BleManager.write(this.id, UUID[0], UUID[2], packet, 20);
            }
            return;
          } catch (error) {
            const reason = error instanceof Error ? error.message : String(error);
            console.warn(`[NordicBLE] GATT write rejected: ${reason}`);
            if (attempt===3 || !/^Error writing [a-f0-9-]+ status=17$/i.test(reason)) throw error;
            await new Promise(resolve=>setTimeout(resolve,50*(attempt+1)));
          }
        }
      };
      this.writes = this.writes.then(write).catch((error) => {
        const pending = this.pending.get(id);
        const reason = error instanceof Error ? error.message : String(error);
        if (pending) { clearTimeout(pending.timer); this.pending.delete(id); pending.reject(new Error(`Bluetooth write failed: ${reason}`)); }
      });
    });
  }
  fail(reason: string) {
    if (this.closed) return;
    this.closed = true;
    for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(new Error(reason)); }
    this.pending.clear(); this.receiver.pending.clear();
    this.listeners.forEach(listener => listener.remove()); this.listeners = [];
    this.onDisconnect(reason);
  }
  async close() {
    this.fail('Disconnected');
    await BleManager.disconnect(this.id).catch(() => {});
  }
}
