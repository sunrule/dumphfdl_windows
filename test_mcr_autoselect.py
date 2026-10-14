"""Functional test for MCR channel auto-selection from the system table."""
import struct, math, subprocess, sys, os

C_LIGHT = 299792458.0

def make_mcr(path, channels, n_samples, frequency, samples_per_sec,
             radius, angle, calib, tone_freq=None, seed=1):
    hdr = struct.pack('<2I2q6i',
        5391181, 42, 7, 1000, channels, n_samples, frequency, 3000,
        samples_per_sec, 0)
    body = bytearray()
    state = seed
    sr = samples_per_sec
    for s in range(n_samples):
        for c in range(channels):
            if tone_freq is not None:
                ph = 2 * math.pi * tone_freq * s / sr + c * 0.7
                i = int(20000 * math.cos(ph))
                q = int(20000 * math.sin(ph))
            else:
                state = (1103515245 * state + 12345) & 0x7fffffff
                i = ((state >> 8) & 0xffff)
                i = i - 0x10000 if i >= 0x8000 else i
                state = (1103515245 * state + 12345) & 0x7fffffff
                q = ((state >> 8) & 0xffff)
                q = q - 0x10000 if q >= 0x8000 else q
            body += struct.pack('<hh', i, q)
    with open(path, 'wb') as f:
        f.write(hdr)
        f.write(struct.pack('<%df' % channels, *radius))
        f.write(struct.pack('<%df' % channels, *angle))
        for c in range(channels):
            f.write(struct.pack('<dd', calib[c].real, calib[c].imag))
        f.write(bytes(body))

# systable with known frequencies spread over a wide range
SYSTABLE = """version = 52;
stations = (
  {
    id = 1;
    lat = 38.384587;
    lon = -121.759647;
    frequencies = ( 21934.0, 17919.0, 13276.0, 11327.0, 10081.0, 8927.0, 8912.0, 6559.0, 5508.0 );
    name = "San Francisco, California";
  },
  {
    id = 2;
    lat = 21.184428;
    lon = -157.186846;
    frequencies = ( 21937.0, 17919.0, 13324.0, 8912.0, 8936.0, 6565.0, 5514.0 );
    name = "Molokai, Hawaii";
  },
  {
    id = 3;
    lat = 63.847168;
    lon = -22.455754;
    frequencies = ( 17985.0, 15025.0, 11184.0, 8977.0, 6712.0, 5720.0, 3900.0 );
    name = "Reykjavik, Iceland";
  }
);
"""

def parse_systable_freqs(text):
    """Extract all frequencies (kHz) from the systable text."""
    freqs = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('frequencies'):
            # frequencies = ( 21934.0, 17919.0, ... );
            inner = line[line.index('(')+1:line.rindex(')')]
            for tok in inner.split(','):
                tok = tok.strip()
                if tok:
                    freqs.append(float(tok))
    return freqs

def run(exe, args, env, timeout=60):
    return subprocess.run([exe] + args, capture_output=True, text=True,
                          timeout=timeout, env=env)

