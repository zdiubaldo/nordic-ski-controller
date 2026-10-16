#!/usr/bin/env python3
"""Exercise the existing HTTP controls. Never changes Wi-Fi or flashes firmware."""
import argparse
import datetime
import json
from pathlib import Path
import signal
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


class RelayTest:
    def __init__(self, url):
        self.url = url.rstrip('/')
        self.token = None
        self.sequence = 0
        self.last_command = 0
        self.events = []
        self.mapping = {}
        self.epoch = 0
        # Always use the selected device directly, never an environment proxy.
        self.http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def log(self, message):
        print(message, flush=True)
        self.events.append({'time': datetime.datetime.now().isoformat(), 'message': message})

    def request(self, path, body=None):
        request = urllib.request.Request(self.url + path,
            data=None if body is None else body.encode(),
            headers={'X-Nordic-Test': '1'})
        try:
            with self.http.open(request, timeout=1.5) as response:
                return json.load(response)
        except (urllib.error.URLError, TimeoutError) as error:
            raise RuntimeError(f'Controller request failed: {error}') from error

    def status(self):
        s = self.request('/api/status')
        r = s.get('relays', {})
        if not r.get('enabled') or not r.get('ready') or r.get('fault') or not r.get('known'):
            raise RuntimeError('Relay hardware is disabled, faulted, or its output state is unknown.')
        if not 0 <= s.get('sample_age_ms', 99999) <= 1000:
            raise RuntimeError('Controller status is stale.')
        return s

    def claim(self):
        reply = self.request('/api/test/claim', '')
        self.token = reply['token']
        self.epoch = reply['control_epoch']
        self.sequence = 0
        self.last_command = time.monotonic()

    def command(self, action, speed=0, grade=0):
        self.sequence += 1
        result = self.request('/api/test/command',
            f'v1 {self.token} {self.sequence} {self.epoch} {action} {speed} {grade}')
        self.last_command = time.monotonic()
        self.epoch = result['control_epoch']
        return result

    def wait_mask(self, mask, timeout=2, heartbeat=True, mode=None):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            if heartbeat and time.monotonic() - self.last_command > .6:
                self.command('heartbeat')
            last = self.status()
            if last['relays']['mask'] == mask and (mode is None or last['mode'] == mode):
                return last
            time.sleep(.05)
        raise RuntimeError(f'Expected output mask {mask} / mode {mode or "any"}; '
                           f'last report: mask {last["relays"]["mask"]}, mode {last["mode"]}.')

    @staticmethod
    def bit(channel):
        return 0 if channel == 0 else 1 << (channel - 1)

    def run(self):
        s = self.status()
        if s.get('local_preview') or not s.get('physical_outputs') or s.get('commissioned'):
            raise RuntimeError('Requires the real, uncommissioned relay-bench firmware.')
        if s.get('session_active'):
            raise RuntimeError('Another page owns controls. Close controller tabs, wait 4 seconds, then rerun.')
        if s['mode'] == 'running' or s['relays']['mask'] != 0:
            raise RuntimeError('Controller must be stopped with all outputs off before testing.')
        self.mapping = s['relays']
        keys = ['start', 'stop', 'speed_up', 'speed_down', 'incline_up', 'incline_down']
        channels = [self.mapping.get(k) for k in keys]
        if any(type(n) is not int or not 0 <= n <= 8 for n in channels):
            raise RuntimeError('Invalid channel mapping.')
        assigned = [n for n in channels if n]
        if len(set(assigned)) != len(assigned):
            raise RuntimeError('Duplicate channel mapping.')
        pulse = self.mapping.get('pulse_ms', 0)
        if not 100 <= pulse <= 2000:
            raise RuntimeError('Invalid pulse duration.')
        self.log('Mapping: ' + ', '.join(f'{k}=CH{self.mapping[k]}' for k in keys))
        self.claim()
        if s['mode'] == 'fault':
            self.command('reset')
        start_mask = self.bit(self.mapping['start'])
        stop_mask = self.bit(self.mapping['stop'])
        self.log('1. START — listen for the Start relay.')
        self.command('start')
        self.wait_mask(start_mask, mode='running')
        self.wait_mask(0, timeout=pulse / 1000 + 1, mode='running')
        self.log('PASS: Start output acknowledged.')
        time.sleep(.15)
        for label, key, speed, grade in [
            ('Speed increase', 'speed_up', 1, 0),
            ('Speed decrease', 'speed_down', 0, 0),
            ('Incline increase', 'incline_up', 0, 1),
            ('Incline decrease', 'incline_down', 0, 0),
        ]:
            self.log(f'{label} — CH{self.mapping[key]}, {pulse} ms pulse.')
            self.command(key)
            if self.mapping[key]:
                self.wait_mask(self.bit(self.mapping[key]))
                self.wait_mask(0, timeout=pulse / 1000 + 1)
                self.log(f'PASS: {label} pulse and release acknowledged.')
                time.sleep(.15)
            else:
                self.wait_mask(0)
                self.log(f'SKIP: {label} output is disabled in the mapping.')
        self.log('STOP — Stop relay should pulse, then release.')
        self.command('stop')
        self.wait_mask(stop_mask, mode='idle')
        self.wait_mask(0, timeout=pulse / 1000 + 1, mode='idle')
        self.log('PASS: Stop output acknowledged.')
        time.sleep(.15)
        self.log('Timeout test — starting Run, then withholding commands for about 3 seconds.')
        self.command('start')
        self.wait_mask(start_mask, mode='running')
        self.wait_mask(0, timeout=pulse / 1000 + 1, mode='running')
        self.wait_mask(stop_mask, timeout=5, heartbeat=False, mode='fault')
        self.wait_mask(0, timeout=pulse / 1000 + 1, heartbeat=False, mode='fault')
        self.log('PASS: Command timeout faults the session, pulses Stop, and releases outputs.')
        s = self.status()
        if s['session_active']:
            raise RuntimeError('Timed-out owner was not released.')
        self.token = None
        self.claim()
        self.command('reset')
        self.wait_mask(0, mode='idle')
        self.log('PASS: Reset leaves outputs off; no automatic restart.')

    def cleanup(self):
        if not self.token:
            return True
        try:
            self.command('stop')
        except Exception:
            pass  # A lost/expired lease cannot issue Stop; verify watchdog shutdown below.
        try:
            self.wait_mask(0, timeout=5, heartbeat=False)
            self.log('FINISHED: all output latches verified off.')
            return True
        except Exception as error:
            self.log(f'OFF NOT VERIFIED: {error}. Disconnect USB power from the unloaded board.')
            return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.4.1', help='Controller address')
    args = parser.parse_args()
    u = urllib.parse.urlsplit(args.url)
    if u.scheme != 'http' or not u.hostname or u.username or u.password or u.query or u.fragment or u.path not in ('', '/'):
        parser.error('Use a plain HTTP controller address, without credentials or path.')
    test = RelayTest(args.url)
    test.log('UNLOADED RELAY TEST — keep all relay terminals disconnected. Ctrl+C stops the test.')
    test.log('Uses your current network. No Wi-Fi changes, firmware updates, or internet required.')
    passed = False
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    try:
        test.run()
        passed = True
    except KeyboardInterrupt:
        test.log('Interrupted; requesting Stop.')
    except Exception as error:
        test.log(f'FAIL: {error}')
    finally:
        # Allow cleanup to finish even if Ctrl+C is pressed again.
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        passed = test.cleanup() and passed
        report_dir = Path(__file__).resolve().parent.parent / '.local' / 'relay-reports'
        report_dir.mkdir(parents=True, exist_ok=True)
        report = report_dir / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.json')
        report.write_text(json.dumps({'passed': passed, 'url': test.url, 'events': test.events,
            'scope': 'HTTP controls and acknowledged output latches; physical clicks/contacts require observation'}, indent=2))
        print(f'Report: {report}', flush=True)
        print('PASS' if passed else 'FAIL / INCOMPLETE', flush=True)
        print('Results verify output commands, not physical contacts. Wait 4 seconds before taking web control.')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
