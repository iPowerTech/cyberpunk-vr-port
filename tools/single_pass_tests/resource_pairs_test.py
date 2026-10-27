import unittest
from patch_resource_pairs import patch_pairs

SOURCE='''define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 1, i1 false)
  %2 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 0)
  %3 = extractvalue %dx.types.CBufRet.f32 %2, 0
  ret void
}
!dx.shaderModel = !{!1}
!dx.version = !{!2}
!dx.resources = !{!3}
!1 = !{!"vs", i32 6, i32 0}
!2 = !{i32 1, i32 0}
!3 = !{null, null, !4, null}
!4 = !{!5}
!5 = !{i32 0, %hostlayout.CameraShaderConsts* undef, !"", i32 0, i32 1, i32 1, i32 848, null}
!6 = !{void ()* @main, !"main", !7, !3, null}
'''


ALPHA_SOURCE='''define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 4, i1 false)
  %2 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %1, i32 0)
  %3 = extractvalue %dx.types.CBufRet.i32 %2, 0
  %4 = add i32 %3, 0
  %5 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 %4, i1 false)
  ret void
}
!dx.shaderModel = !{!1}
!dx.version = !{!2}
!dx.resources = !{!3}
!1 = !{!"ps", i32 6, i32 0}
!2 = !{i32 1, i32 0}
!3 = !{!8, null, !4, null}
!4 = !{!5}
!5 = !{i32 0, %ShaderSpecificConstants* undef, !"", i32 0, i32 4, i32 1, i32 292, null}
!6 = !{void ()* @main, !"main", !7, !3, null}
!8 = !{!9}
!9 = !{i32 0, [32768 x %Texture2D]* undef, !"", i32 1, i32 0, i32 32768, i32 2, i32 0, !10}
!10 = !{i32 0, i32 9}
'''


