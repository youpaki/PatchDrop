"""Package built PatchLink artefacts into release zips and upload to PatchDrop.
Usage: python package_release.py
Reads: ../patchlink/build/PatchLink_artefacts/Release/
Uploads to ~/patchdrop/public/releases/ on gg.trenas.fr
"""
import os, shutil, zipfile, paramiko

SSH_PASSWORD = os.environ.get('PATCHDROP_SSH_PASSWORD', '')

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.normpath(os.path.join(HERE, '..', '..', 'patchlink', 'build', 'PatchLink_artefacts', 'Release'))
OUT = os.path.normpath(os.path.join(HERE, '..', '..', 'patchdrop', 'releases'))
os.makedirs(OUT, exist_ok=True)

INSTALL_TXT = """PatchLink {ver} - Windows 64-bit
================================
1. Close your DAW.
2. Copy the folder "PatchLink.vst3" to: C:\\Program Files\\Common Files\\VST3\\
3. (Optional) Copy PatchLink.exe anywhere - it runs standalone, no install needed.
4. Reopen FL Studio / Ableton, rescan plugins, put PatchLink on a track.
5. Share: "Share: get code" - friend clicks "Open via code" in their instance.

VST hosting inside PatchLink: your VST3 plugins are scanned from the
standard VST3 folders automatically (click Scan in the plugin).

Friend missing a plugin? The patch reports it by name instead of crashing.
"""

entries = []
vst3 = os.path.join(ART, 'VST3', 'PatchLink.vst3')
exe = os.path.join(ART, 'Standalone', 'PatchLink.exe')
print('VST3:', os.path.isdir(vst3), vst3)
print('EXE :', os.path.isfile(exe), exe)

if os.path.isdir(vst3) and os.path.isfile(exe):
    zpath = os.path.join(OUT, 'PatchLink-Windows.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(vst3):
            for f in files:
                full = os.path.join(root, f)
                z.write(full, os.path.join('PatchLink.vst3', os.path.relpath(full, vst3)))
        z.write(exe, 'PatchLink.exe')
        z.writestr('INSTALL.txt', INSTALL_TXT.format(ver='1.0.0'))
    print('wrote', zpath, os.path.getsize(zpath) // 1024, 'KB')
    entries.append(zpath)
else:
    print('BUILD ARTEFACTS MISSING - build first')
    raise SystemExit(1)

c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect('gg.trenas.fr', username='leo', password=SSH_PASSWORD, timeout=20)
sftp = c.open_sftp()
for z in entries:
    sftp.put(z, '/home/leo/patchdrop/public/releases/' + os.path.basename(z))
    print('uploaded', os.path.basename(z))
sin, sou, ser = c.exec_command("ls -la ~/patchdrop/public/releases/ && curl -s -o /dev/null -w 'zip HTTP %{http_code}, %{size_download} bytes' http://127.0.0.1:3250/releases/PatchLink-Windows.zip; echo", timeout=30)
print(sou.read().decode(errors='replace'))
c.close()
print('DONE - https://gg.trenas.fr/patchdrop/releases/PatchLink-Windows.zip')
