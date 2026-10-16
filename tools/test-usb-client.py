#!/usr/bin/env python3
"""Mock an authorized client over USB. Run only with unloaded relay terminals."""
import argparse
import json
import time
from pathlib import Path
import serial
import importlib.util
_spec = importlib.util.spec_from_file_location("relay_tests", Path(__file__).with_name("test-relays.py"))
_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_module)
RelayTest = _module.RelayTest


class USBClient(RelayTest):
    def __init__(self, port):
        super().__init__('usb:' + port)
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=.15, write_timeout=1)
        self.serial.dtr = True
        self.serial.rts = True
        self.serial.port = port
        self.serial.open()
        self.request_id = 0
        self.serial_log = []

    def exchange(self, body):
        self.request_id += 1
        request_id = self.request_id
        self.serial.write(f'{request_id} {body}\n'.encode())
        deadline = time.monotonic() + 3
        prefix = f'@nordic {request_id} '
        while time.monotonic() < deadline:
            line = self.serial.readline().decode(errors='replace').strip()
            if line.startswith(prefix):
                return json.loads(line[len(prefix):])
            if line:
                self.serial_log.append(line)
        raise RuntimeError(f'USB reply timed out for request {request_id}')

    def request(self, path, body=None):
        wire = 'status' if path == '/api/status' else 'connect' if path == '/api/session' else body
        result = self.exchange(wire)
        if not result.get('ok'):
            raise RuntimeError('USB rejected: ' + str(result.get('error') or result.get('command_result')))
        return result

    def claim(self):
        result = self.request('/api/session', '')
        self.token = result['token']
        self.epoch = result['control_epoch']
        self.sequence = 0
        self.command('claim')

    def protocol_checks(self):
        s = self.status()
        if s['session_active'] or s['mode'] == 'running' or s['relays']['mask']:
            raise RuntimeError('Close the controlling webpage and leave the board stopped before testing.')
        assert self.exchange('bad command')['error'] == 'malformed_command'
        # The adapter discards an oversized line completely, then accepts a fresh frame.
        self.serial.write(b'1 ' + b'x' * 220 + b'\n')
        assert self.status()['relays']['mask'] == 0
        self.claim()
        assert self.status()['owner_transport'] == 4
        self.command('heartbeat')
        replay = self.exchange(f'v1 {self.token} {self.sequence} {self.epoch} heartbeat 0 0')
        assert replay['command_result']['reason'] == 'replayed_sequence'
        self.command('release')
        self.command('disconnect')
        self.token = None
        self.log('PASS: USB framing, malformed/oversized input recovery, ownership, replay rejection, release.')

    def restored_controls(self):
        s = self.status()
        if s['session_active'] or s['mode'] == 'running' or s['relays']['mask']:
            raise RuntimeError('Release webpage control and stop before the USB restoration test.')
        assert s['capabilities']['numeric_targets'] and s['capabilities']['workouts']
        catalog = self.exchange('workouts')['workouts']
        assert any(w['id'] == 'hills' for w in catalog)
        self.mapping = s['relays']
        self.claim()
        if s['mode'] == 'fault':
            self.command('reset')
        self.command('start')
        self.wait_mask(self.bit(self.mapping['start']), mode='running')
        self.wait_mask(0)
        time.sleep(.15)
        for speed, grade, key in [(1, 0, 'speed_up'), (.5, 0, 'speed_down'), (.5, 2, 'incline_up'), (.5, 1, 'incline_down')]:
            result = self.command('targets', speed, grade)
            assert result['requested_speed_mps'] == speed and result['requested_grade_percent'] == grade
            self.wait_mask(self.bit(self.mapping[key]))
            self.wait_mask(0)
            time.sleep(.15)
            self.log(f'PASS: numeric targets {speed} m/s, {grade}% produce {key} and release.')
        self.command('stop')
        self.wait_mask(self.bit(self.mapping['stop']), mode='idle')
        self.wait_mask(0)
        time.sleep(.15)
        result = self.command('w:hills', 1, 100)
        assert result['workout']['active'] and result['workout']['duration_ms'] == 60000
        self.log('Workout started: checking an entire one-minute preset over USB.')
        override = False
        transition = False
        completion = False
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            result = self.command('heartbeat')
            w = result['workout']
            if not override and w['elapsed_ms'] >= 1800:
                changed = self.command('targets', 2, 4)
                assert changed['workout']['overridden']
                override = True
                self.log('PASS: numeric manual override during workout.')
            if override and not transition and w['segment'] >= 1:
                assert not w['overridden'] and result['requested_grade_percent'] == 1
                transition = True
                self.log('PASS: ESP advances workout and replaces override at next segment.')
            if w['complete']:
                assert result['mode'] == 'idle' and result['requested_speed_mps'] == 0
                completion = True
                break
            time.sleep(.2)
        assert completion and transition
        time.sleep(.8)
        assert self.status()['relays']['mask'] == 0
        self.log('PASS: full workout completes, stops, and leaves all outputs off.')
        self.command('w:hills', 1, 100)
        self.command('cancel_workout')
        self.wait_mask(self.bit(self.mapping['stop']), mode='idle')
        self.wait_mask(0)
        self.log('PASS: shared cancel-workout command stops the session.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--status-only', action='store_true')
    parser.add_argument('--restored-controls', action='store_true', help='Test numeric targets and an entire one-minute workout on unloaded relays')
    parser.add_argument('--check-reconnect', action='store_true', help='Read-only boot continuity check across closing and reopening USB')
    args = parser.parse_args()
    client = USBClient(args.port)
    passed = False
    try:
        if args.check_reconnect:
            before = client.status()
            client.serial.close()
            time.sleep(1)
            client.serial.open()
            after = client.status()
            assert before['boot_id'] == after['boot_id'], 'Serial reopen reset the ESP'
            assert after['uptime_ms'] >= before['uptime_ms']
            client.log('PASS: closing and reopening USB preserves ESP boot; no reset.')
        elif args.restored_controls:
            client.restored_controls()
        elif args.status_only:
            print(json.dumps(client.status(), indent=2))
        else:
            client.log('USB mock client: physical unloaded relay tests; Wi-Fi and webpage unchanged.')
            client.protocol_checks()
            client.run()
        passed = True
    except (Exception, KeyboardInterrupt) as error:
        client.log(f'FAIL: {error}')
    finally:
        if client.token:
            passed = client.cleanup() and passed
            try:
                client.command('release')
                client.command('disconnect')
                time.sleep(.8)
                final = client.status()
                assert final['mode'] == 'idle' and final['relays']['mask'] == 0
                assert not final['session_active']
                client.log('PASS: final state idle, ownership released, all output latches off.')
            except Exception as error:
                client.log(f'Lease cleanup: {error}')
                passed = False
        client.serial.close()
        name = 'usb-restored-controls-report.json' if args.restored_controls else 'usb-reconnect-report.json' if args.check_reconnect else 'usb-status-report.json' if args.status_only else 'usb-client-report.json'
        report = Path(__file__).resolve().parent.parent / '.local' / name
        report.write_text(json.dumps({'passed': passed, 'events': client.events,
            'serial_log': client.serial_log,
            'scope': 'USB commands and acknowledged relay output latches, not physical contacts or HTTP'}, indent=2))
        print(f'Report: {report}\n' + ('PASS' if passed else 'FAIL / INCOMPLETE'))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
