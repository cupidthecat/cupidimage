#!/usr/bin/env python3
"""Check longer Type 1 hint traces, PDF placement, painting and ownership."""

import argparse
from pathlib import Path
import subprocess
import tempfile
from pdf_type1_replacement_fixture import CASES, program
from test_pdf_type1_page_hinting import pdf
from generate_pdf_type1_replacement_fixture import build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("allocator", type=Path)
    args = parser.parse_args()
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix="cupid-type1-replacements-") as tmp:
        path = Path(tmp) / "case.pdf"

        def render(font, content, aa):
            nonlocal checks
            path.write_bytes(pdf(content, program=font))
            result = subprocess.run(
                [str(args.probe.resolve()), str(path), "0", str(aa), "0", "0", "1"],
                capture_output=True,
                timeout=30,
            )
            assert result.returncode == 0, result.stderr
            checks += 1
            return result.stdout

        for stems, groups in CASES:
            for axis in (False, True):
                actual = program(stems, groups, axis)
                control = program(stems, groups, axis, hints=False)
                for aa in (0, 1):
                    for matrix in ("1 0 0 1", "0 1 -1 0", "1 .25 0 1"):
                        for mode in range(8):
                            content = (
                                f"q {matrix} 45 25 cm .8 w 0.1 .3 .6 rg "
                                f"BT /F 24 Tf {mode} Tr 0 0 Td (A) Tj ET "
                                "1 .2 .1 rg -4 -4 30 30 re f Q"
                                if mode >= 4
                                else f"q {matrix} 45 25 cm .8 w 0.1 .3 .6 rg "
                                f"BT /F 24 Tf {mode} Tr 0 0 Td (A) Tj ET Q"
                            )
                            pixels = render(actual, content, aa)
                            if stems * groups > 96:
                                assert pixels == render(control, content, aa), (
                                    stems,
                                    groups,
                                    axis,
                                    aa,
                                    matrix,
                                    mode,
                                )
                    state = "BT /F 24 Tf 1.25 Tc 80 Tz 3 Ts 20 30 Td "
                    assert render(actual, state + "(AA) Tj ET", aa) == render(
                        actual,
                        state + "(A) Tj 1 0 0 1 30.6 30 Tm (A) Tj ET",
                        aa,
                    ), "fitting changed PDF advances"
                if (stems, groups, axis) in ((25, 4, False), (97, 1, True)):
                    for aa in (0, 1):
                        render(actual, "BT /F 24 Tf 20 30 Td (AA) Tj ET", aa)
                        result = subprocess.run(
                            [str(args.allocator.resolve()), str(path), "0", str(aa), "0", "0", "1"],
                            capture_output=True,
                            text=True,
                            timeout=120,
                        )
                        assert result.returncode == 0, (result.stdout, result.stderr)
                        allocations += int(result.stdout.split("checks")[0].split()[-1])
        for aa in (0, 1):
            for hint in (0, 1):
                outputs = []
                for control in (False, True):
                    path.write_bytes(build(control))
                    result = subprocess.run(
                        [str(args.probe.resolve()), str(path), "0", str(aa), "0", "0", str(hint)],
                        capture_output=True,
                        timeout=30,
                    )
                    assert result.returncode == 0, result.stderr
                    outputs.append(result.stdout)
                    checks += 1
                assert outputs[0] == outputs[1], "owned asset differs from its literal controls"
    print(f"Type 1 replacements: {checks} page checks; {allocations} allocation checks")


if __name__ == "__main__":
    main()
