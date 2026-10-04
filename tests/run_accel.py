"""Exercise the real selection function without launching user disk images."""
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[1]
s=(root/'run.ps1').read_text()
function=s[s.index('function Select-PollikAccel {'):s.index('$accelSelection = Select-PollikAccel')]
build=root/'build'
for name,prelude in [
    ('unavailable', '''Add-Type -TypeDefinition 'public static class PollikHypervisor {public static int WHvGetCapability(uint c,out uint v,uint s,out uint w){v=0;w=4;return 0;}}'
$Accel='auto';$Cpu='max'
'''),
    ('qemu-rejected', "$Accel='auto';$Cpu='pollikos-invalid-probe-cpu'\n"
                      "$script:PSBoundParameters['Cpu']=$Cpu\n")]:
    code="$ErrorActionPreference='Stop'\n"+prelude+function+'''
$result=Select-PollikAccel
Write-Output ('RAW selection='+$result[0]+' reason='+$result[1])
if($result[0] -ne 'tcg'){throw 'auto fallback did not select tcg'}
Write-Output 'PASS auto fallback selects tcg'
'''
    script=build/f'm5-accel-{name}.ps1';script.write_text(code)
    r=subprocess.run(['pwsh','-NoProfile','-File',str(script)],cwd=root,capture_output=True,text=True)
    output=f'ACCEL CASE {name} exit={r.returncode}\n{r.stdout}{r.stderr}'
    (build/f'm5-accel-{name}.log').write_text(output)
    print(output,flush=True)
    assert r.returncode==0,output
