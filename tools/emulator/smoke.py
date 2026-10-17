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


def reconnect(mode='idle'):
    ui.adb('shell','am','start','-n','com.zdiubaldo.nordicski.trial/.MainActivity')
    ui.tap('SCAN FOR CONTROLLERS')
    until(lambda: visible('CONNECT AND PAIR'), 'virtual peripheral discovered')
    ui.tap('CONNECT AND PAIR')
    until(lambda: visible('Enable controls'), 'shared website connected', 30)
    until(lambda: state_matches(mode=mode,owner_client_id=0), 'reconnect does not claim or start')


def scroll(down):
    # Pixel 4 AVD viewport established by UIAutomator (1080 x 2280).
    ui.adb('shell','input','swipe','540','1750' if down else '600','540','800' if down else '1800','500')


def main():
    assert state_matches(mode='idle',owner_client_id=0,connected_clients=1), 'Start connected as an idle viewer'
    ui.tap('Enable controls')
    until(lambda: control()['owner_transport']==2, 'website claims BLE ownership')
    ui.tap('▶ Start')
    until(lambda: state_matches(mode='running'), 'website Start reaches controller')
    scroll(True)
    ui.tap('Increase speed')
    until(lambda: control()['requested_speed_mps']>0, 'speed adjustment accepted')
    ui.tap('Increase incline')
    until(lambda: control()['requested_grade_percent']>0, 'incline adjustment accepted')
    ui.screenshot('shared-controls')
    ui.tap('■ Stop')
    until(lambda: state_matches(mode='idle',requested_speed_mps=0,requested_grade_percent=0), 'Stop clears requested settings')
    scroll(False)
    ui.tap('Steady')
    ui.tap('▶ Start')
    until(lambda: control()['workout']['active'], 'preset starts through shared page')
    ui.tap('■ Stop')
    until(lambda: state_matches(mode='idle'), 'preset Stop accepted')
    ui.adb('shell','input','keyevent','KEYCODE_HOME')
    until(lambda: state_matches(mode='idle',owner_client_id=0,connected_clients=0,fault=0), 'background stops, releases, disconnects')
    reconnect()
    assert control('observer_start')['mode']=='running'
    until(lambda: visible('In use on another screen'), 'another client ownership visible')
    nodes=[n for n in ui.snapshot().iter('node') if n.get('text')=='In use on another screen' or n.get('content-desc')=='In use on another screen']
    assert nodes and all(n.get('enabled')=='false' for n in nodes)
    ui.tap('■ Stop')
    until(lambda: state_matches(mode='idle'), 'observer Stop halts memory workout')
    control('observer_release')
    until(lambda: visible('Enable controls'), 'control available after release')
    ui.tap('Enable controls')
    until(lambda: control()['owner_transport']==2, 'app owns before forced loss')
    control('drop')
    until(lambda: state_matches(owner_client_id=0,connected_clients=0,fault=2), 'link loss faults and clears owner')
    until(lambda: visible('Connect your controller'), 'app returns to connection screen')
    print('Shared-page smoke test passed. Reset virtual simulator before next run.',flush=True)


if __name__=='__main__':
    main()
