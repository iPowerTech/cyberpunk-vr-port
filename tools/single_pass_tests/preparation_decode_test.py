import struct
import unittest
import ast
from pathlib import Path
from read_preparation import decode, analyze, restore_controls, eye_copy_order,proxy_preparation,FIELDS
from unittest.mock import Mock


class PreparationDecodeTests(unittest.TestCase):
    def test_producer_capture_does_not_use_later_local_snapshot_helper(self):
        tree=ast.parse(Path(__file__).with_name('read_preparation.py').read_text(encoding='utf-8-sig'))
        targets=[node for node in ast.walk(tree) if isinstance(node,ast.Assign) and
                 any(isinstance(target,ast.Name) and target.id=='producer_raw' for target in node.targets)]
        self.assertEqual(len(targets),1)
        self.assertFalse(any(isinstance(node,ast.Name) and node.id=='snapshot_buffer' for node in ast.walk(targets[0])))

    def output_trace(self, same_list=False):
        depth, copy = 100, 100 if same_list else 200
        def event(sequence,kind,command_list,scope=1,**kw):
            return dict(sequence=sequence,kind=kind,context=command_list,output=command_list,
                        input=300,scope=scope,side=1,view=400,frame=1,stage=3,flags0=1,**kw)
        return [event(1,13,depth),event(2,14,depth),event(3,12,copy),
                event(4,15,depth),event(5,15,copy,2)] if not same_list else [
                event(1,13,depth),event(2,14,depth),event(3,12,copy),event(4,15,depth)]

    def test_depth_to_copy_order_uses_submissions_not_cpu_recording_order(self):
        rows=self.output_trace()
        rows[1]['sequence'],rows[2]['sequence']=3,2
        rows.sort(key=lambda r:r['sequence'])
        report=eye_copy_order(rows,1,400)
        self.assertTrue(report['passed'])
        self.assertEqual(report['checks'][0]['reason'],'separate-ordered-submissions')

    def test_extra_color_scopes_do_not_change_depth_copy_check(self):
        rows=self.output_trace()
        rows.extend((dict(rows[0],sequence=6,stage=4,scope=3),dict(rows[1],sequence=7,stage=4,scope=3)))
        report=eye_copy_order(rows,1,400)
        self.assertTrue(report['passed']);self.assertEqual(report['depthLists'],1)

    def test_same_recording_requires_copy_after_depth(self):
        rows=self.output_trace(True)
        self.assertTrue(eye_copy_order(rows,1,400)['passed'])
        rows[1]['sequence'],rows[2]['sequence']=3,2
        rows.sort(key=lambda r:r['sequence'])
        self.assertFalse(eye_copy_order(rows,1,400)['passed'])

    def test_same_batch_and_different_queues_are_not_implicitly_ordered(self):
        for change in ('scope','input'):
            rows=self.output_trace()
            rows[-1][change]=1 if change=='scope' else 999
            self.assertFalse(eye_copy_order(rows,1,400)['passed'])

    def test_reversed_submission_unknown_source_and_resets_do_not_pass(self):
        rows=self.output_trace()
        rows[-2]['sequence'],rows[-1]['sequence']=5,4
        rows.sort(key=lambda r:r['sequence'])
        self.assertFalse(eye_copy_order(rows,1,400)['passed'])
        for change,value in (('side',0),('view',999),('flags0',0)):
            rows=self.output_trace();rows[2][change]=value
            self.assertFalse(eye_copy_order(rows,1,400)['passed'])
        rows=self.output_trace();reset=dict(rows[1],kind=16,sequence=3)
        for row in rows[2:]:row['sequence']+=1
        rows.insert(2,reset)
        self.assertFalse(eye_copy_order(rows,1,400)['passed'])

    def test_unsubmitted_or_replayed_recordings_do_not_pass(self):
        rows=self.output_trace()
        self.assertFalse(eye_copy_order(rows[:-1],1,400)['passed'])
        rows.append(dict(rows[-1],sequence=6,scope=3))
        self.assertFalse(eye_copy_order(rows,1,400)['passed'])

    def test_owned_ranges_and_native_stage_comparison(self):
        records = []
        for eye in (1, 0):
            for kind in (1, 2, 3, 4, 5, 6):
                sequence = len(records)+1
                records.append((sequence, sequence*100, 1+eye if kind != 6 else 0, 10, 20+eye, 30, 40, 50,
                                1, kind, eye, 1, 3, 65536, 0, 0, 0, 0 if kind == 1 else 2, 0, 0))
        raw = b''.join(struct.pack('<8Q2Ii9I', *r) for r in records)
        packets = struct.pack('<4Q', 1, 2, 3, 4)
        rows = decode(raw, packets)
        summary = analyze(rows, packets, 1000000)
        self.assertEqual(len(summary['completedStages']), 2)
        self.assertTrue(all(c['equal'] and c['commonPackets'] == 2 for c in summary['comparisons']))
        self.assertEqual(summary['consumes'][1]['matchingStageEnds'], [11])
        bad = bytearray(raw)
        struct.pack_into('<I', bad, 100, 100)
        with self.assertRaises(ValueError):
            decode(bad, packets)

    def test_invalid_abi_does_not_pass(self):
        with self.assertRaises(ValueError):
            decode(bytes(111), b'')
        with self.assertRaises(ValueError):
            decode(b'', bytes(1))

    def test_extended_camera_frames_require_explicit_decode_limit(self):
        row=dict.fromkeys(FIELDS,0);row.update(sequence=1,frame=8,kind=9,side=0)
        raw=struct.pack('<8Q2Ii9I',*(row[name] for name in FIELDS))
        with self.assertRaises(ValueError):decode(raw,b'')
        self.assertEqual(decode(raw,b'',8)[0]['frame'],8)

    def test_materialization_masks_only_defined_context_fields(self):
        row=dict.fromkeys(FIELDS,0)
        row.update(sequence=1,qpc=100,scope=7,context=10,view=20,storage=30,input=30,output=40,
                   frame=1,kind=18,side=1,thread=2,flags0=32,flags1=0xD2,mode=0x201)
        rows=[row,dict(row,sequence=2,qpc=200,kind=19)]
        report=analyze(rows,b'',1000000)['materialization'][0]
        self.assertEqual(report['workFlags'],2)
        self.assertEqual(report['viewIndex'],1)
        self.assertEqual(report['workFlagsRaw'],0xD2)
        self.assertEqual(report['viewWordRaw'],0x201)
        self.assertEqual(report['inputStorage'],report['storage'])

    def test_proxy_pairs_preserve_multiplicity_and_method_identity(self):
        data=struct.pack('<24Q',10,100,1000,1001,10,100,1000,1001,20,100,1000,1001,
                         10,100,1000,1001,20,200,2000,2001,30,300,3000,3001)
        row=dict.fromkeys(FIELDS,0)
        row.update(sequence=1,qpc=100,scope=1,context=10,view=20,storage=30,input=40,output=30,
                   frame=1,kind=20,side=0,thread=2,count=6,flags0=3,reserved=2)
        peer=dict(row,sequence=3,scope=2,side=1,offset=6)
        rows=[row,dict(row,sequence=2,kind=21),peer,dict(peer,sequence=4,kind=21)]
        raw=b''.join(struct.pack('<8Q2Ii9I',*(r[k] for k in FIELDS)) for r in rows)
        report=proxy_preparation(decode(raw,data),data)
        self.assertEqual(report['incomplete'],[])
        self.assertTrue(report['typed'])
        self.assertEqual(report['frames'][0]['calls'],[3,3])
        self.assertEqual(report['frames'][0]['common'],1)
        self.assertEqual(report['frames'][0]['families'][0],dict(method='0x64',vtable='0x3e8',update='0x3e9',main=3,peer=1,common=1))
        for at in (16,24):
            changed=bytearray(data);struct.pack_into('<Q',changed,96+at,9999)
            self.assertEqual(proxy_preparation(rows,changed)['frames'][0]['common'],0)
        for field in ('frame','storage','input','thread','count','offset'):
            changed=[dict(r) for r in rows];changed[1][field]+=1
            self.assertEqual(proxy_preparation(changed,data)['incomplete'],[1],field)
        self.assertEqual(proxy_preparation(rows[:1],data)['incomplete'],[1])
        with self.assertRaises(ValueError):proxy_preparation(rows[1:],data)
        legacy=[dict(r,reserved=0,flags0=0,count=3,offset=0 if r['side']==0 else 3) for r in rows]
        self.assertFalse(proxy_preparation(legacy,struct.pack('<12Q',10,100,10,100,20,100,10,100,20,200,30,300))['typed'])

    def test_restore_does_not_mask_crash_or_skip_live_controls(self):
        write = Mock(side_effect=OSError('process disappeared'))
        self.assertEqual(restore_controls(write, lambda: True), 'process_exited')
        write.assert_called_once_with('Request', 0)
        write = Mock(side_effect=[OSError('live write failed'), None, None, None])
        with self.assertRaisesRegex(OSError, 'live write failed'):
            restore_controls(write, lambda: False)
        self.assertEqual(write.call_count, 4)

    def test_camera_controls_also_restore_after_another_write_fails(self):
        write=Mock(side_effect=[OSError('failed'),None,None,None,None]);camera=Mock()
        with self.assertRaises(OSError):restore_controls(write,lambda:False,camera_write=camera)
        self.assertEqual(write.call_count,5)
        self.assertEqual([c.args for c in camera.call_args_list],[('Request',0),('State',0),('NodeFilter',0)])


if __name__ == '__main__':
    unittest.main()
