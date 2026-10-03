Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

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
!ifndef BITMAPS
  !error "BITMAPS is required (make-installer-bitmaps.sh's output)"
!endif

; Scaled with the display (system DPI), not bitmap-stretched by Windows —
; blurry at 125% and up. Its banners come in one bitmap per scale, the one
; for the display's picked at start (dpiGuiInit below).
ManifestDPIAware true

Name "CloudMus"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\CloudMus"
RequestExecutionLevel user
SetCompressor /SOLID lzma
; Through Modern UI, not Icon/UninstallIcon: it sets those itself, to
; its own default icons.
!define MUI_ICON "${CLOUDMUS_ICON}"
!define MUI_UNICON "${CLOUDMUS_ICON}"
!define MUI_ABORTWARNING
; The 100% ones: replaced with the display's own scale in dpiGuiInit.
!define MUI_WELCOMEFINISHPAGE_BITMAP "${BITMAPS}/welcome-100.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${BITMAPS}/header-100.bmp"
!define MUI_CUSTOMFUNCTION_GUIINIT dpiGuiInit
!define MUI_CUSTOMFUNCTION_UNGUIINIT un.dpiGuiInit
; Its image control (${NSD_CreateBitmap} in MUI2's Welcome.nsh) is sized
; from dialog units, not a clean percentage of 164x314 — close to
; dpiGuiInit's swapped-in bitmap at the display's scale, but usually not
; pixel-exact, and a static control stretches to fill whatever size it
; ended up at, distorting the picture. resizeWelcomeImage forces it back
; to the bitmap's own exact size once the page (and so the control) exists.
; /UPDATE (an update the app downloaded itself, see Update::Installer):
; every page but the progress one is skipped (skipWhenUpdating) — the
; installer shows, but asks nothing.
!define MUI_PAGE_CUSTOMFUNCTION_PRE skipWhenUpdating
!define MUI_PAGE_CUSTOMFUNCTION_SHOW resizeWelcomeImage
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_PRE skipWhenUpdating
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\cloudmus-qt.exe"
!define MUI_PAGE_CUSTOMFUNCTION_PRE skipWhenUpdating
!define MUI_PAGE_CUSTOMFUNCTION_SHOW resizeFinishImage
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; The bitmaps' scales, as make-installer-bitmaps.sh renders them.
Var DpiScale
; 1 when run with /UPDATE (by the app itself; .onInit).
Var UpdateMode

; $DpiScale: the smallest scale at least the display's (system DPI).
!macro PICK_DPI_SCALE
  System::Call 'USER32::GetDC(p0)p.r0'
  System::Call 'GDI32::GetDeviceCaps(pr0,i88)i.r1' ; LOGPIXELSX
  System::Call 'USER32::ReleaseDC(p0,pr0)'
  IntOp $1 $1 * 100
  IntOp $1 $1 / 96
  ${If} $1 <= 100
    StrCpy $DpiScale 100
  ${ElseIf} $1 <= 125
    StrCpy $DpiScale 125
  ${ElseIf} $1 <= 150
    StrCpy $DpiScale 150
  ${ElseIf} $1 <= 175
    StrCpy $DpiScale 175
  ${ElseIf} $1 <= 200
    StrCpy $DpiScale 200
  ${ElseIf} $1 <= 250
    StrCpy $DpiScale 250
  ${Else}
    StrCpy $DpiScale 300
  ${EndIf}
!macroend

!macro EXTRACT_SCALED NAME TARGET
  ${If} $DpiScale == 100
    File "/oname=${TARGET}" "${BITMAPS}/${NAME}-100.bmp"
  ${ElseIf} $DpiScale == 125
    File "/oname=${TARGET}" "${BITMAPS}/${NAME}-125.bmp"
  ${ElseIf} $DpiScale == 150
    File "/oname=${TARGET}" "${BITMAPS}/${NAME}-150.bmp"
  ${ElseIf} $DpiScale == 175
    File "/oname=${TARGET}" "${BITMAPS}/${NAME}-175.bmp"
  ${ElseIf} $DpiScale == 200
    File "/oname=${TARGET}" "${BITMAPS}/${NAME}-200.bmp"
  ${ElseIf} $DpiScale == 250
    File "/oname=${TARGET}" "${BITMAPS}/${NAME}-250.bmp"
  ${Else}
    File "/oname=${TARGET}" "${BITMAPS}/${NAME}-300.bmp"
  ${EndIf}
!macroend

; Runs after Modern UI's own GUI init, which unpacked and loaded its 100%
; bitmaps: the side one is overwritten before its pages load it, the
; header one is loaded again.
!macro DPI_GUIINIT UN
  Function ${UN}dpiGuiInit
    Push $0
    Push $1
    !insertmacro PICK_DPI_SCALE
    !if "${UN}" == ""
      !insertmacro EXTRACT_SCALED welcome "$PLUGINSDIR\modern-wizard.bmp"
    !endif
    !insertmacro EXTRACT_SCALED header "$PLUGINSDIR\modern-header.bmp"
    SetBrandingImage /IMGID=1046 /RESIZETOFIT "$PLUGINSDIR\modern-header.bmp"
    Pop $1
    Pop $0
  FunctionEnd
!macroend
!insertmacro DPI_GUIINIT ""
!insertmacro DPI_GUIINIT "un."

; $DpiScale's own welcome-<scale>.bmp is exactly 164xDpiScale% by
; 314xDpiScale% (make-installer-bitmaps.sh renders every scale off the
; same 164x314 SVG, just zoomed) — the size MUI_PAGE_CUSTOMFUNCTION_SHOW
; forces $VAR (the page's bitmap control, already created by the time SHOW
; runs) to, so it always exactly matches what's actually loaded into it.
!macro RESIZE_WIZARD_IMAGE VAR
  Push $0
  Push $1
  Push $2
  IntOp $1 164 * $DpiScale
  IntOp $1 $1 / 100
  IntOp $2 314 * $DpiScale
  IntOp $2 $2 / 100
  StrCpy $0 ${VAR}
  ; SetWindowPos(hWnd, NULL, 0, 0, cx, cy, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)
  System::Call 'USER32::SetWindowPos(pr0,p0,i0,i0,ir1,ir2,i0x16)'
  Pop $2
  Pop $1
  Pop $0
!macroend

Function resizeWelcomeImage
  !insertmacro RESIZE_WIZARD_IMAGE $mui.WelcomePage.Image
FunctionEnd

Function resizeFinishImage
  !insertmacro RESIZE_WIZARD_IMAGE $mui.FinishPage.Image
FunctionEnd

Function .onInit
  SetShellVarContext current
  StrCpy $UpdateMode 0
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/UPDATE" $1
  ${IfNot} ${Errors}
    StrCpy $UpdateMode 1
  ${EndIf}
FunctionEnd

; A page's PRE callback: Abort there skips the page.
Function skipWhenUpdating
  ${If} $UpdateMode == 1
    Abort
  ${EndIf}
FunctionEnd

; The app quit to be updated: start the new version in its place.
Function .onInstSuccess
  ${If} $UpdateMode == 1
    Exec '"$INSTDIR\cloudmus-qt.exe"'
  ${EndIf}
FunctionEnd

Section "CloudMus" SEC_MAIN
  ; Windows will not rename a running executable. Check before replacing a
  ; previous installation so an update cannot leave a mixed DLL set.
  IfFileExists "$INSTDIR\cloudmus-qt.exe" 0 install_files
  StrCpy $1 0
try_rename:
  ClearErrors
  Rename "$INSTDIR\cloudmus-qt.exe" "$INSTDIR\cloudmus-qt.exe.updating"
  IfErrors 0 renamed
  ; /UPDATE: the app started this just before quitting, and is still
  ; shutting its backends down — wait for it, up to 30 s.
  StrCmp $UpdateMode 1 0 running
  IntCmp $1 60 running
  IntOp $1 $1 + 1
  Sleep 500
  Goto try_rename
renamed:
  Rename "$INSTDIR\cloudmus-qt.exe.updating" "$INSTDIR\cloudmus-qt.exe"
  Goto install_files
running:
  IfSilent +2 0
    MessageBox MB_ICONEXCLAMATION|MB_OK "Close CloudMus before updating it."
  SetErrorLevel 1
  Abort
install_files:
  ${If} $UpdateMode == 1
    SetAutoClose true
  ${EndIf}
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
