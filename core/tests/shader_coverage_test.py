"""Synthetic cache records only; no game assets or executable required."""
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('coverage', Path(__file__).parents[1] / 'tools/shader_coverage.py')
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)


def records(magic, size, offset, ids):
    data = bytearray(struct.pack('<8sII', magic, 1, size))
    for iid in ids:
        record = bytearray(size)
        record[offset:offset + 32] = bytes([iid]) * 32
        data.extend(record)
    return data


class CoverageTests(unittest.TestCase):
    def test_input_id_offsets_and_duplicates(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / 'shader-answers.bin').write_bytes(records(b'TOTKSANS', 216, 36, [1, 1, 2]))
            (p / 'shader-prepared.bin').write_bytes(records(b'TOTKSPRP', 36, 4, [1, 3]))
            result = coverage.coverage(p)
            self.assertEqual(result['recorded_records'], 3)
            self.assertEqual(result['recorded_unique_inputs'], 2)
            self.assertEqual(result['recorded_inputs_in_prepared'], 1)
            self.assertEqual(result['recorded_inputs_not_in_prepared'], ['02' * 32])

    def test_rejects_wrong_formats_and_truncation(self):
        good = records(b'TOTKSPRP', 36, 4, [1])
        bad = [good[:8], good[:-1], good + b'x', b'TOTKSPRG' + good[8:]]
        bad += [good[:8] + struct.pack('<II', 2, 36) + good[16:],
                good[:8] + struct.pack('<II', 1, 216) + good[16:],
                records(b'TOTKSPRP', 36, 4, [0])]
        for data in bad:
            with self.subTest(data=data), tempfile.TemporaryDirectory() as tmp:
                p = Path(tmp) / 'cache.bin'
                p.write_bytes(data)
                with self.assertRaises(ValueError):
                    coverage.read_ids(p, b'TOTKSPRP', 36, 4)

    def test_header_is_not_a_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'cache.bin'
            p.write_bytes(records(b'TOTKSPRP', 36, 4, [1] * 4917))
            self.assertEqual(p.stat().st_size, 177028)
            self.assertEqual(coverage.read_ids(p, b'TOTKSPRP', 36, 4), (4917, {'01' * 32}))


if __name__ == '__main__':
    unittest.main()
