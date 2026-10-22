"""Compile existing EEPROM fault-injection harness against current sources."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
vc = r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
with tempfile.TemporaryDirectory(prefix='joker-storage-10200001-') as tmp:
    work=Path(tmp)
    includes=' '.join('/I"'+str(root/p)+'"' for p in ('BSP/Inc','Service/Inc','Tests/stubs'))
    command=f'call "{vc}" >nul && cl /nologo /TC /utf-8 /W3 /DEEPROM_GUARD_TEST_EXTERNAL {includes} "{root / "Tests/storage_fault_harness.c"}" /Fe:storage.exe'
    result=subprocess.run(command,shell=True,cwd=work,capture_output=True)
    if result.returncode:
        raise RuntimeError((result.stdout+result.stderr).decode(errors='replace'))
    subprocess.run([str(work/'storage.exe')],cwd=work,check=True)
