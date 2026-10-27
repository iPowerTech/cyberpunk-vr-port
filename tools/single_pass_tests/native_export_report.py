"""Strict terminal validation of the owned native output-copy diagnostic."""

FIELDS = ('requested', 'prepared', 'submitted', 'completed', 'planes', 'bytes',
          'differentLeft', 'differentRight', 'compared', 'preservedOnInputReject',
          'inputRejected', 'error', 'sourceFrame', 'exportList', 'targetHeaps',
          'residencyAcquired', 'residencySubmitted', 'residencyFenced', 'residencyRetired',
          'residencyHRESULT', 'accessRevalidated', 'lifetimesRevalidated', 'CPUwaits', 'nativeSuppressed')
MAX_BYTES = 512 * 1024 * 1024


def decode(values):
    if not isinstance(values, (list, tuple)) or len(values) != len(FIELDS) or any(
            type(value) is not int or not 0 <= value < 2**64 for value in values):
        raise ValueError('Native export ABI requires exactly 24 uint64 fields')
    return dict(zip(FIELDS, values))


def validate(values, *, requested, frame, rejected=False):
    result = decode(values)
    if type(requested) is not bool or type(rejected) is not bool or type(frame) is not int or not 0 < frame < 2**64:
        raise ValueError('Native export validation lacks an explicit request or source frame')
    if result['requested'] != int(requested):
        raise ValueError('Native export request differs from the selected mode')
    if not requested:
        if result['sourceFrame'] not in (0, frame) or any(value for name, value in result.items() if name != 'sourceFrame'):
            raise ValueError('Native export performed work without an explicit request')
        return result
    if result['sourceFrame'] != frame:
        raise ValueError('Native export belongs to a different source frame')
    for name in ('prepared', 'submitted', 'completed', 'compared', 'residencyAcquired',
                 'residencySubmitted', 'residencyFenced', 'residencyRetired',
                 'accessRevalidated', 'lifetimesRevalidated'):
        if result[name] != 1:
            raise ValueError(f'Native export did not complete {name}')
    if result['planes'] != 10 or not 10 * 512 <= result['bytes'] <= MAX_BYTES or result['bytes'] % 512:
        raise ValueError('Native export did not compare all ten bounded output planes')
    if not result['exportList'] or not 1 <= result['targetHeaps'] <= 8:
        raise ValueError('Native export lacks its owned list or target residency objects')
    if result['residencyHRESULT'] >= 2**31:
        raise ValueError('Native export SDK residency failed')
    for name in ('differentLeft', 'differentRight', 'error', 'CPUwaits', 'nativeSuppressed'):
        if result[name]:
            raise ValueError(f'Native export reported nonzero {name}')
    if result['inputRejected'] != int(rejected) or result['preservedOnInputReject'] != int(rejected):
        raise ValueError('Native export input-rejection preservation differs from the requested test')
    return result
