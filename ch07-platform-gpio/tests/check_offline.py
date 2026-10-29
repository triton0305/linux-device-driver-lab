#!/usr/bin/env python3
"""Inspect real merged FDT fixups. Not a replacement for dt-schema or hardware."""
import pathlib
import struct
import sys
import yaml

root = pathlib.Path(__file__).resolve().parents[1]
schema = yaml.safe_load((root / 'dt/yocto-lab,gpio-demo.yaml').read_text())
assert schema['properties']['compatible']['const'] == 'yocto-lab,gpio-demo'
assert {'compatible', 'led-gpios', 'button-gpios'} <= set(schema['required'])
print('PASS: YAML parse and required-property declaration (NOT dt-schema validation)')

blob = (root / 'build/merged-offline.dtb').read_bytes()
header = struct.unpack_from('>10I', blob)
assert header[0] == 0xd00dfeed and header[1] == len(blob)
pos, strings = header[2], header[3]
stack, nodes = [], {}
while True:
    token, = struct.unpack_from('>I', blob, pos)
    pos += 4
    if token == 1:  # FDT_BEGIN_NODE
        end = blob.index(b'\0', pos)
        stack.append(blob[pos:end].decode())
        pos = (end + 4) & ~3
        nodes['/'.join(stack) or '/'] = {}
    elif token == 2:
        stack.pop()
    elif token == 3:
        length, nameoff = struct.unpack_from('>II', blob, pos)
        pos += 8
        start = strings + nameoff
        name = blob[start:blob.index(b'\0', start)].decode()
        nodes['/'.join(stack) or '/'][name] = blob[pos:pos + length]
        pos = (pos + length + 3) & ~3
    elif token == 4:
        pass
    elif token == 9:
        break
    else:
        sys.exit(f'Bad FDT token {token}')

lab = nodes['/lab-gpio']
assert lab['compatible'] == b'yocto-lab,gpio-demo\0'
assert lab['status'] == b'disabled\0', 'Do not accidentally enable template'
for prop in ('led-gpios', 'button-gpios'):
    phandle, offset, flags = struct.unpack('>III', lab[prop])
    controllers = [(p, n) for p, n in nodes.items()
                   if n.get('phandle') == struct.pack('>I', phandle)]
    assert len(controllers) == 1
    path, controller = controllers[0]
    assert 'gpio-controller' in controller
    assert controller['#gpio-cells'] == struct.pack('>I', 2)
    assert b'brcm,bcm2711-gpio\0' in controller['compatible']
    assert offset < 58 and flags in (0, 1)
    print(f'PASS: {prop} resolves to {path}, offset={offset}, flags={flags}')
print('PASS: merged node remains disabled. Target occupancy and hardware NOT TESTED.')
