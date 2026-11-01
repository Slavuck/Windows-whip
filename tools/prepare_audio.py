"""Prepare the local Pixabay pack. Requires FFmpeg; never changes source files."""
import array
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE = 48000

def prepare(source, destination, impact):
    raw = subprocess.run([shutil.which('ffmpeg'), '-v','error','-i',str(source),'-map','0:a:0','-vn','-ac','1','-ar',str(RATE),'-f','f32le','pipe:1'],capture_output=True,check=True).stdout
    samples = array.array('f'); samples.frombytes(raw)
    # A 1 ms envelope distinguishes codec noise from the audible attack.
    envelope = [math.sqrt(sum(x*x for x in samples[i:i+48])/len(samples[i:i+48])) for i in range(0,len(samples),48)]
    threshold = max(10**(-48/20), max(envelope)*0.012)
    active = [i for i,v in enumerate(envelope) if v >= threshold]
    if not active:
        raise ValueError(f'No audible signal: {source}')
    start = max(0,active[0]*48-48)
    if impact:
        # Locate the first strong transient near the main peak, rather than
        # leaving a long quiet wind-up in the impact-only sample.
        peak = max(range(len(envelope)),key=envelope.__getitem__)
        attack = next((i for i in range(max(active[0],peak-180),peak+1) if envelope[i]>=max(envelope)*0.32),active[0])
        start = max(start,(attack-1)*48)
    else:
        # Discard inaudible wind-up noise, retaining 4 ms before a sustained
        # audible whoosh. A -48 dB gate alone kept >100 ms of near-silence.
        onset = max(10**(-36/20),max(envelope)*0.06)
        audible = next((i for i in active if i+6 <= len(envelope) and sum(v>=onset for v in envelope[i:i+6])>=4),active[0])
        start = max(start,(audible-4)*48)
    end = min(len(samples),(active[-1]+1)*48+int(0.045*RATE))
    selected = samples[start:end]
    rms = math.sqrt(sum(x*x for x in selected)/len(selected))
    peak = max(abs(x) for x in selected)
    gain = min(10**(-2/20)/peak,10**(-17/20)/max(rms,1e-8))
    pcm = array.array('h')
    for i,x in enumerate(selected):
        fade = min(1.0,i/24,(len(selected)-1-i)/192)
        pcm.append(round(max(-1,min(1,x*gain*max(0,fade)))*32767))
    with wave.open(str(destination),'wb') as out:
        out.setnchannels(1);out.setsampwidth(2);out.setframerate(RATE);out.writeframes(pcm.tobytes())
    return {'source':source.name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'output':destination.name,'output_sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),'trim_start_frames':start,'trim_end_frames':end,'gain':round(gain,8),'sample_rate':RATE,'channels':1,'bits':16,'duration_ms':round(len(pcm)*1000/RATE,3),'peak_dbfs':round(20*math.log10(max(abs(x) for x in pcm)/32768),3),'attack_marker_frames':0,'target_rms_dbfs':-17,'peak_ceiling_dbfs':-2,'swing_onset_dbfs':-36 if not impact else None,'swing_onset_relative':0.06 if not impact else None,'swing_preroll_ms':4 if not impact else None}

def main():
    if not shutil.which('ffmpeg'):
        raise SystemExit('FFmpeg is required only to prepare the local audio pack.')
    source = ROOT/'Audio';target = ROOT/'resources'/'audio';target.mkdir(parents=True,exist_ok=True)
    hits=sorted(p for p in source.glob('*.mp3') if 'удар' in p.stem.lower())
    swings=sorted(p for p in source.glob('*.mp3') if 'взмах' in p.stem.lower())
    if len(hits)!=1 or len(swings)!=2:
        raise SystemExit('Expected one удар MP3 and two взмах MP3 files in Audio/.')
    entries=[prepare(hits[0],target/'hit_01.wav',True)]
    entries += [prepare(p,target/f'swing_{i:02}.wav',False) for i,p in enumerate(swings,1)]
    manifest={'format_version':2,'source':'Pixabay','processing':'FFmpeg float decode; audible onset gate; deterministic mono PCM preparation','samples':entries}
    (target/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    for item in entries:print(f"{item['output']}: {item['duration_ms']} ms, trim {item['trim_start_frames']/48:.1f} ms, peak {item['peak_dbfs']} dBFS")

if __name__=='__main__':main()
