from pathlib import Path
import struct
import tempfile
import unittest

from batch_shaders import (cache_key, cached_record, classify, collect, container_parts,
                           digest, event_weights, family_reports, pass_coverage, pipeline_report, save_json)

HEADER = '''!dx.shaderModel = !{!1}
!1 = !{!"vs", i32 6, i32 0}
!dx.resources = !{!2}
!2 = !{null, null, !3, null}
!3 = !{!4}
!4 = !{i32 0, %hostlayout.CameraShaderConsts* undef, !"", i32 0, i32 1, i32 1, i32 848, null}
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 1, i1 false)
  %v = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %h, i32 36)
  %x = extractvalue %dx.types.CBufRet.f32 %v, 0
'''


class BatchShaderTests(unittest.TestCase):
    def test_camera_dependency_and_missing_word(self):
        row = classify(HEADER)
        self.assertEqual(row['camera']['words'], [144])
        self.assertEqual(row['transform'], 'camera_view_id')
        self.assertFalse(row['runtimeEligible'])
        self.assertNotIn('early_camera_word_147_unavailable', row['requirements'])
        row = classify(HEADER.replace('%v, 0', '%v, 3'))
        self.assertIn('early_camera_word_147_unavailable', row['requirements'])

    def test_no_camera_is_not_eye_independence(self):
        row = classify(HEADER.replace('CameraShaderConsts', 'MaterialConsts'))
        self.assertEqual(row['transform'], 'metadata_only')
        self.assertIn('non_camera_constants_audit', row['requirements'])
        self.assertFalse(row['runtimeEligible'])

    def test_compute_excluded(self):
        self.assertEqual(classify(HEADER.replace('!"vs"', '!"cs"'))['status'], 'excluded')

    def test_reject_unknown_model_and_handle_uses(self):
        for text in (HEADER.replace('i32 6, i32 0', 'i32 6, i32 6'),
                     HEADER + '  call void @helper(%dx.types.Handle %h)',
                     HEADER.replace('!dx.resources', '!other'),
                     HEADER.replace('!3 =', '!30 ='), HEADER.replace('!4 =', '!40 =')):
            with self.assertRaises(ValueError):
                classify(text)

    def test_non_camera_resources_still_require_audit(self):
        text = HEADER.replace('!{null, null, !3, null}', '!{!5, !6, !3, null}')
        text += '''!5 = !{!7}
!6 = !{!8}
!7 = !{i32 0, %Texture2D* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, null}
!8 = !{i32 0, %RWTexture2D* undef, !"", i32 0, i32 0, i32 1, i32 2, i1 false, i1 false, i1 false, null}
'''
        row = classify(text)
        self.assertIn('uav_side_effect_audit', row['requirements'])
        self.assertIn('srv_eye_dependency_audit', row['requirements'])

    def test_deduplicate_shader_not_pipeline_stage(self):
        shader = dict(sha256='a' * 64, file='shader.dxil')
        shaders, psos = collect([dict(resources=dict(pPipelineState=12), shaders=[shader, shader]),
                                 dict(resources=dict(pPipelineState=13), shaders=[shader])])
        self.assertEqual(len(shaders), 1)
        self.assertEqual(len(psos[0]['shaders']), 2)
        with self.assertRaises(ValueError):
            collect([dict(resources=dict(pPipelineState=12), shaders=[dict(shader, sha256='../bad')])])
        with self.assertRaises(ValueError):
            collect([dict(resources=dict(pPipelineState=12), shaders=[shader])] * 2)

    def test_bytecode_success_never_runtime_eligibility(self):
        shader = dict(stage='vs', status='validated', requirements=['binding_equivalence'])
        row = pipeline_report(dict(pso=1, shaders=['x']), {'x': shader}, {})
        self.assertTrue(row['bytecodeReady'])
        self.assertFalse(row['runtimeEligible'])
        row = pipeline_report(dict(pso=1, shaders=['x', 'x']), {'x': shader}, {})
        self.assertIn('ambiguous_duplicate_stages', row['blockers'])

    def test_conflicting_camera_layouts(self):
        a = dict(stage='vs', status='validated', conversion=dict(space=0, camera_register=1,
                 original_size=848, eye_stride=1024, binding_size=2048))
        b = dict(a, stage='ps', conversion=dict(a['conversion'], original_size=832))
        row = pipeline_report(dict(pso=1, shaders=['a', 'b']), dict(a=a, b=b), {})
        self.assertIn('camera_layout_conflict', row['blockers'])

    def test_weights_do_not_count_marker_or_copy(self):
        events = [dict(pso=1, kind=k, ms=1.0, view='MAIN', node='geometry')
                  for k in ('draw', 'draw', 'dispatch', 'other', 'marker')]
        weights = event_weights(events)[1]
        self.assertEqual(weights['draws'], 2)
        self.assertEqual(weights['dispatches'], 1)
        self.assertEqual(weights['replayEventSumMs'], 3)

    def test_family_does_not_duplicate_shared_pso_weight(self):
        a = dict(classify(HEADER), status='validated')
        pipelines = [dict(pso=1, shaders=['a', 'b']), dict(pso=2, shaders=['a'])]
        families = family_reports(dict(a=a, b=a), pipelines, {1: dict(draws=10), 2: dict(draws=5)})
        self.assertEqual(len(families), 1)
        self.assertEqual(families[0]['draws'], 15)
        self.assertEqual(families[0]['shaders'], ['a', 'b'])

    def test_pass_coverage_not_runtime_approval(self):
        psos = [dict(pso=1, bytecodeReady=True), dict(pso=2, bytecodeReady=False)]
        coverage = pass_coverage(psos, {1: dict(draws=10, nodes={'RenderElements': 10}),
                                        2: dict(draws=5, nodes={'RenderElements': 5})})
        self.assertEqual(coverage['RenderElements']['draws'], 15)
        self.assertEqual(coverage['RenderElements']['bytecodeReadyDraws'], 10)
        self.assertEqual(coverage['RenderElements']['runtimeEligibleDraws'], 0)

    def test_cache_identity_covers_input_and_toolchain(self):
        key = cache_key('a', dict(validator='v1', converter='c1'))
        self.assertNotEqual(key, cache_key('b', dict(validator='v1', converter='c1')))
        self.assertNotEqual(key, cache_key('a', dict(validator='v2', converter='c1')))
        self.assertNotEqual(key, cache_key('a', dict(validator='v1', converter='c2')))

    def test_cache_detects_corruption_and_missing_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name in ('stereo.ll', 'stereo.dxil'):
                (directory / name).write_bytes(b'validated')
            row = dict(key='key', sha256='sha', status='validated', artifacts={
                name: digest(b'validated') for name in ('stereo.ll', 'stereo.dxil')})
            save_json(directory / 'result.json', row)
            self.assertIsNotNone(cached_record(directory, 'key', 'sha'))
            self.assertIsNone(cached_record(directory, 'other', 'sha'))
            (directory / 'stereo.dxil').write_bytes(b'corrupt')
            self.assertIsNone(cached_record(directory, 'key', 'sha'))
            (directory / 'stereo.dxil').unlink()
            self.assertIsNone(cached_record(directory, 'key', 'sha'))
            row['artifacts'] = {}
            save_json(directory / 'result.json', row)
            self.assertIsNone(cached_record(directory, 'key', 'sha'))
            for invalid in ([], dict(row, artifacts=[]), dict(row, status='unknown')):
                save_json(directory / 'result.json', invalid)
                self.assertIsNone(cached_record(directory, 'key', 'sha'))

    def test_container_bounds_and_legacy_part(self):
        data = b'DXBC' + bytes(20) + struct.pack('<3I', 48, 1, 36)
        data += b'SHEX' + struct.pack('<2I', 4, 0x00010050)
        self.assertEqual(container_parts(data)['SHEX'], struct.pack('<I', 0x00010050))
        for bad in (data[:-1], bytes(32), data[:32] + struct.pack('<I', 4) + data[36:]):
            with self.assertRaises(ValueError):
                container_parts(bad)


if __name__ == '__main__':
    unittest.main()
