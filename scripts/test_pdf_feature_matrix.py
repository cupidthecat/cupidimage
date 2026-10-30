#!/usr/bin/env python3
"""Validate the PDF requirement inventory without treating entries as test passes."""
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    matrix = json.loads((root / 'docs/pdf-feature-matrix.json').read_text())
    statuses = set(matrix['status_values'])
    assert statuses == {'missing', 'partial', 'implemented-unverified', 'verified'}
    sections = matrix['sections']
    identifiers = {section['id'] for section in sections}
    assert len(identifiers) == len(sections)
    seen = set()
    counts = {status: 0 for status in (*sorted(statuses), 'unassessed')}
    for section in sections:
        assert section['title'] and set(section['dependencies']) <= identifiers
        for pointer in section['review_pointers']:
            assert (root / pointer).is_file(), pointer
        for item in section['items']:
            assert item['id'] not in seen and item['id'].startswith(str(section['id']) + '.')
            seen.add(item['id'])
            assert item['requirement'] and item['audit']
            assert item['status'] is None or item['status'] in statuses
            counts[item['status'] or 'unassessed'] += 1
            for pointer in item['evidence']:
                assert (root / pointer).is_file(), (item['id'], pointer)
            assert all(int(value) in identifiers for value in item['dependencies'])
            if item['status'] in ('partial', 'implemented-unverified', 'verified'):
                assert item['evidence'], item['id']
            if item['status'] == 'verified':
                assert item['standard_clauses'], item['id']
                verification = item.get('verification', {})
                for kind in ('positive', 'negative', 'boundary', 'configuration'):
                    assert verification.get(kind), (item['id'], kind)
    print(f'PDF feature inventory: {len(seen)} unique requirements; {counts}')
    print('Inventory validation checks bookkeeping, not rendering conformance.')


if __name__ == '__main__':
    main()
