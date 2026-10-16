/* cubolt.nsi -- NSIS installer: Program Files, Start Menu, a desktop shortcut, an uninstaller. */
/* Build: makensis -DVERSION=1.2.3 cubolt.nsi  (what `make installer` passes: version.h's) */

Unicode true

!include "MUI2.nsh"
!include "x64.nsh"

!ifndef VERSION
  !error "build with -DVERSION=<version>; version.h is the one place it lives"
!endif

!ifndef BIN
  !define BIN "..\bin"
!endif

!define APPNAME    "Cubolt"
!define PUBLISHER  "Zixing Wang"
!define AUTHORMAIL "zixingwang.cn@gmail.com"
!define COPYYEAR   "2026"
!define EXENAME    "cubolt.exe"
!define REGKEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\Cubolt"

Name "${APPNAME} ${VERSION}"
OutFile "CuboltSetup-${VERSION}.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "${REGKEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName"     "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} setup"
VIAddVersionKey "FileVersion"     "${VERSION}"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "CompanyName"     "${PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "Copyright (C) ${COPYYEAR} ${PUBLISHER} <${AUTHORMAIL}>"

!define MUI_ABORTWARNING
!define MUI_CUSTOMFUNCTION_ABORT stop_table_build
!define MUI_ICON   "..\res\cubolt.ico"
!define MUI_UNICON "..\res\cubolt.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Cubolt" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  SetOverwrite on

  File "${BIN}\cubolt.exe"
  File "${BIN}\mktables.exe"
  File "${BIN}\search.exe"
  File "${BIN}\cubolt.ico"

  File "..\LICENSE"
  File "..\README.md"

  /* Tables land beside the exe and are opened by bare name, so the folder has to be writable. */
  nsExec::ExecToLog 'icacls "$INSTDIR" /grant *S-1-5-32-545:(OI)(CI)M /T /C'
  Pop $0
  /* A failed grant looks like a successful install until a first launch cannot write them. */
  StrCmp $0 0 +2 0
  DetailPrint "WARNING: granting write access failed ($0) -- building the tables here may not be possible; run mktables.exe as administrator if the first launch cannot."

  CreateShortcut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\cubolt.ico" 0

  WriteRegStr HKLM "${REGKEY}" "DisplayName"     "${APPNAME}"
  WriteRegStr HKLM "${REGKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKLM "${REGKEY}" "Publisher"       "${PUBLISHER}"
  WriteRegStr HKLM "${REGKEY}" "DisplayIcon"     "$INSTDIR\${EXENAME}"
  WriteRegStr HKLM "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${REGKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'

  WriteRegStr HKLM "${REGKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"

  /* Break inheritance first: an (OI)(CI) entry survives /remove:g until it is made explicit. */
  nsExec::ExecToLog 'icacls "$INSTDIR\*.exe" /inheritance:d /C'
  Pop $0
  nsExec::ExecToLog 'icacls "$INSTDIR\*.exe" /remove:g *S-1-5-32-545 /C'
  Pop $0
  /* Read goes back on, write not: whoever can replace an exe chooses what everyone else runs. */
  nsExec::ExecToLog 'icacls "$INSTDIR\*.exe" /grant *S-1-5-32-545:RX /C'
  Pop $0
  StrCmp $0 0 +2 0
  DetailPrint "WARNING: restoring read access on the executables failed ($0) -- a standard user may be unable to start them."

  CreateDirectory "$SMPROGRAMS"
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXENAME}" "" "$INSTDIR\cubolt.ico" 0
SectionEnd

/* Built here rather than at first launch: --force, or mktables skips a table already on disk. */
Section "Build the solver tables now" SecBuild
  SetOutPath "$INSTDIR"
  SetOverwrite on

  SetDetailsView show
  SetDetailsPrint both
  DetailPrint "Building solver tables..."
  DetailPrint "Working in $INSTDIR"
  DetailPrint "The lines below come straight from the builder and are the progress report:"
  SetDetailsPrint listonly

  /* NSIS grays Cancel and the X throughout; both are turned back on here: the X is SC_CLOSE. */
  GetDlgItem $R2 $HWNDPARENT 2
  EnableWindow $R2 1
  System::Call 'user32::GetSystemMenu(i $HWNDPARENT, i 0) i .r0'
  System::Call 'user32::EnableMenuItem(i $0, i 0xF060, i 0)'

  /* The stock install bar: control 1004 of the instfiles #32770, the only one that pumps messages. */
  System::Call 'user32::FindWindowExW(i $HWNDPARENT, i 0, w "#32770", p 0) i .r0'
  System::Call 'user32::GetDlgItem(i $0, i 1004) i .r1'
  StrCpy $R1 $1
  System::Call 'kernel32::SetEnvironmentVariable(t "CUBOLT_PROGRESS_HWND", t "$R1")'
  System::Call 'kernel32::SetEnvironmentVariable(t "CUBOLT_STOP_FILE", t "$INSTDIR\__stop")'

  nsExec::ExecToLog '"$INSTDIR\mktables.exe" --force'
  Pop $0
  EnableWindow $R2 0
  StrCmp $0 "0" 0 build_failed
  SetDetailsPrint both
  DetailPrint "Solver tables built."
  SetDetailsPrint listonly
  Goto build_done

