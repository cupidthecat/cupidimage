#!/usr/bin/env python3
"""Check default optional-content states, intents and membership expressions."""
import itertools
from pathlib import Path
import subprocess
import sys
import tempfile
from test_pdf_pages import document


def fixture(configuration='/OFF [6 0 R]', members=None, groups=None, catalog=True):
    objects = {5: '<< /Type /OCG /Name (same name) >>',
               6: '<< /Type /OCG /Name (same name) >>'}
    objects.update(groups or {})
    objects.update(members or {})
    if catalog:
        objects[1] = ('<< /Type /Catalog /Pages 2 0 R /OCProperties << /OCGs [5 0 R 6 0 R] '
                      '/D << ' + configuration + ' >> >> >>')
    return document('/MediaBox [0 0 18 12]', stream='', extra=objects).replace(b'%PDF-1.4', b'%PDF-1.7', 1)


def main():
    probe = str(Path(sys.argv[1]).resolve())
    checks = allocations = 0
    with tempfile.TemporaryDirectory(prefix='cupid-optional-content-') as directory:
        path = Path(directory) / 'case.pdf'
        def check(data, expected, reject=False, faults=False):
            nonlocal checks, allocations
            path.write_bytes(data)
            args = [probe, *(['--faults'] if faults else []), str(path), *map(str, expected)]
            result = subprocess.run(args, capture_output=True, text=True, timeout=30)
            assert (result.returncode != 0) == reject, (result.returncode, result.stdout, result.stderr)
            checks += 1
            if reject:
                return
            if faults:
                allocations += int(result.stdout.split()[0])
            else:
                actual = dict(tuple(map(int, line.split())) for line in result.stdout.splitlines())
                assert actual == expected, (actual, expected)
        from test_pdf_optional_pages import compressed_page
        check(compressed_page(), {90: 1, 91: 0, 20: 1})
        check(compressed_page(True), {90: 0, 91: 1, 20: 0})
        check(compressed_page(), {90: 1, 91: 0, 20: 1}, faults=True)
        check(fixture(), {5: 1, 6: 0})
        check(fixture(''), {5: 1, 6: 1})
        check(fixture('/BaseState /OFF /ON [5 0 R]'), {5: 1, 6: 0})
        check(fixture('/BaseState /Unchanged /OFF [5 0 R]'), {5: 0, 6: 1})
        check(fixture(catalog=False), {5: 1, 6: 1})
        check(fixture('/BaseState /Invalid'), {5: 1}, reject=True)
        for on5, on6, policy in itertools.product((0, 1), (0, 1), ('AnyOn', 'AllOn', 'AnyOff', 'AllOff')):
            off = ' '.join(f'{n} 0 R' for n, on in ((5, on5), (6, on6)) if not on)
            wanted = {'AnyOn': on5 or on6, 'AllOn': on5 and on6,
                      'AnyOff': not(on5 and on6), 'AllOff': not(on5 or on6)}[policy]
            membership = '<< /Type /OCMD /OCGs [5 0 R 6 0 R] /P /' + policy + ' >>'
            data = fixture('/OFF [' + off + ']', {20: membership})
            check(data, {20: int(wanted)})
        for members in ('', '/OCGs []', '/OCGs null', '/OCGs [null 7 0 R]'):
            for policy in ('AnyOn', 'AllOn', 'AnyOff', 'AllOff'):
                check(fixture(members={20: f'<< /Type /OCMD {members} /P /{policy} >>'}), {20: 1})
        check(fixture(members={20: '<< /Type /OCMD /OCGs 6 0 R >>'}), {20: 0})
        expressions = [('/And 5 0 R 6 0 R', 0), ('/Or 5 0 R 6 0 R', 1),
                       ('/Not 6 0 R', 1), ('/Not 5 0 R', 0),
                       ('/And [/Or 5 0 R 6 0 R] [/Not 6 0 R]', 1)]
        for expression, expected in expressions:
            data = fixture(members={20: '<< /Type /OCMD /P /Invalid /OCGs /Invalid /VE [' + expression + '] >>'})
            check(data, {20: expected})
            check(data, {20: expected}, faults=True)
        groups = {6: '<< /Type /OCG /Name (design) /Intent /Design >>'}
        for intent, visible in (('/View', 1), ('/Design', 0), ('[/View /Design]', 0), ('[]', 1)):
            data = fixture('/Intent ' + intent + ' /OFF [6 0 R]', groups=groups)
            check(data, {5: 1, 6: visible})
            check(data, {5: 1, 6: visible}, faults=True)
        for member in ('/OCGs 6 0 R /P /AllOff', '/VE [/Not 6 0 R]', '/VE [/And 6 0 R]'):
            check(fixture(members={20: '<< /Type /OCMD ' + member + ' >>'}, groups=groups), {20: 1})
        for expression in ('[]', '[/And]', '[/Not]', '[/Not 5 0 R 6 0 R]',
                           '[/Or null]', '[/Other 5 0 R]', '[/And 20 0 R]'):
            check(fixture(members={20: '<< /Type /OCMD /VE ' + expression + ' >>'}), {20: 1}, reject=True)
        check(fixture(members={20: '<< /Type /OCMD /VE 21 0 R >>', 21: '[/Not 21 0 R]'}),
              {20: 1}, reject=True)
        check(fixture(members={20: '<< /Type /OCMD /OCGs [7 0 R] >>', 7: '8 0 R', 8: '7 0 R'}),
              {20: 1}, reject=True)
        check(fixture(members={20: '<< /Type /Metadata /OCGs [6 0 R] >>'}), {20: 1})
        check(fixture('/OFF [5 0 R]', members={7: '5 0 R'}), {7: 0})
        check(fixture('/OFF [7 0 R]', members={7: '5 0 R'}), {5: 0, 7: 0})
        for depth in (1, 63, 64, 65):
            expression = '[/Not ' * depth + '5 0 R' + ']' * depth
            check(fixture(members={20: '<< /Type /OCMD /VE ' + expression + ' >>'}),
                  {20: 1 - depth % 2}, reject=depth > 64)
        for count in (0, 1, 64, 65):
            check(fixture('/Intent [' + '/View ' * count + '] /OFF [6 0 R]'),
                  {5: 1, 6: int(count == 0)}, reject=count > 64, faults=count == 64)
        for count in (65536, 65537):
            catalog = '<< /Type /Catalog /Pages 2 0 R /OCProperties << /OCGs [' + '5 0 R ' * count + '] /D << >> >> >>'
            check(fixture(members={1: catalog}, catalog=False), {5: 1}, reject=count > 65536)
        for configuration in ('/Intent true', '/ON 5 0 R', '/OFF [20 0 R]'):
            check(fixture(configuration, {20: '<< /Type /OCMD >>'}), {5: 1}, reject=True)
        for properties in ('<< /OCGs [5 0 R] >>', '<< /OCGs [] /D null >>',
                           '<< /OCGs 5 0 R /D << >> >>'):
            catalog = '<< /Type /Catalog /Pages 2 0 R /OCProperties ' + properties + ' >>'
            check(fixture(members={1: catalog}, catalog=False), {5: 1}, reject=True)
        # Configuration/group intent names use PDF name decoding, not prefixes.
        check(fixture('/Intent /Des#69gn /OFF [6 0 R]', groups=groups), {5: 1, 6: 0})
        check(fixture('/Intent /DesignExtra /OFF [6 0 R]', groups=groups), {5: 1, 6: 1})
    print(f'PDF optional-content state: {checks} checks; {allocations} allocation checks')


if __name__ == '__main__':
    main()
