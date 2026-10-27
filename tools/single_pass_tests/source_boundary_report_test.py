import copy
import unittest
from source_tables_report_test import fixture,packed
from source_boundary_report import analyze,submission


def sample():
    source,_,_,_=fixture();rows=[]
    for order,eye in enumerate((1,0)):
        s=next(s for s in source['rows'] if s[1]==eye)
        rows.append([source['stats'][1],eye,order,s[6],s[7],1,order+1,order,order,order,
                     800 if order else 0,900 if order else 0,order,0,1,1,800,1,1,0])
    return source,rows


class SourceBoundaryTests(unittest.TestCase):
    def test_actual_submission_not_cpu_seal_proves_the_dependency(self):
        source,rows=sample();rows[1][8:14]=[0,0,0,900,0,0]
        boundary=analyze(packed(rows,20),source)
        peer,main=rows
        values=[main[0],800,*peer[3:5],*main[3:5],1,2,8,1,0,1,1,1,2,1]
        self.assertFalse(boundary['producerOrderOpportunity'])
        self.assertTrue(submission(values,boundary,required=True)['eligible'])
        for field,value in ((0,99),(1,0),(2,0),(3,0),(4,0),(5,0),(6,3),(7,8),(8,0),(9,0),(10,1),(11,0),(12,0),(13,0),(14,0),(15,0)):
            bad=values.copy();bad[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):submission(bad,boundary,required=True)
        values[14:]=[0,0]
        self.assertFalse(submission(values,boundary)['eligible'])

    def test_cpu_readiness_does_not_claim_gpu_completion(self):
        source,rows=sample();result=analyze(packed(rows,20),source,require_cbvs=True)
        self.assertEqual(result['lastEye'],0)
        self.assertTrue(result['producerOrderOpportunity'])
        self.assertFalse(result['peerGpuAlreadyComplete'])
        self.assertFalse(result['nativeExecutionPermitted'])

    def test_unsubmitted_or_foreign_queue_does_not_create_an_edge(self):
        source,rows=sample();rows[1][10]=801
        self.assertFalse(analyze(packed(rows,20),source)['producerOrderOpportunity'])
        rows[1][8:14]=[0,0,0,900,0,0]
        self.assertFalse(analyze(packed(rows,20),source)['producerOrderOpportunity'])

    def test_torn_boundaries_or_postreadback_snapshot_fail(self):
        source,rows=sample()
        for field,value in ((0,99),(1,1),(2,0),(3,0),(4,0),(5,0),(6,1),(7,0),(8,2),(9,0),(10,0),(11,0),(12,0),(13,2**64-1),(16,0),(17,0),(19,1)):
            bad=copy.deepcopy(rows);bad[1][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):analyze(packed(bad,20),source,require_cbvs=True)
