"""Bounded independent-view regression using native world actors and real controls."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,sys,time,wave
import qa_route
ROOT=Path(__file__).resolve().parent.parent
ap=argparse.ArgumentParser(description="Check two living players in independent horizontal views and current-build continuation.")
ap.add_argument('--exe',required=True);ap.add_argument('--rom',required=True);ap.add_argument('--out',required=True)
a=ap.parse_args();exe=Path(a.exe).resolve();rom=Path(a.rom).resolve()
out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
qa_route.STATES.clear();_,lines=qa_route.expand(ROOT/'tests/routes/proto-fixture.route')
lines=[x for x in lines if int(x.split()[0])<4000]+['4030 2:RIGHT','4040 2:-','4050 2:B','4052 2:-']
(out/'input.txt').write_text('\n'.join(lines)+'\n')
env={**os.environ,'NESRECOMP_NO_LAUNCHER':'1','SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy'}
personal=[base+slot for base in range(0x3a0,0x63c,23) for slot in range(8)]+[a for a in range(0x94,0xaf) if a!=0xa9]+[0x22]+list(range(0x688,0x692))+[0x699]+list(range(0x370,0x378))+list(range(0x6a2,0x6a5))+[0x6ab]
def memory(path):
 b=path.read_bytes();at=32
 while at<len(b):
  tag,n=struct.unpack_from('<4sI',b,at);v=b[at+8:at+8+n];at+=8+n
  if tag==b'HW  ':m=v[23:2071];break
 i=b.find(b'M6C4');assert i>=0;c=b[i:]
 def read(s,a):return m[a] if c[8]==s else c[17+s*320+personal.index(a)]
 return b,m,read
elapsed={}
for name in ['native','interp','cold','player1','player2']:
 d=out/name;d.mkdir(exist_ok=True)
 args=[str(exe),str(rom),'--no-save','--config',str(d/'config.ini'),'--frames','4200','--input',str(out/'input.txt'),'--coop','on','--coop-roster','megaman-protoman','--coop-view',name if name.startswith('player') else 'split','--wide','on','--aspect','fit','--no-flicker','on','--weapon-cycle','off','--mm6-controls-log',str(d/'state.jsonl'),'--hash-out',str(d/'run.hash'),'--present-out',str(d/'present.png'),'--present-every','1','--mem-frame','4199','--mem-out',str(d/'mem.txt'),'--wav-out',str(d/'audio.wav')]
 for a,v in [(0x486,0x80),(0x46f,2),(0x4cb,116),(0x4b4,0),(0x49d,0),(0x4e2,0),(0x3e5,27),(0xa2,255)]:args+=['--mm6-coop-poke',f'4000:1:{a:x}:{v:x}']
 args+=['--mm6-coop-poke','4070:1:a2:1']
 for f in [4005,4055,4100,4199]:args+=['--save-state',str(f)+':'+str(d/(str(f)+'.cycstate'))]
 if name=='interp':args+=['--load-state',str(out/'native/4005.cycstate'),'--interp-only']
 if name in ['cold','player1','player2']:args+=['--load-state',str(out/'native/4100.cycstate')]
 # Only retain per-frame pictures around the current-build reload boundary.
 if name=='native':
  args[args.index('--present-every')+1]='100'
 start=time.monotonic()
 with (d/'run.log').open('w') as log:subprocess.run(args,check=True,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0)
 elapsed[name]=time.monotonic()-start;print(name,elapsed[name],flush=True)
n=out/'native';checks={};full=qa_route.load_hashes(n/'run.hash')
for name,cut in [('interp',4005),('cold',4100)]:
 d=out/name;tail=qa_route.load_hashes(d/'run.hash')
 checks[name+'-execution']=len(tail)==4200-cut-1 and tail=={f:h for f,h in full.items() if f>cut}
 checks[name+'-state-picture']=all((n/p).read_bytes()==(d/p).read_bytes() for p in ['mem.txt','present.png','4199.cycstate'])
 with wave.open(str(n/'audio.wav')) as a,wave.open(str(d/'audio.wav')) as b:pcm=a.readframes(a.getnframes());tailpcm=b.readframes(b.getnframes())
 checks[name+'-pcm']=bool(tailpcm) and pcm.endswith(tailpcm)
checks['cold-first-picture']=(out/'interp/present_04101.png').read_bytes()==(out/'cold/present_04101.png').read_bytes()
b,m,read=memory(n/'4199.cycstate');i=b.find(b'M6V2');v=b[i+6044:i+12088];j=b.find(b'M6C4')
checks['both-live-native-bodies']=all(read(s,0x3a0)==4 and read(s,0x3e5)==27 and not(read(s,0x3b7)&3) for s in [0,1])
checks['distant-personal-movement']=read(1,0x46f)==2 and read(1,0x486)>128 and read(0,0x46f)==0 and read(0,0x486)==128
_,_,shot_read=memory(n/'4055.cycstate')
checks['distant-personal-shot']=any(shot_read(1,0x3a0+s) in [9,10,11] for s in range(1,4))
checks['separate-camera-panels']=v[90:92]==b'\x01\x00' and struct.unpack_from('<ii',v,96)==(0,512)
checks['guest-real-parts']=struct.unpack_from('<H',b,j+38628)[0]>0 and b[j+38640]==1
checks['no-life-loss']=m[0xa9]==2
for name in ['native','player1','player2']:
 d=out/name;w,h=struct.unpack('>II',(d/'present.png').read_bytes()[16:24]);checks[name+'-dimensions']=(w,h)==(512 if name=='native' else 256,240)
 if name!='native':checks[name+'-canonical-simulation']=all((d/p).read_bytes()==(out/'cold'/p).read_bytes() for p in ['run.hash','mem.txt','4199.cycstate','audio.wav'])
result={'checks':checks,'passed':all(checks.values()),'seconds':elapsed,'exe':hashlib.sha256(exe.read_bytes()).hexdigest(),'native_hp':27,'last_personal_x':[read(s,0x486)+256*read(s,0x46f) for s in [0,1]]}
(out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(result,flush=True);raise SystemExit(not result['passed'])
