from pathlib import Path
import argparse, json, re, shlex, subprocess, sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'firmware/tests'))
from prepare_box0_probe import instrument_sketch
from test_box0_bounds import function
parser=argparse.ArgumentParser()
parser.add_argument('--nearest',action='store_true')
parser.add_argument('--live',action='store_true')
parser.add_argument('--output',default='replay-fast')
options=parser.parse_args()
out=ROOT/'artifacts/box0-test'/options.output;out.mkdir(exist_ok=True)
packet=(ROOT/'artifacts/box0-test/buildings-real.bin').read_bytes()
array='static const uint8_t replayMap[] = {\n'+',\n'.join(','.join(str(v) for v in packet[i:i+32]) for i in range(0,len(packet),32))+'\n};\n'
replay=r"""
static bool replayRunning=false;
static uint32_t replayStarted=0,replayPoseAt=0,replayMapAt=0,replayOffset=0;
static uint8_t replayMapId=20;
static int32_t replayShift=0;
static bool replayQueue(const uint8_t *data,size_t count) {
  portENTER_CRITICAL(&mux);
  int next=(rxHead+1)%RXQ_SLOTS;
  if(next==rxTail) { portEXIT_CRITICAL(&mux);return false; }
  rxq[rxHead].len=count;memcpy(rxq[rxHead].data,data,count);rxHead=next;
  portEXIT_CRITICAL(&mux);return true;
}
static void replayText(const String &s) { replayQueue((const uint8_t*)s.c_str(),s.length()); }
static void replayPut32(uint8_t *p,int32_t value) {
  for(int i=0;i<4;i++)p[i]=(uint32_t)value>>(8*i);
}
static void replayStart() {
  if(bleConnected) {Serial.println("[REPLAY] refused: live BLE is connected");return;}
  replayRunning=true;replayStarted=millis();replayPoseAt=0;
  replayMapAt=millis();replayOffset=0;replayShift=0;page=0;
  handleText("#t,1");handleText("#m,1");handleText("#z,0");
  handleText("R,320,90");handleText("#v,20");handleText("#k,60,180");
  handleText("#j,0033F0");
  uint8_t route[12+4*63]={};uint16_t frame=rdu16(replayMap);
  route[0]=frame;route[1]=frame>>8;route[10]=64;
  int32_t cx=rd32(replayMap+2)+1200,cy=rd32(replayMap+6)-1200;
  int32_t px=cx+180,py=cy;replayPut32(route+2,px);replayPut32(route+6,py);
  for(int i=1;i<64;i++) {
    float a=i*2*PI/63;int32_t x=cx+lroundf(180*cosf(a)),y=cy+lroundf(180*sinf(a));
    int16_t dx=x-px,dy=y-py;int k=12+4*(i-1);
    route[k]=dx;route[k+1]=dx>>8;route[k+2]=dy;route[k+3]=dy>>8;px=x;py=y;
  }
  uint8_t msg[15+sizeof(route)]={};msg[0]='M';msg[1]=2;msg[2]=7;msg[3]=1;
  replayPut32(msg+7,sizeof(route));memcpy(msg+15,route,sizeof(route));replayQueue(msg,sizeof(msg));
  probeTextSlot(0,"REPLAY ROAD");probeTextSlot(1,"TEST MAP NOT LIVE");
  Serial.println("[REPLAY] start; cached real buildings, protocol queue, simulated poses; BLE authentication unchanged");
}
static void replayTick() {
  if(!replayRunning)return;
  if(bleConnected) {replayRunning=false;Serial.println("[REPLAY] stopped for live BLE");return;}
  unsigned long now=millis(),elapsed=now-replayStarted;
  static bool checked=false;
  if(!checked && rasterValid && RM.fb && elapsed>5000) {
    int errors=0;
    for(int i=0;i<8192;i++) {
      int x=(i*37)%(rasW+4)-2,y=(i*53)%(rasH+4)-2;
      if(i%4==0)x=(x>>4)*16+15;
      if(i%4==1)y=(y>>4)*16+15;
      uint32_t fx=(i*131)&255,fy=(i*197)&255;
      int32_t u=x*65536+32768+fx*256,v=y*65536+32768+fy*256;
      uint16_t a=rasterColorAt(x,y,false,0,0),b=rasterColorAt(x+1,y,false,0,0);
      uint16_t c=rasterColorAt(x,y+1,false,0,0),d=rasterColorAt(x+1,y+1,false,0,0);
      uint16_t expected=blendF(blendF(a,b,fx),blendF(c,d,fx),fy);
      if(rasterSmoothColor(u,v,false,0,0)!=expected)errors++;
    }
    Serial.printf("[SAMPLER] scalar equivalence 8192 samples errors=%d\n",errors);
    checked=true;
  }
  if(pendingStat2.length()) {Serial.println("[REPLAY_STATS] "+pendingStat2);pendingStat2="";}
  if(replayOffset>=sizeof(replayMap) && now-replayMapAt>=10000) {
    replayOffset=0;replayMapId++;replayMapAt=now;replayShift=replayShift?0:64;
  }
  for(int n=0;n<8 && replayOffset<sizeof(replayMap);n++) {
    constexpr uint32_t chunk=480;
    uint8_t msg[15+chunk];uint32_t len=min(chunk,(uint32_t)sizeof(replayMap)-replayOffset);
    uint16_t seq=replayOffset/chunk,parts=(sizeof(replayMap)+chunk-1)/chunk;
    msg[0]='M';msg[1]=3;msg[2]=replayMapId;
    msg[3]=parts;msg[4]=parts>>8;msg[5]=seq;msg[6]=seq>>8;
    replayPut32(msg+7,sizeof(replayMap));replayPut32(msg+11,replayOffset);
    memcpy(msg+15,replayMap+replayOffset,len);
    if(replayOffset==0)replayPut32(msg+17,rd32(replayMap+2)+replayShift);
    if(!replayQueue(msg,15+len))break;
    replayOffset+=len;
    if(replayOffset==sizeof(replayMap))Serial.printf("[REPLAY] submitted map id=%u elapsed=%lu shift=%ld\n",replayMapId,elapsed/1000,(long)replayShift);
  }
  if(now-replayPoseAt>=500) {
    replayPoseAt=now;
    float a=elapsed*0.00010f;
    int32_t cx=rd32(replayMap+2)+1200+(int32_t)(180*cosf(a));
    int32_t cy=rd32(replayMap+6)-1200+(int32_t)(180*sinf(a));
    int hd=((int)(elapsed*0.02f))%360;
    int speed=(elapsed/60000)%2?120:20;
    String position="#p,"+String(rdu16(replayMap))+","+String(cx*10)+","+String(cy*10)+","+String(hd)+","+String(speed);
    replayText(position);replayText("#v,"+String(speed));
    replayText((elapsed/20000)%3==0?"R,25,90":((elapsed/20000)%3==1?"L,320,-90":"S,12000"));
    replayText("#e,12000,1830,12");
    if((elapsed/90000)%2)replayText("#m,0");else replayText("#m,1");
  }
}
"""
header=(ROOT/'firmware/tests/Box0RenderProbe.h').read_text(encoding='utf-8')
header=header.replace('static void probeDispatch(const char *command) {',array+replay+'\nstatic void probeDispatch(const char *command) {\n  if(strcmp(command,"REPLAY")==0) {replayStart();return;}\n  if(strcmp(command,"STOP")==0) {replayRunning=false;resetNavState();return;}')
header=header.replace('static void probeTick() {','static void probeTick() {\n  replayTick();')
if options.live:
    header=(ROOT/'firmware/tests/Box0RenderProbe.h').read_text(encoding='utf-8')
