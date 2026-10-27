"""Validate current-occurrence binding assembly diagnostics, not execution."""
import struct

from source_cbv_report import _no_permission, _source_rows, _words


def _rows(raw, words, limit):
    if not isinstance(raw, (bytes, bytearray)) or len(raw) % (words * 8) or len(raw) // (words * 8) > limit:
        raise ValueError('Invalid source assembly binary extent')
    return list(struct.iter_unpack(f'<{words}Q', raw))


def require_bank_created(report):
    bank = _words(report.get('bankStats'), 6, 'source bank stats')
    count = len(report.get('commands', []))
    if report.get('verified') is not True or not count or report.get('bankCreated') is not True or bank[:3] != [1, 1, count]:
        raise ValueError('Whole source bank was not actually created')
    if report.get('ready') != count or report.get('cpuAssembled') != count or bank[3] > 1000000 or any(bank[4:]):
        raise ValueError('Whole source bank has missing preparation, capacity failure or consumption')
    if report.get('privateTablesConsumed') is not False or report.get('nativeExecutionPermitted') is not False:
        raise ValueError('Bank preparation acquired native execution permission')


def analyze(stats, command_raw, table_raw, missing_raw, source_report, snapshot_report, selector_report):
    stats = _words(stats, 16, 'source assembly stats')
    source_stats, sources = _source_rows(source_report)
    commands = _rows(command_raw, 32, 2048)
    tables = _rows(table_raw, 16, 65536)
    missing = _rows(missing_raw, 16, 65536)
    if stats[0] != 3 or stats[1] != source_stats[1] or stats[12] or any(stats[13:]):
        raise ValueError('Assembly diagnostics are incomplete or acquired bank execution')
    if len(commands) != len(sources) or stats[2] != len(commands):
        raise ValueError('Assembly lost the original source occurrence set')
    if snapshot_report.get('verified') is not True or selector_report.get('verified') is not True:
        raise ValueError('Assembly lacks verified original GPU input reports')
    cbvs = snapshot_report.get('rows', [])
    selectors = selector_report.get('commands', [])
    if len(selectors) != len(commands):
        raise ValueError('Assembly selector occurrence set differs')
    grouped = [[] for _ in sources]
    for row in cbvs:
        row = _words(row, 32, 'source CBV snapshot')
        if row[0] >= len(sources) or row[1] != sources[row[0]][1]:
            raise ValueError('Assembly CBV snapshot belongs to another source')
        grouped[row[0]].append(row)
    table_cursor = missing_cursor = 0
    for ordinal, (command, source, selector, source_cbvs) in enumerate(zip(commands, sources, selectors, grouped)):
        if command[:2] != (ordinal, source[1]) or command[11:15] != (source[6], source[7], ordinal + 1, source[11]):
            raise ValueError('Assembly borrowed another eye, recording, root or occurrence')
        if any(command[k] not in (0, 1) for k in (2, 4, 9, 10, 17, 21, 22)) or command[3] > 17 or any(command[23:26]):
            raise ValueError('Assembly has unknown flags or acquired execution permission')
        qualified_selector = (selector[2] == 1 and selector[11] == 1 and not selector[23] and selector[24] == 1 and
                              selector[5] == ordinal + 1 and tuple(selector[13:15]) == (source[6], source[7]) and selector[16] == source[11])
        if command[16] != selector[2] or command[17] != selector[11] or (command[9] and not qualified_selector):
            raise ValueError('Assembly dynamic coverage differs from exact GPU selector validation')
        if command[4] and (not command[2] or command[5] or (command[8] and not command[10])):
            raise ValueError('Ready assembly has missing inputs or unclosed copies')
        if command[21] and (not command[4] or command[26]):
            raise ValueError('CPU assembly claims success without a ready plan')
        if command[22] and not command[21]:
            raise ValueError('GPU bank acquired an unassembled source')
        if not command[2] and (not command[3] or command[4] or command[21]):
            raise ValueError('Failed Build acquired readiness')
        if command[2] and command[3] and not command[26]:
            raise ValueError('Successful Build has an unexplained failure')
        copied = [row for row in source_cbvs if row[8] == 1]
        domains = {}
        for cbv in source_cbvs:
            domains.setdefault((cbv[4], cbv[5]), []).append(cbv)
        # Reflection may repeat one ALL-visible descriptor across shader stages.
        # Its logical requirement grows to the maximum, but its backing stays one.
        backing_fields = (3, 7, 8, 10, 11, 12, 13, 14, 23, 24, 25, 26)
        for domain in domains.values():
            backing = tuple(domain[0][k] for k in backing_fields)
            if any(tuple(cbv[k] for k in backing_fields) != backing for cbv in domain[1:]):
                raise ValueError('Shared CBV descriptor domain has conflicting original backing')
        if command[8] != len(copied) or command[31] != len(domains):
            raise ValueError('Assembly reflected/private CBV counts differ')
        if command[27] != table_cursor or command[29] != missing_cursor or command[30] != command[5]:
            raise ValueError('Assembly table/missing ranges have gaps or overlaps')
        selected_tables = tables[table_cursor:table_cursor + command[28]]
        selected_missing = missing[missing_cursor:missing_cursor + command[30]]
        if len(selected_tables) != command[28] or len(selected_missing) != command[30]:
            raise ValueError('Assembly table/missing range is truncated')
        table_cursor += command[28]
        missing_cursor += command[30]
        parameters = set()
        for table in selected_tables:
            if table[:2] != command[:2] or table[2] >= 64 or not source[13] & (1 << table[2]) or table[2] in parameters:
                raise ValueError('Assembly table parameter is duplicated or belongs to another source')
            parameters.add(table[2])
            if not table[3] or table[4] not in (1, 2) or not 0 < table[5] <= table[3] or not table[6] or not table[7] or any(table[14:]) or table[13] != command[22]:
                raise ValueError('Assembly original table extent/owner or reserved fields differ')
            if command[2] and not all(table[k] for k in (8, 9, 10, 11)):
                raise ValueError('Successful Build lost original table identity/version/source labels')
            if bool(table[12]) != bool(command[21]):
                raise ValueError('Assembly CPU heap publication differs from its command')
        if command[18:20] != (sum(t[4] == 1 for t in selected_tables), sum(t[4] == 2 for t in selected_tables)):
            raise ValueError('Assembly finite/sparse table counts differ')
        seen = set()
        for row in selected_missing:
            if row[:2] != command[:2] or row[4] or row[10] not in (1, 2) or row[14] or row[15] not in (0, 1):
                raise ValueError('Missing CBV has another source or invented original-safe certificate')
            key = (row[5], row[6])
            matches = domains.get(key, [])
            if key in seen or not matches or (row[2], row[3]) != (matches[0][2], matches[0][3]):
                raise ValueError('Missing CBV is not one exact reflected descriptor domain')
            seen.add(key)
            cbv = matches[0]
            if (row[7], row[8], row[9], row[11], row[12], row[13], row[15]) != (
                    max(v[6] for v in matches), cbv[7], cbv[10], cbv[12], cbv[23], cbv[24], int(cbv[10] in (2, 5))):
                raise ValueError('Missing CBV view/backing differs from the original GPU receipt')
            if row[10] == 1 and cbv[8] == 1:
                raise ValueError('Copied DEFAULT CBV is reported without a private owner')
        deferred = {(v[4], v[5]) for v in source_cbvs if v[8] == 2}
        if not deferred <= seen:
            raise ValueError('Assembly omitted a deferred UPLOAD CBV')
    if table_cursor != len(tables) or missing_cursor != len(missing):
        raise ValueError('Assembly has unrelated trailing rows')
    expected = [3, source_stats[1], len(commands), sum(c[2] for c in commands), sum(not c[2] for c in commands),
                sum(c[4] for c in commands), len(missing), len(tables), sum(c[7] for c in commands),
                sum(c[21] for c in commands), sum(c[22] for c in commands), next((c[3] for c in commands if c[3]), 0), 0, 0, 0, 0]
    if stats != expected:
        raise ValueError('Assembly aggregates differ from exact occurrence rows')
    return dict(verified=True, stats=stats, commands=[list(c) for c in commands], tables=[list(t) for t in tables],
                missingCbvs=[list(m) for m in missing], buildSucceeded=stats[3], buildFailed=stats[4], ready=stats[5],
                missingCount=len(missing), missingUploadCount=sum(m[15] for m in missing), cpuAssembled=stats[9],
                bankCreated=bool(stats[10]), bankSourceCount=stats[10], privateTablesConsumed=False,
                wholeInputEquivalenceProved=False, **_no_permission())
