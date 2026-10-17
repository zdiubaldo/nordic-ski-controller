#!/usr/bin/env python3
"""Exercise the installed, paired app against the virtual controller only.
Start with the app connected as an idle viewer and no other owner.
"""
import json
import socket
import time
import ui

SOCKET = ui.ROOT / '.local/ble-simulator/control.sock'


def control(action='status'):
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(5)
        sock.connect(str(SOCKET))
        sock.sendall((action+'\n').encode())
        data = b''
        while not data.endswith(b'\n'):
            part = sock.recv(8192)
            if not part:
                raise RuntimeError('Simulator closed control socket')
            data += part
        return json.loads(data)


def until(check, label, timeout=20):
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        if check():
            print('PASS:', label, flush=True)
            return
        time.sleep(.3)
    raise AssertionError(label)


def state_matches(**fields):
    value=control()
    assert value['physical_outputs'] is False and value['driver']['type']=='memory'
    return all(value[key]==expected for key,expected in fields.items())


def visible(text):
    return text in ui.labels()


def reconnect():
    ui.adb('shell','am','start','-n','com.zdiubaldo.nordicski.trial/.MainActivity')
    ui.tap('SCAN FOR CONTROLLERS')
    until(lambda: visible('CONNECT AND PAIR'), 'virtual peripheral discovered')
    ui.tap('CONNECT AND PAIR')
    until(lambda: visible('Connected as a viewer. Claim control only when ready.'), 'bonded reconnect', 30)
    until(lambda: state_matches(mode='idle',owner_client_id=0), 'reconnect stays idle and unowned')


def main():
    assert state_matches(mode='idle',owner_client_id=0,connected_clients=1), 'Start connected as an idle viewer'
    ui.tap('ENABLE CONTROLS')
    until(lambda: control()['owner_transport']==2, 'app claims BLE ownership')
    until(lambda: visible('Control: This phone'), 'UI reflects ownership')
    ui.tap('RELEASE CONTROLS')
    until(lambda: state_matches(owner_client_id=0,mode='idle'), 'app releases ownership')
    assert control('observer_start')['mode']=='running'
    until(lambda: visible('Control: Another client'), 'second client ownership visible')
    nodes=list(ui.snapshot().iter('node'))
    claim=[n for n in nodes if n.get('text')=='ENABLE CONTROLS' or n.get('content-desc')=='ENABLE CONTROLS']
    assert claim and all(n.get('enabled')=='false' for n in claim), 'Claim must be disabled for observer'
    ui.tap('STOP')
    until(lambda: state_matches(mode='idle'), 'observer Stop halts memory workout')
    assert control()['owner_client_id'] != 0
    control('observer_release')
    until(lambda: visible('Control: Available'), 'control available after other client release')
    ui.tap('ENABLE CONTROLS')
    until(lambda: visible('Control: This phone'), 'app owns before background test')
    ui.adb('shell','input','keyevent','KEYCODE_HOME')
    until(lambda: state_matches(mode='idle',owner_client_id=0,connected_clients=0,fault=0), 'background stops, releases, disconnects')
    reconnect()
    ui.tap('ENABLE CONTROLS')
    until(lambda: visible('Control: This phone'), 'app owns before forced link loss')
    control('drop')
    until(lambda: state_matches(owner_client_id=0,connected_clients=0), 'link loss clears owner')
    until(lambda: visible('Bluetooth disconnected; reconnect explicitly'), 'app reports link loss')
    print('Final virtual state:', json.dumps({k:control()[k] for k in ('mode','fault','owner_client_id')}), flush=True)


if __name__=='__main__':
    main()
