"""Select per-eye CBV/SRV descriptor pairs without changing shader arithmetic.

Offline SM6.0 -> SM6.1 transformation. Requires matching private root tables:
original space-0 register N becomes registers 2*N..2*N+1 in the requested space.
Bindless arrays, samplers and UAV bindings need their own audit. Dependent
descriptor indices are marked nonuniform when paired reads can vary by view.
Run DXC viewid-state, assembly and validation before using any output.
"""
import argparse
import json
from pathlib import Path
import re

from batch_shaders import classify, metadata
from upgrade_stage import upgrade


LOCAL = r'%[-\w.$]+'


def mark_dependent_handles(source):
    body = re.findall(r'(?ms)^define void @[\w.]+\(\) \{\n(.*?)^\}', source)
    if len(body) != 1:
        raise ValueError('Expected one graphics body for descriptor dependency analysis')
    blocks = {}
    current = None
    for line in body[0].splitlines():
        text = line.split(';', 1)[0].strip()
        if not text:
            continue
        label = re.fullmatch(r'([-\w.$]+):', text)
        if label:
            current = label[1]
            if current in blocks:
                raise ValueError('Duplicate shader block')
            blocks[current] = []
        elif current is None:
            raise ValueError('Missing shader entry block')
        elif blocks[current] and blocks[current][-1].startswith('switch ') and ']' not in blocks[current][-1]:
            blocks[current][-1] += ' ' + text
        else:
            blocks[current].append(text)
    definitions, successors, conditions, phis = {}, {}, {}, {}
    for block, lines in blocks.items():
        if not lines:
            raise ValueError('Empty shader block')
        for line in lines:
            definition = re.fullmatch('(' + LOCAL + r') = (.+)', line)
            if not definition:
                continue
            name, instruction = definition.groups()
            if name in definitions:
                raise ValueError('Duplicate shader SSA definition')
            definitions[name] = set(re.findall(LOCAL, instruction))
            if instruction.startswith('phi '):
                incoming = re.findall(r'\[\s*(.*?),\s*%([-\w.$]+)\s*\]', instruction)
                if not incoming:
                    raise ValueError('Unsupported shader phi')
                phis[name] = (block, incoming)
        terminator = lines[-1]
        if terminator.startswith(('br ', 'switch ')):
            targets = set(re.findall(r'label %([-\w.$]+)', terminator))
            if not targets or not targets <= blocks.keys():
                raise ValueError('Unsupported shader branch targets')
            successors[block] = targets
            condition = re.match(r'(?:br i1|switch i\d+) (' + LOCAL + r'),', terminator)
            if condition:
                conditions[block] = condition[1]
        elif terminator.startswith(('ret ', 'unreachable')):
            successors[block] = {None}
        else:
            raise ValueError('Unsupported shader terminator')
    # A phi can vary through control flow even when all incoming values are
    # constants. Bound each branch's region at its immediate postdominator.
    postdominators = {block: set(blocks) | {None} for block in blocks}
    postdominators[None] = {None}
    changed = True
    while changed:
        changed = False
        for block, targets in successors.items():
            result = {block} | set.intersection(*(postdominators[target] for target in targets))
            if result != postdominators[block]:
                postdominators[block] = result
                changed = True
    controlled_by = {block: set() for block in blocks}
    for block, condition in conditions.items():
        strict = postdominators[block] - {block}
        nearest = [candidate for candidate in strict if strict - {candidate} <= postdominators[candidate]]
        if len(nearest) != 1:
            raise ValueError('Unsupported shader control-flow convergence')
        join = nearest[0]
        region, pending = set(), list(successors[block])
        while pending:
            target = pending.pop()
            if target is None or target == join or target in region:
                continue
            region.add(target)
            pending.extend(successors[target])
        for target in region:
            controlled_by[target].add(condition)
        for name, (phi_block, incoming) in phis.items():
            if len({value.strip() for value, _ in incoming}) > 1 and (
                    phi_block == join or any(predecessor in region for _, predecessor in incoming)):
                definitions[name].add(condition)
    dependent = {'%cvr_pair_view'}
    changed = True
    while changed:
        changed = False
        for name, inputs in definitions.items():
            if name not in dependent and inputs & dependent:
                dependent.add(name)
                changed = True
    handle = re.compile(r'(@dx.op.createHandle\(i32 57, i8 \d+, i32 \d+, i32 )('
                        + LOCAL + r'|-?\d+)(, i1 )(false|true)(\))')
    unresolved_indices = any(match[2].startswith('%') and match[2] not in dependent and match[4]=='false'
                             for match in handle.finditer(body[0]))
    # Memory flow only needs further analysis when an unmarked dynamic handle
    # remains. Static handles and indices already proved divergent are safe.
    if unresolved_indices:
        for block, lines in blocks.items():
            for line in lines:
                if line.startswith('store ') and (set(re.findall(LOCAL, line)) | controlled_by[block]) & dependent:
                    raise ValueError('View-dependent memory stores need a descriptor dependency audit')
    changed_handles = 0

    def replace_handle(match):
        nonlocal changed_handles
        if match[2] not in dependent or match[4] == 'true':
            return match[0]
        changed_handles += 1
        return match[1] + match[2] + match[3] + 'true' + match[5]

    for line in body[0].splitlines():
        if '@dx.op.createHandle(' in line and not handle.search(line):
            raise ValueError('Unsupported descriptor handle for dependency analysis')
    return handle.sub(replace_handle, source), changed_handles


