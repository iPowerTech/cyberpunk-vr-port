"""Temporary named Boolean CVar setters for genuine native cache eviction."""
import itertools
import json
from pathlib import Path
import shutil
import time
import uuid

NAMES=('Rendering/DLSSDSeparateParticleColor','Rendering/DLSSDTransparentGuide',
       'Developer/FeatureToggles/CharacterSubsurfaceStochastic')
MOD_NAME='CyberpunkVR_SinglePassRetirementProbe'


def combinations(baseline):
    if set(baseline)!=set(NAMES) or any(type(value) is not bool for value in baseline.values()):
        raise ValueError('Incomplete or non-Boolean cache CVar baseline')
    return [dict(zip(NAMES,values)) for values in itertools.product((False,True),repeat=3)]


class NativeSettingsProbe:
    def __init__(self,game,alive,check=lambda:None):
        self.directory=Path(game)/'bin/x64/plugins/cyber_engine_tweaks/mods'/MOD_NAME
        self.alive=alive;self.check=check;self.run_id=uuid.uuid4().hex;self.next_id=0;self.installed=False
        self.baseline=None;self.restored=False
    def install(self,seconds=600):
        if self.installed or self.directory.exists():raise RuntimeError('Refusing to replace an existing settings probe')
        self.directory.mkdir()
        try:
            shutil.copy2(Path(__file__).with_suffix('.lua'),self.directory/'init.lua')
            (self.directory/'settings_probe_run.json').write_text(json.dumps(dict(enabled=True,runId=self.run_id,expiresAt=int(time.time()+seconds))),encoding='utf-8')
        except Exception:
            self._remove_owned_files();raise
        self.installed=True
    def command(self,action,values=None,timeout=15):
        if not self.installed or not self.alive():raise RuntimeError('Settings probe requires its owned live game')
        self.next_id+=1
        request=dict(runId=self.run_id,id=self.next_id,action=action)
        if values is not None:request['values']=values
        temporary=self.directory/'settings_probe_command.tmp'
        temporary.write_text(json.dumps(request),encoding='utf-8');temporary.replace(self.directory/'settings_probe_command.json')
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            self.check()
            if not self.alive():raise RuntimeError('Game exited during native graphics Apply')
            try:status=json.loads((self.directory/'settings_probe_status.json').read_text(encoding='utf-8'))
            except (FileNotFoundError,json.JSONDecodeError):status={}
            if status.get('runId')==self.run_id and status.get('id')==self.next_id:
                if status.get('status')!='applied':raise RuntimeError('Native CVar command failed: '+json.dumps(status))
                return status
            time.sleep(.05)
        raise TimeoutError('Native CVar command was not acknowledged')
    def begin(self):
        if self.baseline is not None:
            result=self.command('inspect')
            if result['values']!=self.baseline or result.get('baseline')!=self.baseline or result.get('dirty') is not False:
                raise RuntimeError('Previous retirement cycle did not restore the immutable CVar baseline')
            return result
        result=self.command('begin');self.baseline=result['values'];combinations(self.baseline);return result
    def apply(self,values):
        self.restored=False
        return self.command('apply',values)
    def restore_live(self):
        if self.baseline is None:return False
        result=self.command('restore')
        if result['values']!=self.baseline:
            raise RuntimeError('Native CVar baseline was not restored')
        self.restored=True;return result
    def _remove_owned_files(self):
        for name in ('init.lua','settings_probe_run.json','settings_probe_command.json','settings_probe_command.tmp','settings_probe_status.json',
                     MOD_NAME+'.log','db.sqlite3','db.sqlite3-wal','db.sqlite3-shm'):
            (self.directory/name).unlink(missing_ok=True)
        self.directory.rmdir()
    def remove_after_exit(self):
        if self.alive():raise RuntimeError('Cannot remove a settings mod used by a running game')
        if not self.installed:return
        deadline=time.monotonic()+5
        while True:
            try:
                self._remove_owned_files();self.installed=False;return
            except PermissionError:
                if time.monotonic()>=deadline:raise
                time.sleep(.1)
