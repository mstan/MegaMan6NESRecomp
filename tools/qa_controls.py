"""Qualify native equipment cycling and current-build held-input continuation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import qa_route

ROOT=Path(__file__).resolve().parent.parent


def dump(path,section):
    data=bytearray(0x2000)
    for line in path.read_text().splitlines():
        if line.startswith(section+' '):
            address,values=line[len(section)+1:].split(':',1)
            start=int(address,16);chunk=bytes.fromhex(values)
            data[start:start+len(chunk)]=chunk
    return data


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe',required=True);ap.add_argument('--rom',required=True)
    ap.add_argument('--out',required=True);ap.add_argument('--baseline-hash')
    a=ap.parse_args();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
    env={**os.environ,'SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy','NESRECOMP_NO_LAUNCHER':'1'}
    checks={}
    for name,route,frames,on,paired in [('off','boot-play',5220,False,False),
                                      ('idle','boot-play',5220,True,False),
                                      ('gallery','weapon-gallery',4620,True,True),
                                      ('native-beat','native-beat-graphics',4620,False,False)]:
        case=out/name
        cmd=[sys.executable,str(ROOT/'tools/qa_route.py'),str(ROOT/f'tests/routes/{route}.route'),
             '--exe',a.exe,'--rom',a.rom,'--out',str(case),'--frames',str(frames),'--shot-every','800']
        if not paired:cmd.append('--no-interp')
        for arg in ['--config',str(case/'config.ini'),'--weapon-cycle','on' if on else 'off',
                    '--mm6-controls-log',str(case/'controls.jsonl')]:cmd.append('--extra='+arg)
        with (out/f'{name}.log').open('w') as log:
            subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
        checks[name+'_completed']=True
    off=qa_route.load_hashes(out/'off/native.hash');idle=qa_route.load_hashes(out/'idle/native.hash')
    checks['enabled_idle_identity']=off==idle and len(off)==5220
    if a.baseline_hash:
        checks['stock_identity']=off==qa_route.load_hashes(Path(a.baseline_hash))
    rows=[json.loads(s) for s in (out/'gallery/controls.jsonl').read_text().splitlines()]
    previous=0;events=[]
    for row in rows:
        if row['changes']!=previous:events.append(row);previous=row['changes']
    checks['equipment_order']=[r['weapon'] for r in events]==[1,2,3,4,5,6,8,9,0,9,0,9,9]
    checks['one_edge_per_press']=len(events)==13
    checks['simultaneous_and_p2_ignored']=all(r['changes']==10 for r in rows if 4300<=r['frame']<4350)
    checks['menu_input_not_queued']=all(r['changes']==12 for r in rows if 4400<=r['frame']<4540)
    memory=dump(out/'gallery/mem_end.txt','ram');chr_ram=dump(out/'gallery/mem_end.txt','chr_ram')
    golden=dump(out/'native-beat/mem_end.txt','ram');golden_chr=dump(out/'native-beat/mem_end.txt','chr_ram')
    checks['native_menu_reference_is_beat']=memory[0x699]==9 and golden[0x699]==9
    checks['acquired_empty_and_locked_preserved']=memory[0x68d]==0 and memory[0x68f]==0x80
    checks['health_and_ammo_preserved']=memory[0x3e5]==27 and all(memory[0x688+i]==27 for i in range(10) if i not in (5,7))
    checks['native_beat_graphics']=chr_ram[0x1000:0x1800]==golden_chr[0x1000:0x1800]
    checks['native_beat_palette']=memory[0x370:0x378]==golden[0x370:0x378]
    cold=out/'cold';cold.mkdir(exist_ok=True)
    cmd=[a.exe,a.rom,'--no-save','--config',str(cold/'config.ini'),'--frames','4620',
         '--input',str(out/'gallery/input.txt'),'--weapon-cycle','on',
         '--load-state',str(out/'gallery/held_shoulder.cycstate'),'--hash-out',str(cold/'cold.hash'),
         '--screenshot',str(cold/'final.png'),'--mem-frame','4619','--mem-out',str(cold/'mem.txt')]
    with (cold/'run.log').open('w') as log:subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
    full=qa_route.load_hashes(out/'gallery/native.hash');continued=qa_route.load_hashes(cold/'cold.hash')
    checks['held_cold_continuation']=continued=={k:v for k,v in full.items() if k>4104} and bool(continued)
    checks['held_cold_memory']=(cold/'mem.txt').read_bytes()==(out/'gallery/mem_end.txt').read_bytes()
    checks['held_cold_picture']=(cold/'final.png').read_bytes()==(out/'gallery/s.png').read_bytes()
    result=dict(checks=checks,passed=all(checks.values()),executable_sha256=hashlib.sha256(Path(a.exe).read_bytes()).hexdigest())
    (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
