from pathlib import Path
from html import escape
import math

OUT = Path(__file__).parent
parts = []
def text(x,y,s,size=12,color='#F4F4F2',mono=True,weight=400):
    parts.append(f'<text x="{x}" y="{y}" fill="{color}" font-family="{ "Menlo,monospace" if mono else "Arial,sans-serif"}" font-size="{size}" font-weight="{weight}">{escape(s)}</text>')
def rect(x,y,w,h,fill='#101010',stroke='#323232',r=4):
    parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}"/>')
def path(d,color='#777773',width=1):
    parts.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}"/>')
def button(x,y,w,s,selected=False):
    rect(x,y,w,28,'#F4F4F2' if selected else '#171717')
    text(x+10,y+19,s,color='#0A0A0A' if selected else '#F4F4F2')
def label(x,y,s): text(x,y,s,color='#989894')

rect(0,0,1440,1000,'#0A0A0A','#0A0A0A',0)
text(32,57,'PsyRoot',38,mono=False,weight=600)
label(213,55,'42CODON')
label(375,32,'PRESET'); text(375,57,'INIT / ROLLING',18,mono=False)
label(710,32,'HOST'); text(710,57,'145 BPM',18,mono=False)
label(865,32,'MIDI IN'); text(865,57,'IDLE',18,mono=False)
label(1045,32,'TRIAL'); text(1045,57,'7 DAYS LEFT',16,mono=False)
button(1267,29,141,'ACTIVATE →')
path('M32 85H1408','#323232')

text(32,126,'Pattern',28,mono=False,weight=600)
button(228,103,164,'ORDER  UP ▾')
button(408,103,140,'LATCH  OFF')
text(581,122,'SWING  0%')
label(790,122,'HOST SYNC')
label(1035,122,'HOLD A ROOT NOTE TO PLAY')
label(32,166,'16 STEPS / 1 BAR')
for n in range(16):
    x=256+n*71
    label(x+24,166,str(n+1))
    if n%4==0: path(f'M{x-5} 175V424','#454541')
colors=['#BACB9B','#91BDBB','#B5A8C7']
for i,name in enumerate(['ROLLING','SLIDING','FX']):
    y=183+i*80
    text(32,y+19,f'0{i+1} / {name}',16,colors[i],False,600)
    label(32,y+41,'ON · SEQ' if i==0 else 'OFF · SEQ')
    button(32,y+49,193,'TRANSPOSE  +0 st')
    for n in range(16):
        x=256+n*71
        hit=i==0 and n%4!=0
        rect(x,y,64,42,'#BECDA5' if hit else '#111111','#F4F4F2' if i==0 and n==1 else '#323232')
        if hit:
            text(x+13,y+26,'+0',14,'#101010')
            path(f'M{x+8} {y+35}H{x+56}','#536044',2)
    label(256,y+65,'RATE 1/16    LENGTH 16    SWING GLOBAL · 0%')
    label(858,y+65,'12 HITS · NO HELD NOTE' if i==0 else 'EMPTY PATTERN · ENGINE OFF')

rect(32,439,1376,74)
label(48,462,'ROLL / STEP 02')
button(48,475,90,'ON',True)
label(175,462,'OFFSET'); text(175,494,'+0 st',16)
label(300,462,'GATE'); text(300,494,'50%',16)
label(400,462,'VELOCITY'); text(400,494,'100',16)
label(530,462,'ACCENT'); text(530,494,'OFF',16)
label(650,462,'TIE'); text(650,494,'OFF',16)
label(755,462,'GLIDE'); text(755,494,'FOLLOW LANE ▾',14)
label(1010,462,'EFFECTIVE GLIDE'); text(1010,494,'—',16)
label(1220,462,'NOTE'); text(1220,494,'—',16)

settings=[
    [('CUTOFF','900 Hz'),('ENV AMT','60%'),('FILTER D','60 ms'),('AMP D','70 ms')],
    [('CUTOFF','1.20 kHz'),('RESONANCE','45%'),('GLIDE SEQ','40%'),('ACCENT','50%')],
    [('FM AMOUNT','30%'),('FORMANT','50%'),('MOVEMENT','40%'),('RATE','1/8')]
]
for i,name in enumerate(['Rolling','Sliding','FX']):
    x=32+i*464
    rect(x,532,448,266)
    text(x+16,562,name,22,colors[i],False,600)
    button(x+322,545,110,'ON' if i==0 else 'OFF',i==0)
    label(x+16,591,'SOURCE');button(x+85,573,65,'SEQ',True)
    text(x+163,592,'MIDI');label(x+300,592,'LEVEL 0 dB')
    for j,(name,value) in enumerate(settings[i]):
        cx=x+57+j*108
        parts.append(f'<circle cx="{cx}" cy="633" r="19" fill="#141414" stroke="#60605C"/>')
        path(f'M{cx} 617V627','#F4F4F2',2)
        label(x+16+j*108,670,name)
        text(x+16+j*108,694,value,16,mono=False)
    if i==1: label(x+232,714,'≈41 ms')
    path(f'M{x+16} 728H{x+432}','#323232')
    if i==0:
        text(x+16,752,'PHASE RESET ON · FILTER RESET ON')
        label(x+16,781,'LOW CUT AUTO → OFF')
    elif i==1:
        text(x+16,752,'LEGATO · TIED NOTES ONLY')
        label(x+16,781,'LOW CUT AUTO → ON · 100 Hz')
    else:
        text(x+16,752,'LOW CUT ON · 150 Hz')
        label(x+16,781,'MOVEMENT SYNCED TO HOST')
    text(x+350,781,'DETAILS')

path('M32 821H1408','#323232')
label(32,850,'KICK DUCK')
text(32,879,'45%',24,mono=False)
text(108,879,'120 ms',20,mono=False)
button(220,853,65,'EXP',True);button(293,853,65,'LIN')
path('M32 920V897H48Q65 928 96 928Q145 928 202 897H350','#F4F4F2',1.5)
label(32,952,'TARGETS  ROLL ON · SLIDE ON · FX OFF')
label(395,850,'DUCK SOURCE')
text(395,879,'INTERNAL ▾',16)
label(395,908,'1/4 NOTE · STOPPED')
label(395,952,'NO AUTOMATIC FALLBACK')
label(695,850,'SUB OWNER')
button(695,861,71,'ROLL',True);button(774,861,79,'SLIDE');button(861,861,62,'OFF')
label(695,920,'MONO LOW');text(695,949,'120 Hz',18,mono=False)
label(880,920,'DRIVE');text(880,949,'0%',18,mono=False)
label(1020,850,'QUALITY')
button(1020,861,55,'2x',True);button(1083,861,55,'4x')
label(1215,850,'OUTPUT');text(1215,880,'0.0 dB',22,mono=False)
label(1020,952,'INTERFACE STUDY / 03')

body='\n'.join(parts)
for name,height in [('psyroot-interface-v3.svg',1000),('render-square.svg',1440)]:
    (OUT/name).write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="1440" height="{height}" viewBox="0 0 1440 {height}">\n{body}\n</svg>\n')
