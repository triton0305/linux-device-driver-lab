#!/usr/bin/env python3
"""Verify both real offline FDT merges, not target GPIO ownership or dt-schema."""
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
LEDS = [5, 6, 12, 13, 19, 16, 26, 20]
BUTTONS = [4, 17, 18, 27, 22, 23, 24, 25]


def nodes_from_fdt(path):
    blob = path.read_bytes()
    header = struct.unpack_from('>10I', blob)
    assert header[0] == 0xd00dfeed and header[1] == len(blob)
    pos, strings = header[2], header[3]
    stack, nodes = [], {}
    while True:
        token, = struct.unpack_from('>I', blob, pos)
        pos += 4
        if token == 1:
            end = blob.index(b'\0', pos)
            stack.append(blob[pos:end].decode())
            pos = (end + 4) & ~3
            name = '/'.join(stack) or '/'
            assert name not in nodes, f'duplicate path: {name}'
            nodes[name] = {}
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
            raise AssertionError(f'Bad FDT token {token}')
    return nodes


for variant, status in [('disabled', b'disabled\0'), ('validation-active', b'okay\0')]:
    nodes = nodes_from_fdt(ROOT / f'build/lab-gpio-eight-{variant}.merged.dtb')
    lab = {p: n for p, n in nodes.items()
           if n.get('compatible') == b'yocto-lab,gpio-demo\0'}
    expected = ['/lab-gpio'] + [f'/lab-gpio-d{i}' for i in range(1, 8)]
    assert set(lab) == set(expected)
    used = set()
    for i, path in enumerate(expected):
        assert lab[path]['status'] == status
        for prop, offset, flags in [('led-gpios', LEDS[i], 0), ('button-gpios', BUTTONS[i], 1)]:
            phandle, line, polarity = struct.unpack('>III', lab[path][prop])
            assert (line, polarity) == (offset, flags), (path, prop, line, polarity)
            controllers = [(p, n) for p, n in nodes.items()
                           if n.get('phandle') == struct.pack('>I', phandle)]
            assert len(controllers) == 1
            controller_path, controller = controllers[0]
            assert controller_path == '/soc/gpio@7e200000'
            assert 'gpio-controller' in controller
            assert controller['#gpio-cells'] == struct.pack('>I', 2)
            assert b'brcm,bcm2711-gpio\0' in controller['compatible']
            assert (phandle, line) not in used, f'duplicate GPIO {line}'
            used.add((phandle, line))
    assert len(used) == 16
    print(f'PASS {variant}: eight unique platform-node paths, 16 exact GPIO mappings, polarity and phandles')
print('NOT TESTED: dt-schema, target platform enumeration/probe, pin ownership and electrical behavior')
