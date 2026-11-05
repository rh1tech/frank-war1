import tempfile
import unittest
from pathlib import Path

from ai_compare import paired_extras, parse
from test_order_gameplay import replay_digest


class TileComparisonTests(unittest.TestCase):
    def test_same_flags_restart_selects_last_mission(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'tiles.txt'
            path.write_text(
                'ms 1 t 33 wave 1 flags 0001\nU 1 0 0 2 3 2 -1 60\n'
                'ms 2 t 66 wave 1 flags 0001\nU 1 0 0 3 3 2 -1 60\n'
                'ms 3 t 33 wave 1 flags 0001\nU 1 0 0 4 3 2 -1 60\n')
            frames = parse(path, '0001')
            self.assertEqual(len(frames), 1)
            self.assertEqual(frames[0][0], 33)
            self.assertEqual(frames[0][1][1][2:4], (4, 3))

    def test_port_survivor_is_extra_when_paired_original_died(self):
        pair = {(1, 1): (8, 1), (2, 1): (9, 1)}
        self.assertEqual(paired_extras(pair, {(2, 1): ()}, {(8, 1): (), (9, 1): ()}), [(8, 1)])

    def test_self_play_replay_ignores_ids_but_checks_hit_points(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / 'first.txt'
            replay = Path(directory) / 'replay.txt'
            header = 'ms 1 t 1 wave 1 flags 0001\n'
            first.write_text(header + 'U 1 0 0 2 3 6 8 60\n')
            replay.write_text(header + 'U 9 0 0 2 3 6 12 60\n')
            expected = replay_digest(first, '0001', 1)
            self.assertEqual(expected, replay_digest(replay, '0001', 1))
            replay.write_text(header + 'U 9 0 0 2 3 6 12 59\n')
            self.assertNotEqual(expected, replay_digest(replay, '0001', 1))

    def test_self_play_replay_rejects_missing_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'tiles.txt'
            path.write_text('ms 1 t 1 wave 1 flags 0001\n')
            with self.assertRaisesRegex(ValueError, 'missing units'):
                replay_digest(path, '0001', 1)
            path.write_text(
                'ms 1 t 1 wave 1 flags 0001\nU 1 0 0 2 3 2 -1 60\n'
                'ms 3 t 3 wave 1 flags 0001\nU 1 0 0 2 3 2 -1 60\n')
            with self.assertRaisesRegex(ValueError, 'incomplete gameplay trace'):
                replay_digest(path, '0001', 3)


if __name__ == '__main__':
    unittest.main()
