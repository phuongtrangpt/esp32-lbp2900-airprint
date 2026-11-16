from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'profile.c';p.write_text('''#include "printer_driver.h"
#include <assert.h>
#include <string.h>
int main(void){assert(printer_drivers[0] && !printer_drivers[1]);
#ifdef PRINTSERVER_PROFILE_HP
assert(!strcmp(printer_drivers[0]->id,"hp-p1006"));assert(printer_driver_match(0x03f0,0x3e17,"")!=0);assert(!printer_driver_match(0x04a9,0x2676,""));
#else
assert(!strcmp(printer_drivers[0]->id,"canon-lbp2900"));assert(printer_driver_match(0x04a9,0x2676,"")!=0);assert(!printer_driver_match(0x03f0,0x3e17,""));
#endif
assert(!printer_driver_match(1,2,"CMD:PCL,POSTSCRIPT;"));return 0;}''')
 includes=['components/airprint/include','components/xqx/vendor','components/capt/include','components/capt/vendor']
 for kind in ['HP','CANON']:
  sources=['components/printers/printers.c']
  if kind=='HP':sources+=['components/printers/hp_p1006.c','components/xqx/vendor/jbig.c','components/xqx/vendor/jbig_ar.c']
  else:sources+=['components/printers/canon_lbp2900.c','components/capt/capt_transport.c']+[str(x.relative_to(r)) for x in (r/'components/capt/vendor').glob('*.c')]
  exe=Path(td)/kind
  subprocess.run(['cc','-O2','-DPRINTSERVER_PROFILE_'+kind,*['-I'+str(r/x) for x in includes],str(p),*[str(r/x) for x in sources],'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True);print(kind+': one driver, correct identity, other models refused')