build_failed:
  SetDetailsPrint both
  DetailPrint "The table build did not finish (exit code $0)."
  DetailPrint "A table the builder stopped half way through is rebuilt, not reused."
  DetailPrint "cubolt.exe builds the ones still missing the first time you start it."
  SetDetailsPrint listonly
build_done:
  Delete "$INSTDIR\__stop"      /* the cancel marker */
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  /* Not RMDir "$SMPROGRAMS": that is Start Menu\Programs itself and would go with the shortcuts. */

  Delete "$INSTDIR\cubolt.exe"
  Delete "$INSTDIR\mktables.exe"
  Delete "$INSTDIR\search.exe"
  Delete "$INSTDIR\cubolt.ico"
  Delete "$INSTDIR\uninstall.exe"

  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\README.md"

  /* None is a file this installer shipped: the ini is under AppData, the rest beside the program. */
  RMDir /r "$APPDATA\${APPNAME}"
  Delete "$INSTDIR\__stop"

  Delete "$INSTDIR\tables.manifest"
  Delete "$INSTDIR\conj_twist"
  Delete "$INSTDIR\ct_clssym"
  Delete "$INSTDIR\ct_prun"
  Delete "$INSTDIR\ct_prun_ftm"
  Delete "$INSTDIR\fs24_clssym"
  Delete "$INSTDIR\fs24_rep"
  Delete "$INSTDIR\move_corners"
  Delete "$INSTDIR\move_flip"
  Delete "$INSTDIR\move_slice_sorted"
  Delete "$INSTDIR\move_twist"
  Delete "$INSTDIR\phase1x24_prun"
  Delete "$INSTDIR\phase1x24_prun_ftm"

  /* Recursive delete of a registry-supplied path, so the test is applied again here. */
  StrCpy $0 $INSTDIR "" 1
  ${If} $0 == ""
  ${OrIf} $0 == ":"
  ${OrIf} $0 == ":\"
  ${OrIf} $0 == ":/"
  ${OrIf} $INSTDIR == "$WINDIR"
  ${OrIf} $INSTDIR == "$SYSDIR"
  ${OrIf} $INSTDIR == "$PROGRAMFILES64"
  ${OrIf} $INSTDIR == "$PROGRAMFILES"
    DetailPrint "Not removing $INSTDIR: it is a drive root or a system folder."
  ${Else}
    RMDir /r "$INSTDIR"
  ${EndIf}

  DeleteRegKey HKLM "${REGKEY}"
SectionEnd

Function .onInit
  /* 64-bit only: $PROGRAMFILES64 has no value on a 32-bit OS, so InstallDir would be wrong. */
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${APPNAME} needs 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  /* Shortcuts are written for every user, so uninstall has to look where install wrote them. */
  SetShellVarContext all

  /* /D= drops the directory page, so .onVerifyInstDir never runs for a silent install. */
  StrCpy $0 $INSTDIR "" 1
  ${If} $0 == ""
  ${OrIf} $0 == ":"
  ${OrIf} $0 == ":\"
  ${OrIf} $0 == ":/"
  ${OrIf} $INSTDIR == "$WINDIR"
  ${OrIf} $INSTDIR == "$SYSDIR"
  ${OrIf} $INSTDIR == "$PROGRAMFILES64"
  ${OrIf} $INSTDIR == "$PROGRAMFILES"
    MessageBox MB_OK|MB_ICONSTOP "${APPNAME} will not install into $INSTDIR."
    Abort
  ${EndIf}

  /* SecBuild has no File, so NSIS would size it 0; this is TABLE_FILES added up, rounded to KB. */
  SectionSetSize ${SecBuild} 1417851
FunctionEnd

/* $INSTDIR decides where the tables are written and what a recursive delete takes with it. */
Function .onVerifyInstDir
  StrCpy $0 $INSTDIR "" 1
  ${If} $0 == ""
  /* A drive root is what follows the drive letter: ":" alone, ":\" or ":/", and nothing else. */
  ${OrIf} $0 == ":"
  ${OrIf} $0 == ":\"
  ${OrIf} $0 == ":/"
  ${OrIf} $INSTDIR == "$WINDIR"
  ${OrIf} $INSTDIR == "$SYSDIR"
  ${OrIf} $INSTDIR == "$PROGRAMFILES64"
  ${OrIf} $INSTDIR == "$PROGRAMFILES"
    Abort
  ${EndIf}
FunctionEnd

/* MUI's abort handler: the builder watches this process; a rollback may not run mid-table. */
/* No plugin call here: the builder's is still on the stack, and nesting faults (0xC0000005). */
Function stop_table_build
  FileOpen $0 "$INSTDIR\__stop" w
  FileClose $0
  Sleep 500
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} \
    "The program itself: the GUI, the command-line solver and the table builder. About 1 MB."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecBuild} \
    "Run the table builder now, so the first start is instant. Building them takes a while, and \
     most of the time goes into the two big ones. If you clear this, the program builds them on \
     its first run instead."
!insertmacro MUI_FUNCTION_DESCRIPTION_END
