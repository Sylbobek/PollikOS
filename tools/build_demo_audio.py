"""Reproducible original 2-second PCM test sound, shipped as a system file."""
import io,math,struct,wave
def demo_wave():
    data=io.BytesIO()
    with wave.open(data,'wb') as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000)
        pcm=bytearray()
        for n in range(96000):
            # A smooth envelope makes start/stop samples quiet.
            fade=min(1,n/2400,(95999-n)/2400)
            x=int(6000*fade*math.sin(2*math.pi*440*n/48000));pcm+=struct.pack('<hh',x,x)
        w.writeframes(pcm)
    return data.getvalue()
