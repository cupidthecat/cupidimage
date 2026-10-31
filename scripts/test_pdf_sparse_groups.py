#!/usr/bin/env python3
"""Compare sparse implicit groups with flattened owned glyph controls."""

import argparse
import itertools
from pathlib import Path
import subprocess
import tempfile
from pdf_sparse_groups_fixture import nested
from test_pdf_pages import document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("allocator", type=Path)
    args = parser.parse_args()
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupid-sparse-groups-") as temporary:
        path = Path(temporary) / "case.pdf"

        def render(data, aa, reject=False):
            nonlocal checks
            path.write_bytes(data)
            result = subprocess.run(
                [str(args.probe.resolve()), str(path), "0", str(aa), "0", "0", "0", str(aa)],
                capture_output=True,
                timeout=60,
            )
            assert result.returncode == (3 if reject else 0), result.stderr
            checks += 1
            return result.stdout

        def faults(data, aa, reject=False):
            nonlocal allocations
            path.write_bytes(data)
            result = subprocess.run(
                [
                    str(args.allocator.resolve()),
                    str(path),
                    str(int(reject)),
                    str(aa),
                    "0",
                    "0",
                    "0",
                    str(aa),
                ],
                capture_output=True,
                text=True,
                timeout=120,
            )
            assert result.returncode == 0, (result.stdout, result.stderr)
            allocations += int(result.stdout.split("checks")[0].split()[-1])

        for depth, alpha, space, knockout, aa in itertools.product(
            (2, 4, 6),
            (0, 0.5, 1),
            ("/DeviceRGB", "/DeviceGray", "/DeviceCMYK"),
            (False, True),
            (0, 1),
        ):
            options = dict(depth=depth, alpha=alpha, space=space, knockout=knockout)
            assert render(nested(**options), aa) == render(nested(control=True, **options), aa), (
                depth,
                alpha,
                space,
                knockout,
                aa,
            )
        for space, aa in itertools.product(("/DeviceRGB", "/DeviceCMYK"), (0, 1)):
            faults(nested(size=(120, 90), space=space, alpha=0.5, knockout=True), aa)
        for aa in (0, 1):
            assert render(nested(depth=6, caption=True), aa) == render(
                nested(depth=6, caption=True, control=True), aa
            )
        # A late sample-allocation failure must not be swallowed by a terminal BI.
        for suffix in ("", " q Q"):
            content = "18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /RGB ID abc EI" + suffix
            data = document("/MediaBox [0 0 18 12]", stream=content).replace(
                b"abc", bytes((255, 0, 0))
            )
            pixels = render(data, 0)
            assert pixels[12:] == bytes((255, 0, 0, 255)) * (24 * 16)
            faults(data, 0)
        for aa in (0, 1):
            data = nested(depth=2, size=(120, 90), cyclic=True)
            render(data, aa, True)
            faults(data, aa, True)
    print(f"Sparse groups: {checks} exact page checks; {allocations} allocation checks")


if __name__ == "__main__":
    main()
