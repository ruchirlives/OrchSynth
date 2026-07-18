# Deploying OrchSynth Standalone VST3

This checklist covers building and deploying the standalone `OrchSynth` VST3 Windows Setup installer to Gumroad.

## 1. Prerequisites

- **Inno Setup 6** must be installed in `C:\Program Files (x86)\Inno Setup 6`.
- **Microsoft Visual Studio** (latest MSBuild/CMake) must be installed to compile the C++ source.
- **Python Runtime Environment** inside `E:\CODINGPROJECTS\Python\orch` must have the `gumroad_upload` tool ready with active credentials in `tools/gumroad_upload/.env`.

---

## 2. Compile VST3 & Build Installer

Run the build compilation command from the repository root:

```cmd
.\build-installer.bat
```

This script will:
1. Compile the C++ Faust-backed synthesizer in `Release x64` configuration.
2. Copy the VST3 bundle (`OrchSynth.vst3`) and Faust JIT `.lib` runtime files to your local system folder `C:\Program Files\Common Files\VST3` (for local scanning).
3. Invoke Inno Setup compiler (`ISCC.exe`) on `installer.iss` to package the standalone setup installer.

Verify the output setup installer exists at:
`E:\CODINGPROJECTS\C++\OrchSynth\installer\OrchSynth-Setup.exe`

---

## 3. Upload Standalone Installer to Gumroad

Upload the newly compiled setup installer directly to the **plugins** page tab of the Orch product (`Yh1x5FDZsQ77s6tKin33zQ==`) using the python helper:

```powershell
& E:\CODINGPROJECTS\Python\orch\.venv-runtime\Scripts\python.exe E:\CODINGPROJECTS\Python\orch\tools\gumroad_upload\gumroad_upload.py attach 'Yh1x5FDZsQ77s6tKin33zQ==' 'E:\CODINGPROJECTS\C++\OrchSynth\installer\OrchSynth-Setup.exe' --file-name 'OrchSynth-Setup.exe' --description 'Windows standalone VST3 plugin setup installer for OrchSynth v<VERSION>' --page 'plugins'
```

> [!IMPORTANT]
> When executing the upload command above, ensure you replace `<VERSION>` in the description and command parameters with the actual release version (e.g., `v3.6.2`) to maintain consistent version numbers across all files on Gumroad.

---

## 4. Reorder layouts

Re-run the Gumroad ordering command to ensure the new standalone plugin installer is properly positioned in the layout:

```powershell
& E:\CODINGPROJECTS\Python\orch\.venv-runtime\Scripts\python.exe E:\CODINGPROJECTS\Python\orch\tools\gumroad_upload\gumroad_upload.py active-order 'Yh1x5FDZsQ77s6tKin33zQ==' --windows-file-id '<LATEST_CORE_SETUP_ID>' --mac-file-id '<LATEST_USER_MODULES_SETUP_ID>' --mac-file-id '<MAC_ARM_ID>' --mac-file-id '<MAC_INTEL_ID>'
```

Note: Replace/drop any stale standalone VST3 installer IDs on Gumroad using the `--drop-file-id` flag to prevent duplicate downloads.
