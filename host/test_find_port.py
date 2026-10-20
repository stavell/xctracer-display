import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from collections import namedtuple
from load_pack import pick_port
P = namedtuple('P', 'device vid pid serial_number location')
a = P('/dev/cu.usbmodem1101', 0x239A, 0x8087, 'AB', '1-1')
b = P('/dev/tty.usbmodem1101', 0x239A, 0x8087, 'AB', '1-1')
assert pick_port([a], []) == '/dev/cu.usbmodem1101'
assert pick_port([b, a], []) == '/dev/cu.usbmodem1101'
assert pick_port([P('/dev/cu.x', None, None, None, None)], ['/dev/cu.usbmodem1101', '/dev/tty.usbmodem1101']) == '/dev/cu.usbmodem1101'
assert pick_port([], ['/dev/cu.usbmodem1101']) == '/dev/cu.usbmodem1101'
c = P('/dev/cu.usbmodem2201', 0x239A, 0x8087, 'CD', '1-2')
for bad in ([a, c], []):
    try:
        pick_port(bad, []); raise AssertionError
    except SystemExit:
        pass
print('ok')
