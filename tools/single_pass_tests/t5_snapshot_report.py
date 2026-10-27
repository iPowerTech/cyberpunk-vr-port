"""Original-source native t5 and bounded committed-buffer snapshot receipts."""
import struct
from source_cbv_report import _source_rows, _no_permission


def rows(raw, words, limit=2048):
    if not isinstance(raw, (bytes, bytearray)) or len(raw) % (words * 8) or len(raw) // (words * 8) > limit:
        raise ValueError('Invalid t5 ABI extent')
    return list(struct.iter_unpack(f'<{words}Q', raw))


def analyze_coverage(raw, metadata_raw, source_report):
    """Current CPU coverage only; this does not validate contents or writers."""
    _, sources = _source_rows(source_report)
    coverage, metadata = rows(raw, 20), rows(metadata_raw, 32)
    if len(coverage) != len(metadata):
        raise ValueError('Structured coverage lost its original t5 occurrence set')
    seen = set()
    for row, meta in zip(coverage, metadata):
        if row[:2] != meta[:2] or row[0] >= len(sources) or row[0] in seen or row[1] != sources[row[0]][1] or row[2] not in (0, 1) or row[3]:
            raise ValueError('Structured coverage borrowed another source occurrence')
        seen.add(row[0])
        d = row[4:]
        if not row[2]:
            if any(d[:15]) or d[15] not in (0, 2**64 - 1):
                raise ValueError('Failed structured query acquired coverage')
            continue
        if d[0] != 0x5356434f56303031 or d[1] != meta[10] or not d[2] or any(d[k] not in (0, 1) for k in (3, 5, 7)):
            raise ValueError('Structured query current resource/ABI differs')
        if d[12:14] != meta[14:16] or not 0 < d[13] <= 65536 or d[14] > d[13] or d[10] > 2 * 1024 * 1024 or d[11] > 65536:
            raise ValueError('Structured coverage extent exceeds its bounded query/cache')
        if d[5] and (d[4] != d[2] or not d[9] or d[9] > 2 * 1024 * 1024):
            raise ValueError('Structured marker is stale or unbounded')
        if d[7] and (not d[3] or not d[5] or d[6] != d[2]):
            raise ValueError('Structured owner borrowed a stale marker')
        if not d[3] and (any(d[k] for k in (6, 7, 10, 11, 14))):
            raise ValueError('Missing owner acquired historical coverage')
        if bool(d[10]) != bool(d[11]) or d[14] > d[10]:
            raise ValueError('Structured owner counters differ')
        if d[14] == d[13]:
            if d[15] != 2**64 - 1:
                raise ValueError('Full structured coverage acquired a hole')
        elif not d[12] <= d[15] < d[12] + d[13]:
            raise ValueError('Structured query concealed its first hole')
    return dict(verified=True, rows=[list(row) for row in coverage], queries=sum(row[2] for row in coverage),
                fullyCovered=sum(row[2] and row[18] == row[17] for row in coverage),
                partial=sum(row[2] and 0 < row[18] < row[17] for row in coverage),
                uncovered=sum(row[2] and not row[18] for row in coverage), **_no_permission())


