"""Decode sealed, draw-time GPU material snapshots."""
import hashlib
import struct

FIELDS=('sequence','frame','side','family','resource','handle','kind','descriptor','address','sourceOffset',
        'heapType','nativeStatus','nativeState','stage','failure','payloadOffset','payloadBytes',
        'failureDetail0','failureDetail1','failureDetail2','failureDetail3',
        'alphaGateBits','ditherScaleBits','ditherBiasBits','ditherSelector','controlMask',
        'hintedIndex','descriptorMask','textureConfirmed')
ROW_BYTES=len(FIELDS)*8
ROW_FORMAT=f'<{len(FIELDS)}Q'
PAYLOAD_BYTES=292


def decode(raw,blob,enabled):
    if len(raw)%ROW_BYTES or len(raw)>128*ROW_BYTES or len(blob)>128*PAYLOAD_BYTES:
        raise ValueError('Invalid material capture size')
    rows=[]
    for values in struct.iter_unpack(ROW_FORMAT,raw):
        row=dict(zip(FIELDS,values));offset=row['payloadOffset'];size=row['payloadBytes']
        if row['side']>1 or not row['sequence'] or not row['frame'] or row['family'] not in (39,43):
            raise ValueError('Invalid material source draw')
        if offset>len(blob) or size>len(blob)-offset or size not in (0,PAYLOAD_BYTES):
            raise ValueError('Invalid material payload span')
        if size:
            if row['stage']!=3 or row['failure']:raise ValueError('Unsealed material payload published')
            payload=blob[offset:offset+size]
            row.update(textureIndex=struct.unpack_from('<I',payload)[0],
                       cutoffBits=struct.unpack_from('<I',payload,272)[0],
                       uvScaleBits=struct.unpack_from('<I',payload,288)[0],sha256=hashlib.sha256(payload).hexdigest())
        rows.append(row)
    return dict(enabled=enabled,rows=rows,bytes=len(blob),sealed=sum(r['payloadBytes']==PAYLOAD_BYTES for r in rows))


def validate(report):
    rows=report['rows']
    if not report['enabled']:
        if rows or report['bytes']:raise RuntimeError('Disabled material capture recorded GPU copies')
        return
    if not rows or {r['side'] for r in rows}!={0,1} or report['sealed']!=len(rows):
        failed=[(r['sequence'],r['nativeStatus'],r['stage'],r['failure']) for r in rows if r['payloadBytes']!=PAYLOAD_BYTES]
        raise RuntimeError(f'Material GPU capture incomplete: {failed}')
    end=0
    for row in rows:
        if row['payloadOffset']!=end:raise RuntimeError('Material payloads overlap or leave gaps')
        end+=PAYLOAD_BYTES
    if end!=report['bytes']:raise RuntimeError('Unreferenced material payload bytes')


def hints(report):
    validate(report);result={}
    for row in report['rows']:
        handle,index=row['handle'],row['textureIndex']
        if not 0<handle<=0x100000 or not 0<=index<32768:raise ValueError('Invalid material texture hint')
        if handle in result and result[handle]!=index:raise ValueError('Material changed texture index within capture')
        result[handle]=index
    if not result or len(result)>128:raise ValueError('Invalid material hint count')
    return sorted(result.items())


def attach_descriptors(report,raw):
    if len(raw)!=len(report['rows'])*480:raise ValueError('Invalid material descriptor size')
    for row,values in zip(report['rows'],struct.iter_unpack('<60Q',raw)):
        row['descriptors']=[list(values[i:i+20]) for i in (0,20,40)]
        if row['descriptorMask']&~7 or row['textureConfirmed'] not in (0,1):raise ValueError('Invalid descriptor mask')
        if row['textureConfirmed'] and (not row['descriptorMask']&1 or row.get('textureIndex')!=row['hintedIndex']):
            raise ValueError('Texture association lacks fresh GPU index confirmation')


def validate_textures(report):
    validate(report)
    for row in report['rows']:
        if row['descriptorMask']!=7 or not row['textureConfirmed']:raise RuntimeError('Material texture association incomplete')
        alpha,dither,sampler=row['descriptors']
        if alpha[0]!=2 or alpha[5]!=4 or dither[0]!=2 or dither[5]!=5 or sampler[0]!=4:
            raise RuntimeError('Unexpected alpha/dither/sampler binding type')
        for view in (alpha,dither):
            if view[1]&3!=1 or not view[1]&4 or not view[2] or not view[4] or view[13]&0xffffffff!=3 or not view[14] or not view[15]&0xffffffff:
                raise RuntimeError('Material SRV lacks live resource identity or shape')
        if row['hintedIndex']>=32768:raise RuntimeError('Material texture index is outside the native array')
