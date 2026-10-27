import copy
import struct
import unittest
from unittest import mock
import fnmatch
from source_tables_report_test import fixture,table_stats,packed
from source_tables_report import analyze as table_analyze
from native_descriptor_report import analyze


def sample():
    source,cbvs,commands,tables=fixture()
    report=table_analyze(table_stats(commands,tables,source),packed(commands,24),packed(tables,16),source,cbvs)
    rows=[]
    for index,s in enumerate(source['rows']):
        finite=[t for t in tables if t[0]==index and not t[14]]
        count=sum(t[3] for t in finite);mask=sum(1<<t[2] for t in finite)
        rows.append([index,s[1],1,0,count,len(finite),5000,64,256,index*32,index*32+count,s[6],s[7],s[13]&~mask,1,1])
    return rows,source,report


def check(data):
    rows,source,report=data
    return analyze(b''.join(struct.pack('<16Q',*r) for r in rows),source,report)


def cached_sample():
    source,cbvs,commands,tables=fixture(3)
    report=table_analyze(table_stats(commands,tables,source),packed(commands,24),packed(tables,16),source,cbvs)
    rows=[];entries=[];first={};stride=report['resourceDescriptorStrideFromCamera']
    for ordinal,s in enumerate(source['rows']):
        finite=[t for t in tables if t[0]==ordinal and not t[14]];total=sum(t[3] for t in finite);fresh=0
        before=8 if not ordinal else 24
        for table in finite:
            if ordinal:gpu=first[table[2]]
            else:
                gpu=0x800000+(64+before+fresh)*stride;first[table[2]]=gpu;fresh+=table[3]
            entries.append([ordinal,table[2],table[3],table[4],gpu,0,table[2],int(ordinal>0)])
        rows.append([ordinal,s[1],1,0,total,len(finite),5000,64,256,before,before+fresh,s[6],s[7],s[13]&~sum(1<<t[2] for t in finite),1,1])
    return rows,source,report,entries


def cached_check(data):
    rows,source,report,entries=data
    return analyze(packed(rows,16),source,report,packed(entries,8))


class NativeDescriptorReportTests(unittest.TestCase):
    def test_native_nonblocking_ring_reservation_preserves_context_and_reuse(self):
        rows,source,report,entries=cached_sample();count=rows[0][4]
        rows[0][2]=2
        rows[0][7:11]=[72,count,0,count]
        ring=[0,0x100000,0x100000+0x149DE40,5000,128,70,0,72+count,0,72,count,72,3,2,1,1]
        run=lambda data:analyze(packed(rows,16),source,report,packed(entries,8),packed(data,16))
        result=run([ring]);self.assertEqual(result['ringReservations'],1);self.assertEqual(result['ringDescriptors'],count)
        for field,value in ((0,99),(1,0),(2,0),(3,9),(4,0),(5,90),(6,80),(7,80),(8,80),(9,127),(10,0),(11,73),(12,0),(13,0),(14,0),(15,0)):
            bad=ring.copy();bad[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):run([bad])
        with self.assertRaises(ValueError):run([ring,ring])
        with self.assertRaises(ValueError):run([])

    def test_reuse_keeps_original_allocation_with_zero_new_reservation(self):
        data=cached_sample();result=cached_check(data)
        self.assertEqual(result['allocatedDescriptors'],data[0][0][4])
        self.assertEqual(result['reusedDescriptors'],data[0][0][4]*2)
        self.assertTrue(result['allSourcesBound'])

    def test_reuse_rejects_foreign_recording_or_forged_table(self):
        data=cached_sample();at=next(i for i,e in enumerate(data[3]) if e[0]==1)
        for field,value in ((0,99),(1,99),(2,1),(3,0),(4,0),(5,1),(6,99),(7,0)):
            bad=copy.deepcopy(data);bad[3][at][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):cached_check(bad)
        bad=copy.deepcopy(data);bad[0][1][11]+=10;bad[1]['rows'][1][6]+=10
        with self.assertRaises(ValueError):cached_check(bad)
        bad=copy.deepcopy(data);bad[0][1][6]+=1
        with self.assertRaises(ValueError):cached_check(bad)
        bad=copy.deepcopy(data);bad[3].pop()
        with self.assertRaises(ValueError):cached_check(bad)

    def test_reader_resolves_native_table_exports_before_launch_capture(self):
        import read_preparation
        with mock.patch('live_symbols.resolve',side_effect=RuntimeError('symbol-boundary')) as resolve:
            with self.assertRaisesRegex(RuntimeError,'symbol-boundary'):
                read_preparation.main(['--pid','123','--out','unused.json'])
        patterns=resolve.call_args.args[2]
        for name in ('CyberpunkVR_RetainedNativeTableRows','CyberpunkVR_RetainedNativeTableCount','CyberpunkVR_RetainedNativeTableRowBytes'):
            self.assertTrue(any(fnmatch.fnmatchcase(name,p) for p in patterns))

    def test_material_tables_remain_on_original_heap_with_owned_new_ranges(self):
        result=check(sample())
        self.assertTrue(result['allSourcesBound'])
        self.assertTrue(result['borrowedOriginalTables'])
        self.assertFalse(result['laterFrameReuse'])
        self.assertFalse(result['nativeExecutionPermitted'])

    def test_stale_metadata_omission_and_wrapping_fail(self):
        for field,value in ((0,9),(1,1),(2,2),(3,4),(4,1),(5,0),(6,0),(7,1048576),(8,1),(9,1),(10,0),(11,0),(12,0),(13,0),(14,0),(15,0)):
            data=copy.deepcopy(sample());data[0][0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):check(data)
        data=copy.deepcopy(sample());data[0].pop()
        with self.assertRaises(ValueError):check(data)

    def test_capacity_deferral_is_explicit_not_full_coverage(self):
        data=copy.deepcopy(sample());row=data[0][0];row[2:11]=[0,7]+[0]*7;row[13:]=[0]*3
        result=check(data);self.assertEqual(result['deferred'],1);self.assertFalse(result['allSourcesBound'])
        row[3]=8
        with self.assertRaises(ValueError):check(data)
