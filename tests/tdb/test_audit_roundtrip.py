import tempfile
import unittest
from pathlib import Path

from audit_roundtrip import compare


class AuditComparisonTests(unittest.TestCase):
    def diff(self, a, b):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / name for name in ('a.tdb', 'b.tdb')]
            for path, body in zip(paths, (a, b)):
                path.write_text('SIMISA@@@@@@@@@@JINX0T0t______\n' + body, encoding='utf-16')
            return compare(*paths)

    def test_numeric_spelling_and_whitespace(self):
        result = self.diff('TrackDB( Serial(01) Value(-0 1e-3))',
                           'trackdb (serial ( 1 ) value ( 0.0 0.001 ))')
        self.assertEqual(result['counts'], {'numeric_spelling_only': 3})

    def test_numeric_change_and_float_equivalence(self):
        result = self.diff('Value(451.227 1e+006)', 'Value(451.22699 7)')
        self.assertEqual(result['counts'], {'same_float32': 1, 'changed_float32': 1})
        self.assertEqual(result['changed_numbers_at_six_significant_digits'], {'same': 1, 'different': 1})

    def test_blocks_match_by_id(self):
        result = self.diff('TrackNodes(2 TrackNode(1 UiD(5)) TrackNode(2 UiD(6)))',
                           'TrackNodes(2 TrackNode(2 UiD(6)) TrackNode(1 UiD(5)))')
        self.assertEqual(result['counts'], {'identical_atoms': 5})

    def test_lost_data_is_reported(self):
        result = self.diff('Value(1 2) Extra(3)', 'Value(1)')
        self.assertEqual(result['counts'], {'atom_count': 1, 'identical_atoms': 1, 'removed_block': 1})

    def test_reference_sort_preserves_membership(self):
        result = self.diff('TrItemRefs(2 TrItemRef(4) TrItemRef(3))',
                           'TrItemRefs(2 TrItemRef(3) TrItemRef(4))')
        self.assertEqual(result['reordered_reference_blocks'], ['/tritemrefs[1]'])
        self.assertEqual(result['changed_numbers_at_six_significant_digits'], {})


if __name__ == '__main__':
    unittest.main()
