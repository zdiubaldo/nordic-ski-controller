import React, {useEffect, useRef, useState} from 'react';
import {AppState, Button, ScrollView, StyleSheet, Text, View} from 'react-native';
import BleManager, {type Peripheral} from 'react-native-ble-manager';
import {activateKeepAwakeAsync, deactivateKeepAwake} from 'expo-keep-awake';
import {ControllerClient, type Snapshot} from '@nordic/controller-client';
import {BleTransport, scan} from './ble';

export default function App() {
  const [devices, setDevices] = useState<Peripheral[]>([]);
  const [state, setState] = useState<Snapshot>();
  const [catalog, setCatalog] = useState<unknown>();
  const [message, setMessage] = useState('Scan for your controller to begin.');
  const [busy, setBusy] = useState(false);
  const [connected, setConnected] = useState(false);
  const client = useRef<ControllerClient | undefined>(undefined);
  const transport = useRef<BleTransport | undefined>(undefined);
  const discovery = useRef<{remove(): void} | undefined>(undefined);
  const heartbeatBusy = useRef(false);
  const foreground = useRef(true);
  const statusAt = useRef(0);

  async function disconnect() {
    const c = client.current, t = transport.current;
    if (c && !c.closed && c.ownsControl) {
      try { await c.command('stop'); await c.command('release'); } catch { /* Link close retains the ESP watchdog fallback. */ }
    }
    c?.invalidate(); client.current = undefined; transport.current = undefined;
    setConnected(false); setState(undefined); setCatalog(undefined);
    await t?.close();
  }
  async function run(work: () => Promise<void>) {
    setBusy(true);
    try { await work(); }
    catch (error) {
      setMessage(error instanceof Error ? error.message : 'Bluetooth operation failed');
      if (client.current?.closed) await transport.current?.close();
    } finally { setBusy(false); }
  }
  useEffect(() => {
    const timer = setInterval(() => {
      const c = client.current;
      if (!foreground.current || !c?.token || c.closed || heartbeatBusy.current) return;
      if (Date.now() - statusAt.current > 8000) {
        c.invalidate(); void transport.current?.close(); setMessage('Status stream lost. Reconnect to refresh.'); return;
      }
      heartbeatBusy.current = true;
      void c.command('heartbeat').catch(async () => {
        if (client.current !== c) return;
        setMessage('Heartbeat failed. Reconnect and check controller status.');
        c.invalidate(); await transport.current?.close();
      }).finally(() => { heartbeatBusy.current = false; });
    }, 500);
    const subscription = AppState.addEventListener('change', next => {
      foreground.current = next === 'active';
      if (next !== 'active' && client.current?.token) {
        // Stop accepting work immediately. Background stop is best effort; ESP timeout remains authoritative.
        setConnected(false);
        void disconnect();
      }
    });
    return () => { clearInterval(timer); subscription.remove(); discovery.current?.remove(); void transport.current?.close(); };
  }, []);
  useEffect(() => {
    if (connected && client.current?.ownsControl) void activateKeepAwakeAsync('controller').catch(() => {});
    else void deactivateKeepAwake('controller');
    return () => { void deactivateKeepAwake('controller'); };
  }, [connected, state?.owner_client_id]);

  async function find() {
    discovery.current?.remove(); setDevices([]);
    discovery.current = await scan(device => setDevices(previous => [...previous.filter(p => p.id !== device.id), device]));
    setMessage('Scanning for 8 seconds. Select your controller below.');
  }
  async function connect(device: Peripheral) {
    await BleManager.stopScan(); discovery.current?.remove();
    await disconnect();
    setMessage('Connecting. For first pairing, open the USB pairing window and enter its code in the system prompt.');
    const t = new BleTransport(device.id), c = new ControllerClient(t);
    transport.current = t; client.current = c;
    statusAt.current = Date.now();
    c.onState = value => {if (client.current === c) {statusAt.current = Date.now(); setState(value);}};
    t.onState = value => c.accept(value);
    t.onCatalog = value => {if (transport.current === t) setCatalog(value);};
    t.onDisconnect = reason => { c.invalidate(); if (transport.current === t) {setConnected(false); setState(undefined); setMessage(reason);} };
    try {
      await t.open();
      if (!foreground.current) throw new Error('Return to the app and reconnect');
      await c.register();
      if (!foreground.current) throw new Error('Return to the app and reconnect');
      statusAt.current = Date.now();
      const status = await t.request('status');
      if (!status.ok) throw new Error(status.reason || 'Status unavailable');
      const workouts = await t.request('workouts');
      if (!workouts.ok) throw new Error(workouts.reason || 'Catalog unavailable');
      setConnected(true); setMessage('Connected as a viewer. Claim control only when ready.');
    } catch (error) { await t.close(); throw error; }
  }
  async function action(name: string) {
    await client.current!.command(name);
    setMessage(name === 'claim' ? 'Controls enabled. This prototype does not start motion.' : name === 'stop' ? 'Stop accepted.' : 'Control released.');
  }
  const owns = connected && client.current?.ownsControl;
  return <ScrollView contentContainerStyle={styles.page}>
    <Text style={styles.title}>Nordic Ski Trial</Text>
    <Text style={styles.subtitle}>Bluetooth connection prototype</Text>
    <Text accessibilityLiveRegion="polite" style={styles.message}>{message}</Text>
    <Text>First pairing needs the temporary code from the controller’s USB pairing window. Existing bonds reconnect without another code.</Text>
    <View style={styles.row}><Button title="Scan for controllers" disabled={busy || connected} onPress={() => void run(find)}/></View>
    {!connected && devices.map(device => <View style={styles.card} key={device.id}>
      <Text>{device.name || 'Nordic Ski'} · {device.id}</Text>
      <Button title="Connect and pair" disabled={busy} onPress={() => void run(() => connect(device))}/>
    </View>)}
    {connected && <>
      <View style={styles.card}>
        <Text style={styles.heading}>Controller status</Text>
        <Text>Mode: {state?.mode ?? 'Waiting for status'}</Text>
        <Text>Fault: {state?.fault ?? '—'}</Text>
        <Text>Control: {owns ? 'This phone' : state?.owner_client_id ? 'Another client' : 'Available'}</Text>
        <Text>Requested speed: {state?.requested_speed_mps ?? '—'} m/s</Text>
        <Text>Requested incline: {state?.requested_grade_percent ?? '—'}%</Text>
        <Text>These are requested settings; motion is not measured.</Text>
      </View>
      <View style={styles.row}><Button title="Enable controls" disabled={busy || !state || !!state.owner_client_id} onPress={() => void run(() => action('claim'))}/></View>
      <View style={styles.row}><Button title="STOP" color="#b42318" onPress={() => void run(() => action('stop'))}/></View>
      <View style={styles.row}><Button title="Release controls" disabled={busy || !owns} onPress={() => void run(() => action('release'))}/></View>
      <View style={styles.row}><Button title="Disconnect" disabled={busy} onPress={() => void run(disconnect)}/></View>
      <Text style={styles.heading}>Workout catalog</Text>
      <Text selectable>{catalog ? JSON.stringify(catalog, null, 2) : 'Loading catalog…'}</Text>
    </>}
    <Text style={styles.footer}>Connection milestone: scan, pair, status, catalog, ownership, and Stop. Start and workout controls come after phone validation. Leaving the app closes this session; reconnecting never starts it.</Text>
  </ScrollView>;
}
const styles = StyleSheet.create({
  page: {padding: 24, paddingTop: 60, paddingBottom: 48, backgroundColor: '#f5f7fa', flexGrow: 1},
  title: {fontSize: 30, fontWeight: '700', color: '#143548'}, subtitle: {fontSize: 18, marginBottom: 16},
  heading: {fontSize: 20, fontWeight: '600', marginBottom: 8}, message: {padding: 16, backgroundColor: '#e0edf4', marginBottom: 16},
  row: {marginVertical: 8}, card: {padding: 16, borderRadius: 12, backgroundColor: 'white', marginVertical: 12, gap: 8},
  footer: {marginTop: 24, color: '#475467', lineHeight: 22},
});
