Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

!ifndef VERSION
  !error "VERSION is required"
!endif
!ifndef STAGING
  !error "STAGING is required"
!endif
!ifndef OUTPUT
  !error "OUTPUT is required"
!endif

!ifndef CLOUDMUS_ICON
  !error "CLOUDMUS_ICON is required"
!endif

Name "CloudMus"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\CloudMus"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Icon "${CLOUDMUS_ICON}"
UninstallIcon "${CLOUDMUS_ICON}"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\cloudmus-qt.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  SetShellVarContext current
FunctionEnd

Section "CloudMus" SEC_MAIN
  ; Windows will not rename a running executable. Check before replacing a
  ; previous installation so an update cannot leave a mixed DLL set.
  IfFileExists "$INSTDIR\cloudmus-qt.exe" 0 install_files
  ClearErrors
  Rename "$INSTDIR\cloudmus-qt.exe" "$INSTDIR\cloudmus-qt.exe.updating"
  IfErrors running
  Rename "$INSTDIR\cloudmus-qt.exe.updating" "$INSTDIR\cloudmus-qt.exe"
  Goto install_files
running:
  IfSilent +2 0
    MessageBox MB_ICONEXCLAMATION|MB_OK "Close CloudMus before updating it."
  SetErrorLevel 1
  Abort
install_files:
  RMDir /r "$INSTDIR"
  SetOutPath "$INSTDIR"
  File /r "${STAGING}/*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\CloudMus"
  CreateShortCut "$SMPROGRAMS\CloudMus\CloudMus.lnk" "$INSTDIR\cloudmus-qt.exe"
  CreateShortCut "$SMPROGRAMS\CloudMus\Uninstall CloudMus.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CloudMus" \
      "DisplayName" "CloudMus"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CloudMus" \
      "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CloudMus" \
      "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CloudMus" \
      "DisplayIcon" "$INSTDIR\cloudmus-qt.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CloudMus" \
      "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CloudMus" \
      "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CloudMus" \
      "NoRepair" 1
SectionEnd

Section /o "Desktop shortcut" SEC_DESKTOP
  CreateShortCut "$DESKTOP\CloudMus.lnk" "$INSTDIR\cloudmus-qt.exe"
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "CloudMus"
  StrCmp $0 '"$INSTDIR\cloudmus-qt.exe" --autostart' 0 +2
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "CloudMus"

  Delete "$DESKTOP\CloudMus.lnk"
  RMDir /r "$SMPROGRAMS\CloudMus"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\CloudMus"
  RMDir /r "$INSTDIR"
SectionEnd
