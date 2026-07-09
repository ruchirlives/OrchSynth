[Setup]
AppId={{B3E4C8A1-5F2D-4E6A-9C7B-1D8F3A2E4B6C}
AppName=OrchSynth
AppVersion=1.0.0
AppPublisher=OrchSynth
DefaultDirName={autopf}\OrchSynth
DefaultGroupName=OrchSynth
OutputDir=installer
OutputBaseFilename=OrchSynth-Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
SetupIconFile=build\VST3\Release\OrchSynth.vst3\PlugIn.ico
UninstallDisplayIcon={app}\OrchSynth.vst3

[Files]
Source: "build\VST3\Release\OrchSynth.vst3\Contents\x86_64-win\OrchSynth.vst3"; DestDir: "{commoncf64}\VST3\OrchSynth.vst3\Contents\x86_64-win"
Source: "build\VST3\Release\OrchSynth.vst3\Contents\x86_64-win\faust.dll"; DestDir: "{commoncf64}\VST3\OrchSynth.vst3\Contents\x86_64-win"
Source: "build\VST3\Release\OrchSynth.vst3\Contents\Resources\moduleinfo.json"; DestDir: "{commoncf64}\VST3\OrchSynth.vst3\Contents\Resources"
Source: "build\VST3\Release\OrchSynth.vst3\PlugIn.ico"; DestDir: "{commoncf64}\VST3\OrchSynth.vst3"

[Icons]
Name: "{group}\Uninstall OrchSynth"; Filename: "{uninstallexe}"