def patch_pairs(source, space=100, selected=None):
    info=classify(source)
    if info['stage'] not in ('vs','ps') or not 1<=space<65536:
        raise ValueError('Only VS/PS and a nonzero private register space are supported')
    if any(r['space']==space for r in info['resources']):
        raise ValueError('Private register space overlaps original resources')
    if '@dx.op.viewID.' in source or 'cvr_pair_' in source:
        raise ValueError('Already transformed shader')
    source=re.sub(r'%(\d+)\b',lambda m:'%cvr'+m[1],source)
    source=re.sub(r'(?m)^(\d+):',lambda m:'cvr'+m[1]+':',source)
    source=re.sub(r'(?m)^; <label>:(\d+)\s*(;[^\n]*)?$',lambda m:'cvr'+m[1]+': '+(m[2] or ''),source)
    source=upgrade(source)
    original_ir=source
    md=metadata(source)
    lists=md.get(md.get('!dx.resources'),'').split(', ')
    pairs=[];instructions=[]
    for kind,class_id in (('srv',0),('cbuffer',2)):
        if len(lists)!=4 or lists[class_id]=='null':continue
        for node in md[lists[class_id]].split(', '):
            match=re.fullmatch(r'i32 (\d+), (.+)\* undef, (!"[^"]*"), i32 (\d+), i32 (\d+), i32 (\d+), (.*)',md[node])
            if not match:raise ValueError('Unsupported resource metadata')
            resource_id,type_name,name,old_space,register,count,tail=match.groups()
            if selected is not None and (kind,int(register)) not in selected:continue
            if int(old_space)!=0 or int(count)!=1:continue
            register=int(register);destination=register*2
            if destination>=0xfffffffe:raise ValueError('Register overflow')
            handle=re.compile(r'(@dx.op.createHandle\(i32 57, i8 '+str(class_id)+r', i32 '+resource_id+r', i32 )'+str(register)+r'(, i1 )(?:false|true)(\))')
            all_handles=re.findall(r'@dx.op.createHandle\(i32 57, i8 '+str(class_id)+r', i32 '+resource_id+r',',source)
            index=f'%cvr_pair_c{class_id}_r{resource_id}'
            source,n=handle.subn(lambda m:m[1]+index+m[2]+'true'+m[3],source)
            if n!=len(all_handles) or not n:raise ValueError('Non-static or missing original resource handle')
            original=node+' = !{'+md[node]+'}'
            replacement=f'{node} = !{{i32 {resource_id}, [2 x {type_name}]* undef, {name}, i32 {space}, i32 {destination}, i32 2, {tail}}}'
            if source.count(original)!=1:raise ValueError('Resource declaration not unique')
            source=source.replace(original,replacement)
            instructions.append(f'  {index} = add i32 %cvr_pair_view, {destination}')
            pairs.append(dict(kind=kind,id=int(resource_id),sourceSpace=0,sourceRegister=register,
                              space=space,register=destination,count=2,type=type_name,handles=n))
    paired={(p['kind'],p['sourceRegister']) for p in pairs}
    if selected is not None and selected-paired:raise ValueError('Requested binding is absent or unsupported')
    shared=[r for r in info['resources'] if r['space']!=0 or (r['kind'],r['register']) not in paired]
    if not pairs:return source,dict(stage=info['stage'],pairs=[],shared=shared,arithmeticUnchanged=True,
                                   transitiveNonuniformHandles=0)
    entries=re.findall(r'(?m)^define void @([\w.]+)\(\) \{$',source)
    if len(entries)!=1:raise ValueError('Expected one graphics entry')
    entry='define void @'+entries[0]+'() {'
    source=source.replace(entry,entry+'\ncvr0:\n  %cvr_pair_view = call i32 @dx.op.viewID.i32(i32 138)\n'+'\n'.join(instructions),1)
    source+='\ndeclare i32 @dx.op.viewID.i32(i32) nounwind readnone\n'
    props=re.search(r'(?m)^!\d+ = !\{void \(\)\* @'+re.escape(entries[0])+r', !"[^"]*", !\d+, !\d+, (!\d+|null)\}$',source)
    if not props:raise ValueError('Unsupported entry properties')
    if props[1]=='null':
        new='!'+str(max(map(int,re.findall(r'(?m)^!(\d+) =',source)))+1)
        source=source.replace(props[0],props[0][:-5]+new+'}')
        source+='\n'+new+' = !{i32 0, i64 '+str(1<<21)+'}\n'
    else:
        flags=re.search(r'(?m)^'+re.escape(props[1])+r' = !\{(.*)\}$',source)
        changed,n=re.subn(r'i32 0, i64 (\d+)',lambda m:'i32 0, i64 '+str(int(m[1])|(1<<21)),flags[1])
        if n!=1:raise ValueError('Unsupported shader flags')
        source=source.replace(flags[0],props[1]+' = !{'+changed+'}')
    source,dependent_handles=mark_dependent_handles(source)
    def arithmetic(text):
        return [line for line in text.splitlines() if line.strip() and not line.startswith(('!',';'))
                and 'dx.op.createHandle' not in line and 'dx.op.viewID' not in line
                and 'cvr_pair_' not in line and line!='cvr0:']
    if arithmetic(source)!=arithmetic(original_ir):raise ValueError('Non-binding shader instructions changed')
    return source,dict(stage=info['stage'],pairs=pairs,shared=shared,arithmeticUnchanged=True,
                       transitiveNonuniformHandles=dependent_handles,
                       limitation='Needs private per-eye descriptor tables, certified shared bindless/sampler/UAV state and timely native data')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path);parser.add_argument('--space',type=int,default=100)
    parser.add_argument('--pair',action='append',help='Pair only these proven eye-varying space-0 bindings, e.g. cbuffer:1 or srv:37')
    args=parser.parse_args();selected=None
    if args.pair:
        selected=set()
        for value in args.pair:
            match=re.fullmatch(r'(cbuffer|srv):(\d+)',value)
            if not match:parser.error('Use --pair cbuffer:N or --pair srv:N')
            selected.add((match[1],int(match[2])))
    ir,report=patch_pairs(args.source.read_text(encoding='utf-8'),args.space,selected)
    args.output.write_text(ir,encoding='utf-8');args.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
