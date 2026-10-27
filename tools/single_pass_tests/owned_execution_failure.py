"""Bounded read-only evidence for failures before normal owned-graph decoding."""

import time


FIELDS = (
    ('stats', 'CyberpunkVR_OwnedExecutionStats', 24),
    ('resources', 'CyberpunkVR_OwnedExecutionResources', 12),
    ('compiler', 'CyberpunkVR_OwnedExecutionCompiler', 16),
    ('retirement', 'CyberpunkVR_OwnedExecutionRetirement', 8),
    ('admission', 'CyberpunkVR_OwnedExecutionAdmission', 6),
    ('schedule', 'CyberpunkVR_OwnedExecutionSchedule', 16),
)


def capture(read32, read_words, available=lambda name: True, *, attempts=8, sleep=time.sleep):
    if type(attempts) is not int or not 1 <= attempts <= 32:
        raise ValueError('Owned failure capture attempts exceed bounds')
    last_sequence = None
    for attempt in range(attempts):
        try:
            sequence = read32('CyberpunkVR_OwnedExecutionSeq')
            last_sequence = sequence
            if sequence & 1:
                if attempt + 1 < attempts:
                    sleep(.002)
                continue
            result = {}
            for key, name, count in FIELDS:
                words = list(read_words(name, count))
                if len(words) != count or any(type(value) is not int or not 0 <= value < 1 << 64 for value in words):
                    raise ValueError(f'{name} ABI mismatch')
                result[key] = words
            for key, name in (('mode', 'CyberpunkVR_OwnedExecutionMode'),
                              ('request', 'CyberpunkVR_OwnedExecutionRequest'),
                              ('closing', 'CyberpunkVR_OwnedExecutionClosing')):
                result[key] = read32(name)
            name = 'CyberpunkVR_OwnedSceneSplitStats'
            if available(name):
                words = list(read_words(name, 24))
                if len(words) != 24 or any(type(value) is not int or not 0 <= value < 1 << 64 for value in words):
                    raise ValueError(f'{name} ABI mismatch')
                result['sceneSplit'] = words
            if sequence != read32('CyberpunkVR_OwnedExecutionSeq'):
                if attempt + 1 < attempts:
                    sleep(.002)
                continue
            return dict(consistent=True, sequence=sequence, attempt=attempt + 1, **result)
        except Exception as error:
            return dict(consistent=False, sequence=last_sequence, attempt=attempt + 1, error=str(error))
    return dict(consistent=False, sequence=last_sequence, attempt=attempts,
                error='Owned execution publication remained odd or changed')