def main():
    exe = sys.argv[1]
    workdir = sys.argv[2] if len(sys.argv) > 2 else '.'
    os.makedirs(workdir, exist_ok=True)

    env = dict(os.environ)
    env['PATH'] = 'C:/dev/build/prefix/bin;C:/msys64/mingw64/bin;' + env.get('PATH', '')

    channels = 4
    radius = [1.0, 2.5, 4.0, 5.5]
    angle = [0.0, 90.0, 180.0, 270.0]
    calib = [complex(1.0, 0.0), complex(0.5, 0.25),
             complex(-0.75, 0.1), complex(0.3, -0.6)]

    st_path = os.path.join(workdir, 'systable.conf')
    open(st_path, 'w').write(SYSTABLE)

    # Test 1: auto-selection picks exactly the in-band frequencies
    # MCR centered at 8912 kHz with 12 kHz sample rate -> band 8906..8918 kHz
    # In-band from systable: 8912.0 (twice, deduped) -> exactly 1 channel
    mcr = os.path.join(workdir, 'narrow.mcr')
    make_mcr(mcr, channels, 1000, 8912000, 12000, radius, angle, calib,
             tone_freq=1200.0)
    r = run(exe, ['--mcr-file', mcr, '--system-table', st_path,
                  '--output', 'decoded:text:file:path=/dev/null'], env)
    out = (r.stdout + r.stderr).replace("\r\n", "\n")
    all_freqs = parse_systable_freqs(SYSTABLE)
    expected = sorted(set(f for f in all_freqs if 8906.0 <= f <= 8918.0))
    ok = ('Auto-selected %d channel(s)' % len(expected)) in out
    for f in expected:
        ok = ok and ('%.3f kHz' % f) in out
    print('%s auto-selection (narrow band): expected %s, got line present: %s' %
          ('OK' if ok else 'FAIL', expected,
           [l for l in out.splitlines() if 'Auto-selected' in l or 'kHz' in l][:6]))
    if not ok:
        print(out[:2000])
        return 1

    # Test 2: wider band selects more channels
    # MCR centered at 8920 kHz with 100 kHz sample rate -> band 8870..8970 kHz
    # In-band: 8927, 8912, 8936, 8977? no (8977 > 8970). So: 8912, 8927, 8936
    mcr2 = os.path.join(workdir, 'wide.mcr')
    make_mcr(mcr2, channels, 1000, 8920000, 100000, radius, angle, calib,
             tone_freq=1200.0)
    r = run(exe, ['--mcr-file', mcr2, '--system-table', st_path,
                  '--output', 'decoded:text:file:path=/dev/null'], env)
    out = (r.stdout + r.stderr).replace("\r\n", "\n")
    expected2 = sorted(set(f for f in all_freqs if 8870.0 <= f <= 8970.0))
    ok = ('Auto-selected %d channel(s)' % len(expected2)) in out
    for f in expected2:
        ok = ok and ('%.3f kHz' % f) in out
    print('%s auto-selection (wide band): expected %s' %
          ('OK' if ok else 'FAIL', expected2))
    if not ok:
        print(out[:2000])
        return 1

    # Test 3: explicit frequencies still work (no auto-selection)
    r = run(exe, ['--mcr-file', mcr, '--system-table', st_path,
                  '--output', 'decoded:text:file:path=/dev/null', '8912.0'], env)
    out = (r.stdout + r.stderr).replace("\r\n", "\n")
    ok = 'Auto-selected' not in out and r.returncode in (0, 130)
    print('%s explicit frequencies bypass auto-selection (rc=%d)' %
          ('OK' if ok else 'FAIL', r.returncode))
    if not ok:
        return 1

    # Test 4: no system table + no frequencies -> error
    r = run(exe, ['--mcr-file', mcr,
                  '--output', 'decoded:text:file:path=/dev/null'], env)
    out = (r.stdout + r.stderr).replace("\r\n", "\n")
    ok = r.returncode != 0 and 'No channel frequencies given' in out
    print('%s missing systable errors out (rc=%d)' % ('OK' if ok else 'FAIL', r.returncode))
    if not ok:
        return 1

    # Test 5: systable with no in-band channels -> clear error
    mcr3 = os.path.join(workdir, 'empty_band.mcr')
    make_mcr(mcr3, channels, 100, 5000000, 12000, radius, angle, calib)
    r = run(exe, ['--mcr-file', mcr3, '--system-table', st_path,
                  '--output', 'decoded:text:file:path=/dev/null'], env)
    out = (r.stdout + r.stderr).replace("\r\n", "\n")
    ok = r.returncode != 0 and 'fall within' in out
    print('%s empty band errors out clearly (rc=%d)' % ('OK' if ok else 'FAIL', r.returncode))
    if not ok:
        return 1

    # Test 6: full run with auto-selected channels completes
    r = run(exe, ['--mcr-file', mcr2, '--system-table', st_path,
                  '--output', 'decoded:text:file:path=/dev/null'], env, timeout=60)
    out = (r.stdout + r.stderr).replace("\r\n", "\n")
    ok = 'Segmentation' not in out and r.returncode in (0, 130)
    print('%s full run with auto-selection completes (rc=%d)' %
          ('OK' if ok else 'FAIL', r.returncode))
    if not ok:
        print(out[-1500:])
        return 1

    print('ALL AUTO-SELECTION TESTS PASSED')
    return 0

if __name__ == '__main__':
    sys.exit(main())
