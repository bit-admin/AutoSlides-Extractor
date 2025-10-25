; AutoSlides Extractor NSIS Installer Script
; Copyright (c) 2025 bit-admin
; Licensed under MIT License

;--------------------------------
; Includes
!include "MUI2.nsh"

;--------------------------------
; General Configuration
Name "AutoSlides Extractor"
OutFile "${OUTFILE}"
Unicode True

; Default installation directory
InstallDir "$PROGRAMFILES\AutoSlides Extractor"

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\AutoSlidesExtractor" "Install_Dir"

; Request application privileges for Windows Vista/7/8/10/11
RequestExecutionLevel admin

; Version Information
VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "AutoSlides Extractor"
VIAddVersionKey "CompanyName" "AutoSlides Project"
VIAddVersionKey "LegalCopyright" "Copyright © 2025 bit-admin"
VIAddVersionKey "FileDescription" "AutoSlides Extractor Installer"
VIAddVersionKey "FileVersion" "1.0.0.0"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "InternalName" "AutoSlidesExtractor"
VIAddVersionKey "OriginalFilename" "AutoSlidesExtractor-Setup.exe"

;--------------------------------
; Interface Settings
!define MUI_ABORTWARNING
!define MUI_ICON "icon.ico"
!define MUI_UNICON "icon.ico"

; Header image (optional - can be added later)
; !define MUI_HEADERIMAGE
; !define MUI_HEADERIMAGE_BITMAP "header.bmp"

;--------------------------------
; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\AutoSlidesExtractor.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch AutoSlides Extractor"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages
!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Installer Sections

; Core application files (required)
Section "AutoSlides Extractor (required)" SecCore
  SectionIn RO

  ; Set output path to the installation directory
  SetOutPath $INSTDIR

  ; Copy application files
  File /r "files\*.*"

  ; Write the installation path into the registry
  WriteRegStr HKLM "Software\AutoSlidesExtractor" "Install_Dir" "$INSTDIR"

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor" "DisplayName" "AutoSlides Extractor"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor" "DisplayIcon" "$INSTDIR\AutoSlidesExtractor.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor" "Publisher" "AutoSlides Project"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor" "DisplayVersion" "${VERSION}"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor" "NoRepair" 1

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; Start Menu shortcuts
Section "Start Menu Shortcuts" SecStartMenu
  CreateDirectory "$SMPROGRAMS\AutoSlides Extractor"
  CreateShortcut "$SMPROGRAMS\AutoSlides Extractor\AutoSlides Extractor.lnk" "$INSTDIR\AutoSlidesExtractor.exe"
  CreateShortcut "$SMPROGRAMS\AutoSlides Extractor\Uninstall.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

; Desktop shortcut
Section "Desktop Shortcut" SecDesktop
  CreateShortcut "$DESKTOP\AutoSlides Extractor.lnk" "$INSTDIR\AutoSlidesExtractor.exe"
SectionEnd

; File associations
Section "MP4 File Association" SecFileAssoc
  ; Register file association for .mp4 files
  WriteRegStr HKCR ".mp4" "" "AutoSlides.MP4File"
  WriteRegStr HKCR ".mp4" "Content Type" "video/mp4"

  ; Add context menu entry
  WriteRegStr HKCR "AutoSlides.MP4File" "" "MP4 Video File"
  WriteRegStr HKCR "AutoSlides.MP4File\DefaultIcon" "" "$INSTDIR\AutoSlidesExtractor.exe,0"
  WriteRegStr HKCR "AutoSlides.MP4File\shell" "" "open"
  WriteRegStr HKCR "AutoSlides.MP4File\shell\open" "" "Open with AutoSlides Extractor"
  WriteRegStr HKCR "AutoSlides.MP4File\shell\open\command" "" '"$INSTDIR\AutoSlidesExtractor.exe" "%1"'

  ; Add "Extract Slides" context menu option
  WriteRegStr HKCR "AutoSlides.MP4File\shell\extract" "" "Extract Slides with AutoSlides Extractor"
  WriteRegStr HKCR "AutoSlides.MP4File\shell\extract\command" "" '"$INSTDIR\AutoSlidesExtractor.exe" "%1"'

  ; Refresh shell
  System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
SectionEnd

;--------------------------------
; Section Descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} "Core application files (required)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Create shortcuts in Start Menu"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create shortcut on Desktop"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecFileAssoc} "Associate MP4 files with AutoSlides Extractor"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Uninstaller Section

Section "Uninstall"
  ; Ask user about keeping configuration
  MessageBox MB_YESNO|MB_ICONQUESTION "Do you want to keep your application settings and configurations?" IDYES KeepConfig

  ; Remove configuration from registry
  DeleteRegKey HKCU "Software\AutoSlidesExtractor"

  KeepConfig:

  ; Remove file associations
  DeleteRegKey HKCR ".mp4"
  DeleteRegKey HKCR "AutoSlides.MP4File"

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor"
  DeleteRegKey HKLM "Software\AutoSlidesExtractor"

  ; Remove files and uninstaller
  Delete "$INSTDIR\AutoSlidesExtractor.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\uninstall.exe"

  ; Remove shortcuts
  Delete "$SMPROGRAMS\AutoSlides Extractor\*.*"
  Delete "$DESKTOP\AutoSlides Extractor.lnk"

  ; Remove directories
  RMDir "$SMPROGRAMS\AutoSlides Extractor"
  RMDir /r "$INSTDIR"

  ; Refresh shell
  System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
SectionEnd

;--------------------------------
; Functions

; Function to run on installer initialization
Function .onInit
  ; Check if already installed
  ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\AutoSlidesExtractor" "UninstallString"
  StrCmp $R0 "" done

  MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
  "AutoSlides Extractor is already installed. $\n$\nClick 'OK' to remove the previous version or 'Cancel' to cancel this upgrade." \
  IDOK uninst
  Abort

  uninst:
    ClearErrors
    ExecWait '$R0 _?=$INSTDIR'

    IfErrors no_remove_uninstaller done
    no_remove_uninstaller:

  done:
FunctionEnd