import React, {useEffect, useRef, useState} from 'react';
import {AppState, Button, ScrollView, StyleSheet, Text, View, StatusBar} from 'react-native';
import {SafeAreaProvider, SafeAreaView} from 'react-native-safe-area-context';
import {WebView} from 'react-native-webview';
import {controllerPage} from './generated/controller-page';
import {WebBridge, type WebRequest} from './web-bridge';
import BleManager, {type Peripheral} from 'react-native-ble-manager';
import {activateKeepAwakeAsync, deactivateKeepAwake} from 'expo-keep-awake';
import {ControllerClient, type Snapshot} from '@nordic/controller-client';
import {BleTransport, scan} from './ble';

const pageSource = {html: controllerPage, baseUrl: 'https://nordic.invalid/'};

export default function App() {
  const [devices, setDevices] = useState<Peripheral[]>([]);
  const [state, setState] = useState<Snapshot>();
  const [message, setMessage] = useState('Scan for your controller to begin.');
  const [busy, setBusy] = useState(false);
  const [connected, setConnected] = useState(false);
  const client = useRef<ControllerClient | undefined>(undefined);
  const transport = useRef<BleTransport | undefined>(undefined);
  const discovery = useRef<{remove(): void} | undefined>(undefined);
  const web = useRef<WebView>(null);
  const bridge = useRef<WebBridge | undefined>(undefined);
  const foreground = useRef(true);
  const statusAt = useRef(0);
  const pageActive = useRef(false);
  const bootstrapBusy = useRef(false);

  async function disconnect() {
    const c = client.current, t = transport.current;
    // Detach immediately: no new page work can enter a closing session.
    client.current = undefined; transport.current = undefined; bridge.current = undefined; pageActive.current = false;
    setConnected(false); setState(undefined);
    setMessage('Disconnected. Select your controller to reconnect.');
    if (c && !c.closed && c.ownsControl) {
      try { await c.command('stop'); await c.command('release'); } catch { /* ESP watchdog remains the fallback. */ }
    }
    c?.invalidate();
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
      if (!foreground.current || !c?.token || c.closed) return;
      if (Date.now() - statusAt.current > 8000) {
        c.invalidate(); void transport.current?.close(); setMessage('Connection lost. Connect again to check the controller.');
      }
      // Keep only the viewer registration alive while the WebView starts.
      // After its session handshake, the page must sustain its own heartbeats.
      if (!pageActive.current && !c.ownsControl && !bootstrapBusy.current) {
        bootstrapBusy.current = true;
        void c.command('heartbeat').catch(async () => {
          if (client.current === c) { c.invalidate(); await transport.current?.close(); }
        }).finally(() => { bootstrapBusy.current = false; });
      }
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
    let receivedCatalog: unknown;
    t.onCatalog = value => {if (transport.current === t) {receivedCatalog=value;}};
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
      const deadline = Date.now()+10000;
      while (!receivedCatalog || !c.state) {
        if (c.closed || Date.now()>deadline) throw new Error('Controller status unavailable. Connect again.');
        await new Promise(resolve=>setTimeout(resolve,30));
      }
      bridge.current = new WebBridge(c, receivedCatalog);
      setConnected(true); setMessage('Connected');
    } catch (error) { await t.close(); throw error; }
  }
  async function pageMessage(data: string) {
    const active = bridge.current;
    if (!active || !foreground.current) return;
    let request: WebRequest;
    try {
      const message = JSON.parse(data);
      if (message.type === 'cancel') { await disconnect(); setMessage('Request timed out. Connect again to check the controller.'); return; }
      request = message;
      const reply = await active.handle(request);
      if (bridge.current === active && request.path === '/api/session') pageActive.current = true;
      if (bridge.current === active) web.current?.injectJavaScript(`window.__nordicReply(${JSON.stringify(reply).replace(/</g,'\\u003c')});true;`);
    } catch (error) {
      if (bridge.current !== active) return;
      await disconnect();
      setMessage(error instanceof Error ? error.message : 'Connection lost. Connect again.');
    }
  }
  return <SafeAreaProvider><SafeAreaView style={styles.screen}>
    <StatusBar barStyle="light-content" backgroundColor="#0c1824"/>
    {connected ? <>
      <View style={styles.connectionBar}><Text style={styles.connected}>Bluetooth connected</Text><Button title="Disconnect" color="#16ade5" onPress={() => void run(disconnect)}/></View>
      <WebView ref={web} style={styles.web} source={pageSource}
        originWhitelist={['https://nordic.invalid','about:blank']}
        onShouldStartLoadWithRequest={request => request.url==='about:blank'||request.url==='https://nordic.invalid/'}
        onMessage={event => void pageMessage(event.nativeEvent.data)}
        javaScriptEnabled domStorageEnabled={false} allowFileAccess={false} allowUniversalAccessFromFileURLs={false}
        mixedContentMode="never" setSupportMultipleWindows={false}
        onError={() => void disconnect()} onRenderProcessGone={() => void disconnect()}
        onContentProcessDidTerminate={() => void disconnect()}/>
    </> : <ScrollView contentContainerStyle={styles.page}>
      <Text style={styles.eyebrow}>TEAM SUMMIT NORDIC</Text>
      <Text style={styles.title}>Connect your controller</Text>
      <Text style={styles.subtitle}>Your pace. Your climb.</Text>
      <Text accessibilityLiveRegion="polite" style={styles.message}>{message}</Text>
      <View style={styles.row}><Button title="Scan for controllers" color="#16ade5" disabled={busy} onPress={() => void run(find)}/></View>
      {devices.map(device => <View style={styles.card} key={device.id}>
        <Text style={styles.device}>{device.name || 'Nordic Ski'}</Text>
        <Button title="Connect and pair" color="#16ade5" disabled={busy} onPress={() => void run(() => connect(device))}/>
      </View>)}
      <Text style={styles.footer}>For first pairing, open the controller’s USB pairing window and enter its code when your phone asks. Previously paired controllers reconnect without a code.</Text>
    </ScrollView>}
  </SafeAreaView></SafeAreaProvider>;
}
const styles = StyleSheet.create({
  screen: {flex:1,backgroundColor:'#0c1824'}, web:{flex:1,backgroundColor:'#0c1824'},
  connectionBar:{flexDirection:'row',justifyContent:'space-between',alignItems:'center',paddingHorizontal:16,borderBottomWidth:1,borderBottomColor:'#2c4052'},
  connected:{color:'#9eb2c4',fontSize:12},
  page: {padding:24,paddingTop:40,backgroundColor:'#0c1824',flexGrow:1},
  eyebrow:{color:'#16ade5',fontSize:12,fontWeight:'700',letterSpacing:2,marginBottom:16},
  title:{fontSize:30,fontWeight:'700',color:'#f5f8fb'}, subtitle:{fontSize:18,color:'#9eb2c4',marginTop:8,marginBottom:24},
  message:{padding:16,backgroundColor:'#142536',borderRadius:12,color:'#d9eaf5',marginBottom:16},
  row:{marginVertical:8},card:{padding:16,borderRadius:12,backgroundColor:'#142536',marginVertical:12,gap:16},
  device:{color:'#f5f8fb',fontSize:18},footer:{marginTop:24,color:'#9eb2c4',lineHeight:22},
});
