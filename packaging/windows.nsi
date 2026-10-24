Unicode true
!include "MUI2.nsh"
!ifndef DIST
  !error "DIST must name the prepared Windows package"
!endif
!ifndef OUTPUT
  !define OUTPUT "BaawiilMonitor-0.2.0-dev-setup.exe"
!endif
!ifndef DELETE_MANIFEST
  !error "DELETE_MANIFEST must list the exact installed files"
!endif
Name "Baawiil Monitor (développement)"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\BaawiilMonitor"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "French"
!macro CheckRunning FunctionName
Function ${FunctionName}
retry:
  System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\BaawiilMonitorRuntime") p.r0 ?e'
  Pop $1
  StrCmp $0 0 inaccessible
  System::Call 'kernel32::CloseHandle(p r0)'
  Goto busy
inaccessible:
  StrCmp $1 2 done
busy:
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Fermez Baawiil et ses composants de travail/administration avant de continuer." IDRETRY retry
  Abort
done:
FunctionEnd
!macroend
!insertmacro CheckRunning ".onInit"
!insertmacro CheckRunning "un.onInit"
Section "Programme"
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${DIST}/*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortcut "$SMPROGRAMS\Baawiil Monitor.lnk" "$INSTDIR\baawiil.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BaawiilMonitor" "DisplayName" "Baawiil Monitor (développement)"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BaawiilMonitor" "DisplayVersion" "0.2.0-dev"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BaawiilMonitor" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BaawiilMonitor" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BaawiilMonitor" "NoRepair" 1
SectionEnd
Section "Uninstall"
  SetShellVarContext current
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Baawiil Monitor"
  StrCmp $0 '$\"$INSTDIR\baawiil.exe$\"' 0 +2
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Baawiil Monitor"
  Delete "$SMPROGRAMS\Baawiil Monitor.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\BaawiilMonitor"
  ; Generated explicit list: preserve any unrelated files added to the directory.
  !include "${DELETE_MANIFEST}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
