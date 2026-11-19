"""Local USB-only music trigger and private log capture; never flashes firmware."""
import argparse,os,re,time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
import serial
from serial.tools import list_ports
os.umask(0o077)
a=argparse.ArgumentParser();a.add_argument('action',choices=['play','stop','status']);a.add_argument('index',nargs='?',type=int,default=0);a.add_argument('--seconds',type=int,default=20);a.add_argument('--port');args=a.parse_args()
assert 0<=args.index<128 and 1<=args.seconds<=120
ports=[p.device for p in list_ports.comports() if p.vid==0x303a and p.pid==0x1001];ports=[args.port] if args.port else ports
assert len(ports)==1, 'Use --port when zero or multiple ESP32 USB ports are present: '+str(ports)
p=serial.Serial(port=None,baudrate=115200,timeout=.2,write_timeout=2);p.dtr=False;p.rts=False;p.port=ports[0]
log=(ROOT / '.local/logs')/('music-debug-control-'+time.strftime('%Y%m%d-%H%M%S')+'.log');log.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
command='music-test '+args.action+(' '+str(args.index) if args.action=='play' else '')+'\n'
all_data=bytearray();sent=False;ready=False;opened_at=time.monotonic();deadline=opened_at+40;next_probe=opened_at+8
try:
 p.open()
 with log.open('wb') as f:
  while time.monotonic()<deadline:
   try:
    data=p.read(max(1,min(p.in_waiting,8192)))
   except (serial.SerialException,OSError):
    p.close();time.sleep(.5)
    try:p.open();opened_at=time.monotonic()
    except (serial.SerialException,OSError):pass
    continue
   if data:
    all_data.extend(data);f.write(data);f.flush()
    if b'-> idle' in all_data or b'TfMusicDebug: ready=' in all_data:ready=True
   if not sent and time.monotonic()>=next_probe:
    if b'TfMusicDebug: ready=1' in all_data:
     p.write(command.encode());sent=True;deadline=time.monotonic()+args.seconds
     print('Sent: '+command.strip(),flush=True)
    else:
     p.write(b'music-test status\n')
     next_probe=time.monotonic()+1

finally:
 if p.is_open:p.close()
print('Private log: '+str(log))
for line in all_data.decode('utf-8','replace').splitlines():
 if any(k in line for k in ['TfMusicDebug:', 'MusicDecoder:', 'NotifyPlayer:', 'Notification playback', 'State:']):
  print(line)
if not sent:raise SystemExit('Debug command not sent: diagnostic endpoint/index not ready.')