def analyze(native_raw, snapshot_raw, gpu_blob, intent_blob, source_report, metadata_raw, piece_raw=b''):
    if not isinstance(gpu_blob,(bytes,bytearray)) or not isinstance(intent_blob,(bytes,bytearray)):
        raise ValueError('t5 payload is not a binary ABI buffer')
    _, sources = _source_rows(source_report)
    native, snapshots, metadata = rows(native_raw, 96), rows(snapshot_raw, 32), rows(metadata_raw, 32)
    pieces=rows(piece_raw,16,65536)
    piece_groups={}
    for index,piece in enumerate(pieces):
        if piece[2]!=index or any(piece[13:]) or not all(piece[k] for k in (4,5,6,9,12)):
            raise ValueError('Structured candidate piece lost its actual copy labels')
        piece_groups.setdefault(piece[0],[]).append(piece)
    if len(native) != len(metadata) or len(snapshots) != len(metadata):
        raise ValueError('t5 current source, native and snapshot occurrence counts differ')
    by_ordinal = {}
    for n, meta in zip(native, metadata):
        ordinal = n[0]
        if ordinal >= len(sources) or ordinal in by_ordinal or n[:2] != meta[:2] or n[1] != sources[ordinal][1] or (
                n[15], n[16]) != (sources[ordinal][6], sources[ordinal][7]) or n[2] > 11:
            raise ValueError('Native t5 borrowed another original source occurrence')
        if meta[2] and (meta[4:6] != (23, 5) or meta[8] != 2 or not meta[12] or meta[13] != 128 or meta[11] > (2**64-1)//128):
            raise ValueError('t5 metadata is not the observed explicit structured view')
        if n[23] != (n[2] == 1):
            raise ValueError('Native t5 status and ownership marker differ')
        if any(n[45:48]) or any(n[70:72]) or any(n[74:]):
            raise ValueError('Native t5 reserved fields acquired unknown claims')
        if meta[20] or meta[30] or meta[31] or meta[27]!=sources[ordinal][7]:
            raise ValueError('t5 metadata recording or reserved fields differ')
        if n[23]:
            if not meta[2] or meta[3] or not meta[9] or n[12]!=sources[ordinal][9] or n[17]!=n[12]+0x5C0AF8+(n[7]-1)*0xB0:
                raise ValueError('Native t5 source view, registry or indexed record differs')
            if (n[3], n[4]) != (1, 5) or n[5] not in (0, 1) or not all(n[k] for k in (7, 9, 10, 11, 12, 13, 14, 17, 18, 19)):
                raise ValueError('Native t5 binding is incomplete')
            if n[11] != meta[10] or n[13] != sources[ordinal][8] or not n[34] or not n[35] or n[9] < n[34] or n[9]-n[34] > n[35] or (
                    n[10] > n[35]-(n[9]-n[34])) or (n[36], n[37], n[39], n[40]) != (meta[11]*128, meta[12]*128, 0, 8) or n[43:45] != (1, 0):
                raise ValueError('Native t5 resource/view/context differs from its held original view')
            if n[36] > n[35] or n[37] > n[35]-n[36]:
                raise ValueError('Native t5 structured view exceeds its actual COM buffer')
            raw_record = struct.pack('<22Q', *n[48:70])
            if struct.unpack_from('<I', raw_record)[0] != n[10] or raw_record[6] >> 4 != n[8] or raw_record[6] & 15 != n[6] or (
                    struct.unpack_from('<Q', raw_record, 8)[0], struct.unpack_from('<Q', raw_record, 16)[0]) != (n[9], n[11]):
                raise ValueError('Native t5 raw B0 record differs')
            if n[22] != n[8]:
                raise ValueError('Native t5 state belongs to another native class')
            if struct.unpack_from('<Q',raw_record,0x48)[0] != n[18]:
                raise ValueError('Native t5 CPU descriptor differs from its indexed record')
            slot = struct.pack('<2Q',*n[72:74])
            if struct.unpack_from('<I',slot)[0] or slot[13]==2 or slot[14] != 23 or struct.unpack_from('<HH',slot,8) != (n[39],n[40]):
                raise ValueError('Native t5 slot raw layout differs')
        by_ordinal[ordinal] = (n, meta)
    gpu_cursor = intent_cursor = 0
    seen = set()
    copy_identities={}
    for row in snapshots:
        ordinal, eye, status = row[:3]
        if ordinal not in by_ordinal or ordinal in seen or eye != sources[ordinal][1] or status not in (1, 2, 3) or row[30] not in (0,1) or row[31]:
            raise ValueError('t5 snapshot occurrence/status or permission differs')
        seen.add(ordinal)
        n, meta = by_ordinal[ordinal]
        if (row[4], row[12], row[13], row[14], row[15], row[20]) != (
                meta[10], meta[6], meta[7], sources[ordinal][8], sources[ordinal][7], ordinal + 1):
            raise ValueError('t5 snapshot original view or recording differs')
        if row[11] > 4096 or row[11] % 128 or row[10] % 128 or row[10] > 1024 * 1024 or row[11] > 1024 * 1024 - row[10]:
            raise ValueError('t5 snapshot range exceeds its bounded structured span')
        if row[25]:
            if not row[11] or row[26] != intent_cursor + 1 or row[11] > len(intent_blob) - intent_cursor:
                raise ValueError('t5 provisional candidate bytes are incomplete')
            intended = intent_blob[intent_cursor:intent_cursor + row[11]]
            intent_cursor += row[11]
            selected=piece_groups.get(ordinal,[])
            if not selected:
                raise ValueError('Structured candidate lacks actual copy piece receipts')
            if row[30] and row[25]!=ordinal+1:
                raise ValueError('Composed candidate acquired a fictitious single-copy label')
            cursor=row[10]
            for piece in selected:
                identity=(row[4],piece[4],*piece[6:10])
                if piece[5] in copy_identities and copy_identities[piece[5]]!=identity:
                    raise ValueError('Observed copy sequence reused a conflicting original identity')
                copy_identities[piece[5]]=identity
                if piece[1]!=eye or piece[3]!=row[25] or piece[10]!=cursor or piece[10]<piece[7] or piece[10]-piece[7]>piece[9] or piece[12]>piece[9]-(piece[10]-piece[7]) or piece[11]!=piece[8]+piece[10]-piece[7]:
                    raise ValueError('Structured candidate pieces have holes or wrong original-copy extents')
                cursor+=piece[12]
            if selected and (cursor!=row[10]+row[11] or row[30]!=(len({piece[5] for piece in selected})>1)):
                raise ValueError('Structured candidate piece coverage/composition differs')
            if selected and not row[30] and any(piece[5]!=row[25] for piece in selected):
                raise ValueError('Single-copy candidate borrowed another observed copy')
        elif row[26] or row[28]:
            raise ValueError('Missing t5 candidate acquired payload or GPU verification')
        elif piece_groups.get(ordinal) or row[30]:
            raise ValueError('Missing t5 candidate acquired copy pieces or composition')
        if status == 1:
            if row[3] or n[2] != 1 or n[42] != 1 or n[8] not in (0, 3) or n[20] != 2 or not n[21] & 0x80 or n[21] & ~0xAC3 or row[5] != n[9]:
                raise ValueError('Copied t5 lacks known original native shader-read state')
            if (row[6], row[7], row[8], row[9]) != (n[7], n[8], n[20], n[21]) or meta[21] != 1:
                raise ValueError('Copied t5 lacks its exact committed native backing')
            if not meta[19] or meta[11:14] != (0,8192,128) or meta[21:24] != (1,1,0) or any(meta[24:27]) or n[9] != n[34] or n[10] != 1024*1024 or n[35] != 1024*1024:
                raise ValueError('Copied t5 is not the admitted dedicated full committed backing')
            if row[18] != row[11] or not all(row[k] for k in (16, 19, 27)) or any(row[k] != 1 for k in (21, 22, 23, 24, 29)) or row[27] != gpu_cursor + 1:
                raise ValueError('Copied t5 did not finalize and reach source submission/readback')
            if row[16] == row[4] or row[19] in (row[4],row[16]) or row[17] % 16 or row[17] > 8 * 1024 * 1024 or row[18] > 8 * 1024 * 1024 - row[17]:
                raise ValueError('Private t5 storage overlaps or exceeds its recording arena')
            if row[18] > len(gpu_blob) - gpu_cursor:
                raise ValueError('t5 GPU payload is truncated')
            captured = gpu_blob[gpu_cursor:gpu_cursor + row[18]]
            gpu_cursor += row[18]
            if row[25]:
                if row[28] != (1 if captured == intended else 2):
                    raise ValueError('t5 published GPU match differs from its actual candidate/readback bytes')
            elif row[28]:
                raise ValueError('GPU-only t5 snapshot acquired CPU-candidate equivalence')
        elif status == 2:
            if row[11] or any(row[16:20]) or any(row[21:30]):
                raise ValueError('Empty t5 read acquired snapshot storage or bytes')
        elif any(row[k] for k in (16, 17, 18, 19, 22, 23, 24, 27, 28)):
            raise ValueError('Declined t5 copy acquired GPU capture markers')
    if gpu_cursor != len(gpu_blob) or intent_cursor != len(intent_blob):
        raise ValueError('t5 payload has unrelated trailing data')
    if any(ordinal not in seen for ordinal in piece_groups):
        raise ValueError('Structured candidate piece has no original snapshot occurrence')
    return dict(verified=True, native=[list(row) for row in native], snapshots=[list(row) for row in snapshots], pieces=[list(row) for row in pieces],
                nativeCaptured=sum(row[23] for row in native), copied=sum(row[2] == 1 for row in snapshots),
                declined=sum(row[2] == 3 for row in snapshots), empty=sum(row[2] == 2 for row in snapshots),
                matched=sum(row[28] == 1 for row in snapshots), mismatched=sum(row[28] == 2 for row in snapshots),
                gpuBytes=len(gpu_blob), candidateBytes=len(intent_blob), privateRangesConsumed=False,
                gpuOnly=sum(row[2] == 1 and not row[25] for row in snapshots),
                composed=sum(row[30] for row in snapshots),
                wholeInputEquivalenceProved=False, **_no_permission())
