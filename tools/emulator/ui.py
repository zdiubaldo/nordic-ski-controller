#!/usr/bin/env python3
"""Small UIAutomator helper, restricted to the Nordic virtual device."""
import argparse
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
ADB = ROOT / '.local/android-tools/sdk/platform-tools/adb'
SERIAL = 'emulator-5554'


def adb(*args):
    return subprocess.check_output([str(ADB), '-s', SERIAL, *args], timeout=25)


def snapshot():
    adb('shell', 'uiautomator', 'dump', '/sdcard/nordic-ui.xml')
    return ET.fromstring(adb('shell', 'cat', '/sdcard/nordic-ui.xml'))


def labels():
    return [n.get('text') or n.get('content-desc') for n in snapshot().iter('node') if n.get('text') or n.get('content-desc')]


def tap(label):
    nodes = [n for n in snapshot().iter('node') if label.casefold() in [(n.get('text') or '').casefold(), (n.get('content-desc') or '').casefold()] and n.get('enabled') == 'true']
    nodes = [n for n in nodes if (lambda b: b[2]>b[0] and b[3]>b[1])(list(map(int,re.findall(r'\d+',n.get('bounds')))))]
    if not nodes:
        raise RuntimeError('Enabled UI label not found: ' + label)
    nodes.sort(key=lambda n: n.get('clickable') != 'true')
    x1,y1,x2,y2 = map(int, re.findall(r'\d+', nodes[0].get('bounds')))
    adb('shell', 'input', 'tap', str((x1+x2)//2), str((y1+y2)//2))


def screenshot(name):
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', name):
        raise ValueError('Invalid screenshot name')
    folder = ROOT / '.local/emulator-tests'; folder.mkdir(exist_ok=True)
    path = folder / (name+'.png')
    path.write_bytes(adb('exec-out', 'screencap', '-p'))
    return path


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['show','tap','screenshot']);parser.add_argument('value', nargs='?')
    args=parser.parse_args()
    if args.action == 'tap': tap(args.value)
    elif args.action == 'screenshot': print(screenshot(args.value))
    else:
        for node in snapshot().iter('node'):
            label=node.get('text') or node.get('content-desc')
            if label: print(label[:160], '| enabled='+str(node.get('enabled')), '| '+str(node.get('bounds')))
