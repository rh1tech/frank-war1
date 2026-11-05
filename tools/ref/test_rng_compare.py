import tempfile
import unittest
from pathlib import Path

from rng_compare import compare, read_run


class RngComparisonTests(unittest.TestCase):
    def test_restart_selects_last_mission(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'rng.txt'
            path.write_text('1 ff\n2 ee\n1 aa\n2 bb\n3 cc\n')
            self.assertEqual(read_run(path), [0xaa, 0xbb, 0xcc])

    def test_empty_or_short_run_is_not_a_match(self):
        for ref, port in (([], []), ([], [1]), ([1], [1, 2])):
            with self.subTest(ref=ref, port=port), self.assertRaises(ValueError):
                compare(ref, port, 2)

    def test_requested_window_ignores_extra_tail_but_finds_real_split(self):
        self.assertIsNone(compare([1, 2], [1, 2, 3], 2))
        self.assertEqual(compare([1, 2, 3], [1, 4, 3], 3), 2)

    def test_missing_tick_is_invalid(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'rng.txt'
            path.write_text('1 aa\n3 bb\n')
            with self.assertRaises(ValueError):
                read_run(path)


if __name__ == '__main__':
    unittest.main()
