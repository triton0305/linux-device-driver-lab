#!/usr/bin/env python3
"""Host mock integration tests. Temporary runtime/trace files are removed on exit."""
import ctypes
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
# Linux subreaper: collect this test's daemon children instead of leaving zombies.
assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0
with tempfile.TemporaryDirectory(prefix='g8-', dir='/tmp') as tmp:
    p = Path(tmp)
    devices = [p / f'dev{i}' for i in range(8)]
    for device in devices:
        device.write_bytes(struct.pack('i', 1))
    env = os.environ.copy()
    env['MOCK_DIR'] = tmp
    exe = p / 'app'
    spec = ','.join(map(str, devices))
    subprocess.run([
        'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        f'-DLAB_GPIO_RUNTIME_DIR="{p / "run"}"', f'-I{ROOT / "include"}',
        str(ROOT / 'userspace/lab_gpio_test.c'), str(ROOT / 'tests/mock_gpio.c'),
        '-Wl,--wrap=ioctl,--wrap=read,--wrap=write,--wrap=syslog,--wrap=__syslog_chk',
        '-o', str(exe)], check=True)

    def val(name, i, value):
        q = p / f'{name}{i}.new'
        q.write_text(str(value))
        q.replace(p / f'{name}{i}')

    def events(kind=None, channel=None):
        result = [line.split() for line in (p / 'trace').read_text().splitlines()] if (p / 'trace').exists() else []
        return [x for x in result if (kind is None or x[3] == kind) and (channel is None or int(x[2]) == channel)]

    def leds():
        return [struct.unpack('i', dev.read_bytes()[:4])[0] for dev in devices]

    def cmd(*args, target=spec, ok=True):
        r = subprocess.run([str(exe), str(target), *args], env=env, text=True, capture_output=True, timeout=8)
        assert (r.returncode == 0) == ok, (target, args, r.returncode, r.stdout, r.stderr)
        return r

    def wait_for(fn, timeout=2):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            if fn():
                return
            time.sleep(.005)
        raise AssertionError('condition timed out')

    def stopped():
        return cmd('toggle', 'status').stdout.count('controller stopped.') == 8

    def reset():
        (p / 'trace').write_text('')
        (p / 'errors').write_text('')
        for i in range(8):
            for name in ('fail_get', 'fail_set', 'fail_off', 'unbind', 'button'):
                val(name, i, 0)

    def start():
        r = cmd('toggle', 'start')
        assert '8 channel(s)' in r.stdout and leds() == [0] * 8
        pids = {int(x[1]) for x in events('GET')}
        assert len(pids) == 1, pids
        pid = pids.pop()
        links = [os.readlink(f) for f in (Path('/proc') / str(pid) / 'fd').iterdir()]
        assert all(str(dev) in links for dev in devices)
        time.sleep(.09)
        return pid

    def button(i, value):
        val('button', i, value)
        time.sleep(.10)

    def all_buttons(value):
        for i in range(8):
            val('button', i, value)
        time.sleep(.12)

    def reap():
        while True:
            try:
                child, _ = os.waitpid(-1, os.WNOHANG)
                if not child:
                    break
            except ChildProcessError:
                break

    try:
        reset()
        pid = start()
        expected = [0] * 8
        for i in range(8):
            button(i, 1)
            expected[i] = 1
            assert leds() == expected
            n = len(events('SET'))
            time.sleep(.08)
            assert len(events('SET')) == n
            button(i, 0)
            assert leds() == expected
        for i in range(8):
            button(i, 1)
            expected[i] = 0
            assert leds() == expected
            button(i, 0)
        assert len(events('SET')) == 16
        cmd('toggle', 'start', ok=False)
        for i in range(8):
            cmd('on', target=devices[i], ok=False)
            cmd('selftest', target=devices[i], ok=False)
            cmd('toggle', 'start', target=devices[i], ok=False)
        assert {int(x[1]) for x in events('GET')} == {pid}
        all_buttons(1)
        assert leds() == [1] * 8
        time.sleep(.2)
        assert len(events('SET')) == 24
        all_buttons(0)
        assert leds() == [1] * 8
        all_buttons(1)
        assert leds() == [0] * 8
        cmd('toggle', 'stop')
        assert stopped() and leds() == [0] * 8
        print('PASS: one process/eight FDs, initial OFF, eight independent toggles, simultaneous presses, long hold/release, duplicate/overlapping writers, stop', flush=True)

        reset()
        start()
        # Each alternating level persists for one observed sample, never three.
        def one_sample(i, level):
            n = len(events('GET', i))
            val('button', i, level)
            wait_for(lambda: len(events('GET', i)) > n)
        for i in range(8):
            for b in [1, 0, 1, 0, 1, 0]:
                one_sample(i, b)
            assert not events('SET', i)
            button(i, 1)
            assert len(events('SET', i)) == 1
            for b in [0, 1, 0, 1, 0, 1]:
                one_sample(i, b)
            assert len(events('SET', i)) == 1
            button(i, 0)
            button(i, 1)
            assert len(events('SET', i)) == 2
        cmd('off')
        assert stopped() and leds() == [0] * 8
        print('PASS: per-channel bouncing/rearm and group off', flush=True)

        reset()
        for i in range(0, 8, 2):
            val('button', i, 1)
        start()
        time.sleep(.15)
        assert not events('SET')
        for i in range(1, 8, 2):
            button(i, 1)
        assert leds() == [0, 1, 0, 1, 0, 1, 0, 1]
        for i in range(0, 8, 2):
            button(i, 0)
            button(i, 1)
        assert leds() == [1] * 8
        cmd('off', target=devices[5])
        assert stopped() and leds() == [0] * 8
        print('PASS: initially pressed channels isolated; single-member off stops whole group', flush=True)

        reset()
        cmd('toggle', 'start', target=devices[6])
        before = len(events('OFF'))
        cmd('toggle', 'start', ok=False)
        assert len(events('OFF')) == before  # no interference with existing owner
        cmd('toggle', 'stop', target=devices[6])
        reset()
        jobs = [subprocess.Popen([str(exe), spec if i % 2 else ','.join(map(str, reversed(devices))), 'toggle', 'start'], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for i in range(4)]
        results = [(j.communicate(timeout=8), j.returncode) for j in jobs]
        assert sorted(x[1] for x in results) == [0, 1, 1, 1]
        cmd('off')
        duplicate = ','.join([str(devices[0])] * 8)
        cmd('toggle', 'start', target=duplicate, ok=False)
        cmd('toggle', 'start', target=spec + ',' + str(devices[0]), ok=False)
        assert stopped()
        print('PASS: existing single controller overlap, four reversed-order concurrent starts, duplicate/oversize list rejected', flush=True)

        reset()
        for dev in devices:
            dev.write_bytes(struct.pack('i', 1))
        devices[3].rename(p / 'missing')
        cmd('toggle', 'start', ok=False)
        assert stopped()
        assert {int(x[2]) for x in events('OFF')} == set(range(8)) - {3}
        assert all(struct.unpack('i', dev.read_bytes()[:4])[0] == 0 for dev in devices if dev.exists())
        (p / 'missing').rename(devices[3])
        cmd('off')
        for failure in ('fail_get', 'fail_off'):
            reset()
            for dev in devices:
                dev.write_bytes(struct.pack('i', 1))
            val(failure, 2, 1)
            cmd('toggle', 'start', ok=False)
            assert stopped()
            assert all(state == 0 for i, state in enumerate(leds()) if i != 2)
            assert {int(x[2]) for x in events('OFF')} >= set(range(8)) - {2}
            assert 'initialization failed:' in (p / 'errors').read_text()
            val(failure, 2, 0)
            cmd('off')
        print('PASS: missing/open, initial GET/OFF failures prevent partial startup and clean every accessible channel including later devices', flush=True)

        for failure in ('fail_get', 'fail_set', 'unbind'):
            reset()
            start()
            if failure != 'fail_set':
                all_buttons(1)
            val(failure, 3, 1)
            if failure == 'fail_set':
                button(3, 1)
            wait_for(stopped)
            assert 'I/O failure:' in (p / 'errors').read_text()
            assert {int(x[2]) for x in events('OFF')} >= set(range(8)) - {3}
            assert all(state == 0 for i, state in enumerate(leds()) if i != 3)
            if failure != 'unbind':
                assert leds() == [0] * 8
                assert len(events('GET_FAIL' if failure == 'fail_get' else 'SET_FAIL', 3)) == 1
            else:
                assert 'exit LED OFF failed:' in (p / 'errors').read_text()
                assert len(events('UNBIND', 3)) == 2  # failed GET + one cleanup OFF
            val(failure, 3, 0)
            cmd('off')
        reset()
        start()
        all_buttons(1)
        val('fail_off', 2, 1)
        cmd('off', ok=False)
        assert stopped() and all(state == 0 for i, state in enumerate(leds()) if i != 2)
        val('fail_off', 2, 0)
        cmd('off')
        print('PASS: runtime GET/SET/unbind failure stops all, no retry storm, cleanup continues past OFF failure', flush=True)

        for sig in (signal.SIGINT, signal.SIGTERM):
            reset()
            pid = start()
            all_buttons(1)
            os.kill(pid, sig)
            wait_for(stopped)
            assert leds() == [0] * 8
        reset()
        pid = start()
        os.kill(pid, signal.SIGHUP)
        assert not stopped()
        all_buttons(1)
        os.kill(pid, signal.SIGKILL)
        wait_for(stopped)
        assert leds() == [1] * 8
        reset()
        start()
        assert leds() == [0] * 8
        cmd('off')
        print('PASS: SIGINT/SIGTERM all-OFF, SIGHUP survival, SIGKILL stale-socket recovery (OFF not guaranteed on kill)', flush=True)

        reset()
        for i in range(8):
            cmd('on', target=devices[i])
            assert 'LED=ON' in cmd('get', target=devices[i]).stdout
            cmd('off', target=devices[i])
            r = cmd('selftest', target=devices[i])
            assert '0 failures' in r.stdout
        # Existing hold command must preserve removed-FD behavior.
        hold = subprocess.Popen([str(exe), str(devices[0]), 'hold'], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert 'FD held:' in hold.stdout.readline()
        val('unbind', 0, 1)
        out, err = hold.communicate(timeout=3)
        assert hold.returncode == 0 and 'Old FD rejected' in out, (out, err)
        val('unbind', 0, 0)
        print('PASS: single-device get/on/off/selftest on all channels; hold regression', flush=True)

        reset()
        pid = start()
        begin = time.monotonic()
        watch = subprocess.Popen([str(exe), str(devices[0]), 'watch'], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        logs = [(time.monotonic(), line) for line in watch.stdout]
        _, err = watch.communicate(timeout=3)
        elapsed = time.monotonic() - begin
        reads = [x for x in events('GET', 0) if int(x[1]) == watch.pid]
        assert watch.returncode == 0 and len(reads) == 300 and len(logs) == 30 and 29.9 < elapsed < 35, err
        intervals = [b[0] - a[0] for a, b in zip(logs, logs[1:])]
        assert min(intervals) > .99 and not stopped()
        scans = [float(x[0]) for x in events('GET', 7) if int(x[1]) == pid]
        mean = (scans[-1] - scans[0]) / (len(scans) - 1)
        cmd('off')
        assert stopped() and leds() == [0] * 8
        print(f'PASS: watch 300 GETs/30 logs/{elapsed:.3f}s, log mean {sum(intervals)/len(intervals):.6f}s; eight-channel controller survives >30 s; D7 scan mean {mean:.6f}s', flush=True)
        print('ALL HOST MOCK TESTS PASS; HW NOT TESTED', flush=True)
    finally:
        if (p / 'missing').exists():
            (p / 'missing').rename(devices[3])
        for i in range(8):
            for name in ('fail_get', 'fail_set', 'fail_off', 'unbind'):
                val(name, i, 0)
        subprocess.run([str(exe), spec, 'off'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=8)
        reap()