class ResourcePairTests(unittest.TestCase):
    def test_camera_is_a_pair_of_descriptors_not_a_doubled_buffer(self):
        ir,report=patch_pairs(SOURCE)
        self.assertIn('[2 x %hostlayout.CameraShaderConsts]* undef',ir)
        self.assertIn('i32 100, i32 2, i32 2, i32 848',ir)
        self.assertIn('i32 %cvr_pair_c2_r0, i1 true',ir)
        self.assertIn('i32 59, %dx.types.Handle %cvr1, i32 0',ir)
        self.assertEqual(report['pairs'][0]['sourceRegister'],1)

    def test_other_constants_follow_same_contract(self):
        source=SOURCE.replace('CameraShaderConsts','OtherConsts')
        ir,report=patch_pairs(source,101)
        self.assertEqual(report['pairs'][0]['space'],101)
        self.assertIn('OtherConsts',ir)

    def test_shared_bindings_do_not_gain_view_dependency(self):
        source=SOURCE.replace('!4 = !{!5}','!4 = !{!5, !8}')
        source=source.replace('  ret void','  %4 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 1, i32 5, i1 false)\n  ret void')
        source+='!8 = !{i32 1, %CommonConsts* undef, !"", i32 0, i32 5, i32 1, i32 112, null}\n'
        ir,report=patch_pairs(source,100,{('cbuffer',1)})
        self.assertEqual(len(report['pairs']),1)
        self.assertEqual(report['shared'][0]['register'],5)
        self.assertIn('i8 2, i32 1, i32 5, i1 false',ir)
        with self.assertRaises(ValueError):patch_pairs(source,100,{('srv',99)})

    def test_collision_reentry_and_dynamic_handles_rejected(self):
        with self.assertRaises(ValueError):patch_pairs(SOURCE,0)
        ir,_=patch_pairs(SOURCE)
        with self.assertRaises(ValueError):patch_pairs(ir)
        with self.assertRaises(ValueError):patch_pairs(SOURCE.replace('i32 0, i32 1, i1 false','i32 0, i32 %dynamic, i1 false'))
        with self.assertRaises(ValueError):patch_pairs(SOURCE.replace('i32 0, i32 1, i32 1, i32 848','i32 100, i32 1, i32 1, i32 848'))

    def test_srv_pair_preserves_texture_operation(self):
        source=SOURCE.replace('i8 2, i32 0, i32 1','i8 0, i32 0, i32 1')
        source=source.replace('!{null, null, !4, null}','!{!4, null, null, null}')
        source=source.replace('%hostlayout.CameraShaderConsts','%Texture2D')
        source=source.replace('i32 848, null','i32 2, i32 0, !8')
        source+='!8 = !{i32 0, i32 9}\n'
        _,report=patch_pairs(source)
        self.assertEqual(report['pairs'][0]['kind'],'srv')

    def test_alpha_material_load_makes_bindless_index_nonuniform(self):
        ir,report=patch_pairs(ALPHA_SOURCE,101,{('cbuffer',4)})
        self.assertIn('i8 0, i32 0, i32 %cvr4, i1 true',ir)
        self.assertIn('%cvr4 = add i32 %cvr3, 0',ir)
        self.assertIn('i32 1, i32 0, i32 32768',ir)
        self.assertIn('%cvr_pair_c2_r0 = add i32 %cvr_pair_view, 8',ir)
        self.assertNotIn('xor i32',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],1)
        self.assertTrue(report['arithmeticUnchanged'])

    def test_shared_dynamic_and_static_indices_stay_uniform(self):
        source=ALPHA_SOURCE.replace('!4 = !{!5}','!4 = !{!5, !11}')
        source+='!11 = !{i32 1, %CommonConsts* undef, !"", i32 0, i32 7, i32 1, i32 448, null}\n'
        source=source.replace('  ret void','''  %6 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 1, i32 7, i1 false)
  %7 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %6, i32 0)
  %8 = extractvalue %dx.types.CBufRet.i32 %7, 0
  %9 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 %8, i1 false)
  %10 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 23, i1 false)
  ret void''')
        ir,report=patch_pairs(source,101,{('cbuffer',4)})
        self.assertIn('i32 %cvr4, i1 true',ir)
        self.assertIn('i32 %cvr8, i1 false',ir)
        self.assertIn('i32 23, i1 false',ir)
        self.assertIn('i8 2, i32 1, i32 7, i1 false',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],1)
        ir,report=patch_pairs(ALPHA_SOURCE,101,set())
        self.assertIn('i32 %cvr4, i1 false',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],0)

    def test_existing_nonuniform_flag_is_preserved(self):
        source=ALPHA_SOURCE.replace('i32 %4, i1 false','i32 %4, i1 true')
        ir,report=patch_pairs(source)
        self.assertIn('i32 %cvr4, i1 true',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],0)

    def test_select_condition_and_value_dependencies(self):
        for expression in ('select i1 %gate, i32 8, i32 9',
                           'select i1 true, i32 %3, i32 9'):
            source=ALPHA_SOURCE.replace('  %4 = add i32 %3, 0',
                '  %gate = icmp ne i32 %3, 0\n  %4 = '+expression)
            ir,report=patch_pairs(source)
            self.assertIn('i32 %cvr4, i1 true',ir)
            self.assertEqual(report['transitiveNonuniformHandles'],1)

    def test_phi_tracks_view_control_without_tainting_later_uniform_control(self):
        source=ALPHA_SOURCE.replace('  %4 = add i32 %3, 0','''  %gate = icmp ne i32 %3, 0
  br i1 %gate, label %left, label %right
left:
  br label %merge
right:
  br label %merge
merge:
  %4 = phi i32 [ 8, %left ], [ 9, %right ]''')
        source=source.replace('  ret void','''  br i1 true, label %later_left, label %later_right
later_left:
  br label %later_merge
later_right:
  br label %later_merge
later_merge:
  %uniform = phi i32 [ 12, %later_left ], [ 13, %later_right ]
  %unused = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 %uniform, i1 false)
  ret void''')
        ir,report=patch_pairs(source)
        self.assertIn('i32 %cvr4, i1 true',ir)
        self.assertIn('i32 %uniform, i1 false',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],1)
        ir,report=patch_pairs(source.replace('[ 9, %right ]','[ 8, %right ]'))
        self.assertIn('i32 %cvr4, i1 false',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],0)

    def test_loop_phi_reaches_fixed_point_from_a_later_definition(self):
        source=ALPHA_SOURCE.replace('  %4 = add i32 %3, 0','''  br label %loop
loop:
  %4 = phi i32 [ 0, %cvr0 ], [ %next, %loop ]
  %next = add i32 %4, %3''')
        source=source.replace('  ret void','''  %done = icmp uge i32 %4, 16
  br i1 %done, label %exit, label %loop
exit:
  ret void''')
        ir,report=patch_pairs(source)
        self.assertIn('i32 %cvr4, i1 true',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],1)

    def test_switch_controls_constant_phi_indices(self):
        source=ALPHA_SOURCE.replace('  %4 = add i32 %3, 0','''  switch i32 %3, label %other [
    i32 0, label %zero
  ]
zero:
  br label %merge
other:
  br label %merge
merge:
  %4 = phi i32 [ 8, %zero ], [ 9, %other ]''')
        ir,report=patch_pairs(source)
        self.assertIn('i32 %cvr4, i1 true',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],1)

    def test_view_dependent_memory_flow_fails_closed(self):
        source=ALPHA_SOURCE.replace('  ret void','  store i32 %3, i32* %address\n  ret void')
        ir,report=patch_pairs(source)
        self.assertIn('i32 %cvr4, i1 true',ir)
        self.assertEqual(report['transitiveNonuniformHandles'],1)
        ir,_=patch_pairs(source.replace('i32 %4, i1 false','i32 23, i1 false'))
        self.assertIn('i32 23, i1 false',ir)
        through_memory=source.replace('  %4 = add i32 %3, 0','  %4 = load i32, i32* %address')
        with self.assertRaisesRegex(ValueError,'memory stores'):patch_pairs(through_memory)
        source=ALPHA_SOURCE.replace('  %4 = add i32 %3, 0','''  %gate = icmp ne i32 %3, 0
  br i1 %gate, label %left, label %right
left:
  store i32 8, i32* %address
  br label %merge
right:
  store i32 9, i32* %address
  br label %merge
merge:
  %4 = load i32, i32* %address''')
        with self.assertRaisesRegex(ValueError,'memory stores'):
            patch_pairs(source)


if __name__=='__main__':unittest.main()
