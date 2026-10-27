import ast
import fnmatch
from pathlib import Path
import struct
import unittest
from guarded_tables_report import analyze, validate_cache


class GuardedTablesTests(unittest.TestCase):
    def test_live_reader_resolves_every_guarded_export(self):
        tree=ast.parse(Path(__file__).with_name('read_preparation.py').read_text(encoding='utf-8'))
        calls=[node for node in ast.walk(tree) if isinstance(node,ast.Call) and isinstance(node.func,ast.Name) and node.func.id=='resolve']
        self.assertEqual(len(calls),1)
        patterns=[node.value for node in calls[0].args[2].elts]
        for name in ('Stats','Rows','Count','RowBytes','CacheStats'):
            self.assertTrue(any(fnmatch.fnmatchcase('CyberpunkVR_RetainedGuarded'+name,pattern) for pattern in patterns))

    def test_cache_shares_storage_without_omitting_either_plan_phase(self):
        stats,rows,source=self.fixture()
        commands=[[0]*32 for _ in rows]
        for command in commands:command[21]=1;command[28]=15
        validate_cache([100,20,10,20],[105,22,38,22],rows,commands)
        for before,after in (([100,20,9,20],[105,22,38,22]),([100,20,10,20],[105,22,37,22]),
                             ([100,20,10,20],[99,22,38,22]),([100,20,10,19],[105,22,38,22])):
            with self.assertRaises(ValueError):validate_cache(before,after,rows,commands)

    def fixture(self):
        sources=[];rows=[]
        for i in range(2):
            source=[0]*48;source[0]=i;source[1]=i;source[3]=7
            for field in range(6,13):source[field]=1000+field
            source[6]=100+i;source[7]=10+i;source[11]=200;source[43]=31
            sources.append(source)
            rows.append([i,i,100+i,10+i,i+1,200,1,0,15,2,2276,9,2048,1,1,0])
        stats=[2,7,2,2,2,4,4552,1,1,1,1,0,0,0,2**64-1,0]
        return stats,rows,dict(stats=[3,7,2,2,0,1,1,2,2,2,2,2,0,0,0,0],rows=sources,commands=2,direct=2,indirect=0)

    def test_all_sources_precede_payload_processing(self):
        stats,rows,source=self.fixture()
        report=analyze(stats,b''.join(struct.pack('<16Q',*r) for r in rows),source)
        self.assertTrue(report['constructedBeforeCpuReadback'])
        self.assertTrue(report['gpuChecksRequired'])
        self.assertFalse(report['sourceContentsCpuVerified'])

    def test_multilayer_requires_both_checks_and_exact_bytes(self):
        for field,value in ((9,1),(10,2272),(10,2280)):
            stats,rows,source=self.fixture();rows[0][field]=value
            stats[5]=sum(row[9] for row in rows)
            stats[6]=sum(row[10] for row in rows)
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                analyze(stats,b''.join(struct.pack('<16Q',*r) for r in rows),source)

    def test_scope_and_cpu_verification_cannot_be_fabricated(self):
        for field in range(16):
            stats,rows,source=self.fixture();stats[field]=(stats[field]+1)%(2**64)
            with self.subTest(stat=field),self.assertRaises(ValueError):analyze(stats,b''.join(struct.pack('<16Q',*r) for r in rows),source)
        for field in (0,1,2,3,4,5,6,7,13,14,15):
            stats,rows,source=self.fixture();rows[0][field]+=1
            with self.subTest(row=field),self.assertRaises(ValueError):analyze(stats,b''.join(struct.pack('<16Q',*r) for r in rows),source)
