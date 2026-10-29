#!/usr/bin/env python3
"""Exercise bounded scan conversion with extreme coordinates and mixed tags."""
import random
import subprocess
import sys

rng = random.Random(719)
for case in range(250):
    count = rng.randrange(1, 30)
    points = []
    for index in range(count):
        x = rng.choice((-2097152, 2097152, 0, rng.randrange(-2097152, 2097153)))
        y = rng.choice((-2097152, 2097152, 0, rng.randrange(-2097152, 2097153)))
        points.append((x, y, rng.randrange(3), int(index == count - 1 or rng.random() < .1)))
    request = f'{count} 4 4 {case % 2}\n' + ''.join('%d %d %d %d\n' % p for p in points)
    result = subprocess.run([sys.argv[1], '--probe'], input=request.encode(), capture_output=True,
                            timeout=10)
    assert result.returncode in (0, 1), (case, result.returncode, result.stderr, request)
    assert not result.stderr, (case, result.stderr)
    assert len(result.stdout) == (16 if result.returncode == 0 else 0), case
print('PDF coverage bounds: 250 extreme-coordinate and mixed-tag inputs handled safely')
