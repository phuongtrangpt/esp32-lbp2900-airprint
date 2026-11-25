; GPL-3.0-or-later. Only this product's service and ProgramData directory.
!macro NSIS_HOOK_PREINSTALL
  IfFileExists "$INSTDIR\hp-pc-service.exe" 0 +4
    nsExec::ExecToStack '"$INSTDIR\hp-pc-service.exe" --stop'
    Pop $0
    Pop $1
!macroend
!macro NSIS_HOOK_POSTINSTALL
  SetShellVarContext all
  CreateDirectory "$APPDATA\VietPhuong\HP-P1006-PC"
  nsExec::ExecToStack '"$SYSDIR\icacls.exe" "$APPDATA\VietPhuong\HP-P1006-PC" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-19:(OI)(CI)M" "*S-1-5-32-545:(OI)(CI)RX"'
  Pop $0
  Pop $1
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Cannot configure HP P1006 service data permissions. $1" /SD IDOK
    Abort
  ${EndIf}
  CreateDirectory "$APPDATA\VietPhuong\HP-P1006-PC\spool"
  nsExec::ExecToStack '"$SYSDIR\icacls.exe" "$APPDATA\VietPhuong\HP-P1006-PC\spool" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-19:(OI)(CI)M"'
  Pop $0
  Pop $1
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Cannot protect the print spool folder." /SD IDOK
    Abort
  ${EndIf}
  nsExec::ExecToStack '"$INSTDIR\hp-pc-service.exe" --install'
  Pop $0
  Pop $1
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Cannot install HP P1006 Windows Service. Administrator permission is required." /SD IDOK
    Abort
  ${EndIf}
!macroend
!macro NSIS_HOOK_PREUNINSTALL
  nsExec::ExecToStack '"$INSTDIR\hp-pc-service.exe" --remove'
  Pop $0
  Pop $1
!macroend
