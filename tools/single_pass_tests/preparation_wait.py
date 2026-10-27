"""Wait on one live recorder without treating an observation window as exit."""

import time


def wait(read, alive, on_poll=lambda: None, on_slow=lambda snapshot: None,
         *, timeout=60, idle_timeout=20, slow_after=8,
         monotonic=time.monotonic, sleep=time.sleep):
    start = changed = monotonic()
    previous = None
    slow = False
    while True:
        if not alive():
            raise RuntimeError('Game exited while preparation capture was pending')
        sample = {name: read(name) for name in ('Request', 'State', 'Seq', 'Count', 'PacketCount', 'Failure')}
        now = monotonic()
        if sample['Request'] == 0 and sample['State'] == 2:
            return dict(elapsedSeconds=now-start, slowObservation=slow, final=sample)
        if sample != previous:
            changed = now
            previous = sample
        on_poll()
        if not slow and now-start >= slow_after:
            slow = True
            on_slow(dict(elapsedSeconds=now-start, processAlive=True, **sample))
            continue
        if now-start >= timeout or now-changed >= idle_timeout:
            raise TimeoutError('Preparation recorder remains incomplete in a live process: ' + repr(sample))
        sleep(.01)
