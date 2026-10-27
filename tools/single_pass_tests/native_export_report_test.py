import unittest

import native_export_report as export


class NativeExportReportTests(unittest.TestCase):
    def positive(self):
        # Independent literal ABI fixture, not synthesized by the report decoder.
        return [1, 1, 1, 1, 10, 8192, 0, 0, 1, 0, 0, 0, 417, 0x50000,
                8, 1, 1, 1, 1, 0, 1, 1, 0, 0]

    def test_success_requires_completed_native_roundtrip(self):
        result = export.validate(self.positive(), requested=True, frame=417)
        self.assertEqual(result['planes'], 10)
        self.assertEqual(result['sourceFrame'], 417)
        self.assertEqual(result['nativeSuppressed'], 0)

    def test_fault_proves_native_baseline_preserved(self):
        values = self.positive()
        values[9:11] = [1, 1]
        result = export.validate(values, requested=True, frame=417, rejected=True)
        self.assertEqual(result['preservedOnInputReject'], 1)
        for index in (9, 10):
            bad = values.copy(); bad[index] = 0
            with self.subTest(index=index), self.assertRaises(ValueError):
                export.validate(bad, requested=True, frame=417, rejected=True)
        with self.assertRaises(ValueError):
            export.validate(values, requested=True, frame=417)
        with self.assertRaises(ValueError):
            export.validate(self.positive(), requested=True, frame=417, rejected=True)

    def test_every_required_gate_rejects_missing_or_nonboolean_success(self):
        for index in (1, 2, 3, 8, 15, 16, 17, 18, 20, 21):
            for value in (0, 2):
                values = self.positive(); values[index] = value
                with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                    export.validate(values, requested=True, frame=417)

    def test_failure_waits_suppression_and_image_differences_rejected(self):
        for index in (6, 7, 11, 22, 23):
            values = self.positive(); values[index] = 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                export.validate(values, requested=True, frame=417)
        values = self.positive(); values[11] = 29
        with self.assertRaises(ValueError):export.validate(values, requested=True, frame=417)

    def test_frame_request_shapes_and_residency_bounds(self):
        for index, value in ((0, 0), (0, 2), (4, 9), (4, 11), (5, 0), (5, 512), (5, 8193),
                             (5, export.MAX_BYTES + 512), (12, 416), (12, 0), (13, 0),
                             (14, 0), (14, 9), (19, 0x80004005), (19, 2**32)):
            values = self.positive(); values[index] = value
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                export.validate(values, requested=True, frame=417)
        with self.assertRaises(ValueError):export.validate(self.positive(), requested=True, frame=418)

    def test_disabled_mode_requires_no_native_export_work(self):
        for frame in (0, 417):
            values = [0] * 24; values[12] = frame
            export.validate(values, requested=False, frame=417)
            for index in range(24):
                if index == 12:continue
                bad = values.copy(); bad[index] = 1
                with self.subTest(index=index), self.assertRaises(ValueError):
                    export.validate(bad, requested=False, frame=417)
        values = [0] * 24; values[12] = 416
        with self.assertRaises(ValueError):export.validate(values, requested=False, frame=417)

    def test_malformed_pod_and_validation_inputs_fail_closed(self):
        for values in (None, {}, [0] * 23, [0] * 25):
            with self.subTest(values=values), self.assertRaises(ValueError):export.decode(values)
        for value in (True, False, None, '1', -1, 2**64):
            values = self.positive(); values[0] = value
            with self.subTest(value=value), self.assertRaises(ValueError):export.decode(values)
        for options in ({'requested': 1, 'frame': 417}, {'requested': True, 'frame': 0},
                        {'requested': True, 'frame': True}, {'requested': True, 'frame': 417, 'rejected': 1}):
            with self.subTest(options=options), self.assertRaises(ValueError):export.validate(self.positive(), **options)


if __name__ == '__main__':
    unittest.main()