source=(ROOT/'firmware/.build/box0/build/sketch/MotoNav.ino.cpp').read_text(encoding='utf-8')
production=(ROOT/'firmware/boards/box0/MotoNav/MotoNav.ino').read_text(encoding='utf-8')
for name in ('rasterColorAt','rasterSmoothColor','rasterRows'):
    _,old_definition=function(source,name)
    _,new_definition=function(production,name)
    source=source.replace(old_definition,new_definition,1)
if options.nearest:
    source=source.replace(': rasterSmoothColor(U, V, useBack, backU, backV)',
                          ': rasterColorAt(U >> 16, V >> 16, useBack, backU, backV)')
cpp=out/'MotoNav-replay.cpp';cpp.write_text(instrument_sketch(source,header),encoding='utf-8')
commands=json.loads((ROOT/'firmware/.build/box0/build/compile_commands.json').read_text())
entry=next(c for c in commands if c['file'].endswith('MotoNav.ino.cpp'))
args=entry['arguments'];old=entry['file'];obj=out/'MotoNav-replay.o'
args=[str(cpp) if a==old else a for a in args];args[args.index('-o')+1]=str(obj)
if not Path(args[0]).exists():args[0]+='.exe'
print('Compiling replay using production flags',flush=True);subprocess.run(args,cwd=ROOT,check=True)
props={}
for line in (ROOT/'artifacts/box0-test/box0-build-properties.txt').read_text(encoding='utf-8-sig').splitlines():
 if '=' in line:
  k,v=line.split('=',1);props[k]=v
build=ROOT/'firmware/.build/box0/build'
objects=[obj]+sorted((build/'libraries').rglob('*.o'))
line=props['recipe.c.combine.pattern']
line=line.replace('{object_files}',' '.join('"'+str(p)+'"' for p in objects)).replace('{archive_file_path}',str(build/'core/core.a'))
line=line.replace(str(build).replace('\\','/')+'/MotoNav.ino.map',str(out/'replay.map'))
line=line.replace(str(build)+'\\MotoNav.ino.map',str(out/'replay.map'))
# Replace only output paths; all production libraries and flags remain unchanged.
line=line.replace(str(build)+'/MotoNav.ino.map',str(out/'replay.map')).replace(str(build)+'/MotoNav.ino.elf',str(out/'replay.elf'))
args=[x[1:-1] if x.startswith('"') and x.endswith('"') else x for x in shlex.split(line,posix=False)]
if not Path(args[0]).exists():args[0]+='.exe'
print('Linking replay with production libraries',flush=True);subprocess.run(args,cwd=ROOT,check=True)
esp=ROOT/'.tools/arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool.exe'
subprocess.run([str(esp),'--chip','esp32s3','elf2image','--flash-mode','dio','--flash-freq','80m','--flash-size','16MB','-o',str(out/'replay.bin'),str(out/'replay.elf')],check=True,cwd=ROOT)
print('Replay test firmware ready:',out/'replay.bin',flush=True)
