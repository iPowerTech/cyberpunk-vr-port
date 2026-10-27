"""Compare typed descriptors/timeline ranges for unambiguous geometry pairs.

Identical descriptors do not prove identical resource contents or GPU lifetimes.
"""
import argparse
from collections import Counter,defaultdict
import json
from pathlib import Path
import struct


def load(path):
    meta=json.loads(path.read_text())
    if meta['failure']:raise ValueError('Descriptor capture reports failure')
    data={name:(path.parent/file).read_bytes() for name,file in meta['files'].items()}
    for name,unit in [('Draws',16),('Bindings',64),('Ids',4),('Values',160)]:
        if len(data[name])%unit:raise ValueError('Truncated descriptor block')
    values=list(struct.iter_unpack('<20Q',data['Values']))
    ids=list(struct.unpack('<'+'I'*(len(data['Ids'])//4),data['Ids']))
    if any(i>=len(values) for i in ids):raise ValueError('Descriptor value ID outside dictionary')
    bindings=[]
    for item in struct.iter_unpack('<8I3Q2I',data['Bindings']):
        p,kind,space,reg,count,offset,at,n,gpu,heap,version,status,visibility=item
        if at+n>len(ids) or status>2 or (status and (not heap or not gpu or not count)):
            raise ValueError('Invalid descriptor range')
        if status==2 and (n!=count or any(not v for v in ids[at:at+n])):
            raise ValueError('Known descriptor range contains unknown slots')
        bindings.append(dict(key=(p,kind,space,reg,count,offset,visibility),kind=kind,space=space,
                             register=reg,count=count,ids=ids[at:at+n],gpu=gpu,heap=heap,version=version,status=status))
    draws={}
    for sequence,at,n,complete in struct.iter_unpack('<4I',data['Draws']):
        if sequence in draws or at+n>len(bindings):raise ValueError('Invalid draw binding span')
        draws[sequence]=dict(complete=complete==3,bindings={r['key']:r for r in bindings[at:at+n]})
    return meta,draws,values


def compare_range(a,b):
    if a['key']!=b['key']:return 'layout_mismatch'
    if a['status'] and b['status'] and a['heap'] and a['gpu'] and (a['heap'],a['gpu'],a['version'])==(b['heap'],b['gpu'],b['version']):
        return 'unchanged_range'
    if (a['status']==2 and b['status']==2 and len(a['ids'])==a['count'] and len(b['ids'])==b['count']
            and all(a['ids']) and a['ids']==b['ids']):return 'equal_descriptions'
    return 'compare_slots'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('descriptors',type=Path);parser.add_argument('geometry',type=Path);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();meta,draws,values=load(args.descriptors);geometry=json.loads(args.geometry.read_text())
    if meta['pid']!=geometry['pid']:raise ValueError('Different process captures')
    totals=Counter();by_register=defaultdict(Counter);examples={};pair_rows=[]
    for pair in geometry['mappedPairs']:
        if pair['ambiguous'] or len(pair['draws'])!=1:totals['ambiguousSkipped']+=1;continue
        left,right=pair['draws'][0];a,b=draws[left],draws[right]
        if not a['complete'] or not b['complete']:totals['incompleteDraws']+=1;continue
        if a['bindings'].keys()!=b['bindings'].keys():totals['differentLayouts']+=1;continue
        result=Counter()
        for key,x in a['bindings'].items():
            y=b['bindings'][key];comparison=compare_range(x,y);result[comparison]+=1
            if comparison in ('unchanged_range','equal_descriptions'):continue
            if len(x['ids'])!=x['count'] or len(y['ids'])!=y['count']:
                result['unexpandedChangedRange']+=1;continue
            for i,(u,v) in enumerate(zip(x['ids'],y['ids'])):
                label=f"param{key[0]}:type{x['kind']}:space{x['space']}:reg{x['register']+i}"
                if not u or not v:state='unknown'
                elif u==v:state='equal'
                elif values[u][0]==values[v][0]==1:state='cbv_address_or_size_diff'
                elif values[u][2:4]!=values[v][2:4]:state='resource_identity_diff'
                else:state='view_description_diff'
                by_register[label][state]+=1;result[state]+=1
                if state not in ('unknown','equal') and label not in examples:
                    examples[label]=dict(draws=[left,right],a=list(values[u]),b=list(values[v]))
        totals['uniquePairs']+=1
        if not result['compare_slots']:totals['allDescriptorRangesEqual']+=1
        else:totals['pairsWithUnresolvedDifferences']+=1
        pair_rows.append(dict(packets=pair['packets'],draws=[left,right],summary=dict(result)))
    report=dict(pid=meta['pid'],totals=dict(totals),registers=dict(by_register),examples=examples,pairs=pair_rows,
                runtimeEligible=False,note='Descriptor equivalence only. CBV data, mutable textures/UAVs, camera replacement and ownership still require validation.')
    args.out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(pid=meta['pid'],totals=report['totals'],registers=report['registers']),indent=2))


if __name__=='__main__':main()
