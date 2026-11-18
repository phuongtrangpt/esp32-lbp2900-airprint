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
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'group.c';p.write_text('''#include "printer_driver.h"
#include <assert.h>
#include <string.h>
int main(void){int count=0;while(printer_drivers[count])count++;
#ifdef PRINTSERVER_PROFILE_PIXMA
assert(count==23);assert(printer_driver_match(0x04a9,0x176d,"")!=0);assert(printer_driver_match(0,0,"MFG:Canon;MDL:MP280 series;")!=0);assert(!printer_driver_match(1,2,"CMD:PCL5,POSTSCRIPT;"));
#else
assert(count==2);assert(!strcmp(printer_driver_match(1,2,"CMD:PCL5E;")->id,"generic-pcl5"));assert(!strcmp(printer_driver_match(1,2,"CMD:POSTSCRIPT;")->id,"generic-postscript"));assert(!printer_driver_match(0x04a9,0x176d,"MFG:Canon;MDL:MG2500 series;"));
#endif
assert(!printer_driver_match(0x04a9,0x2676,"MFG:Canon;MDL:LBP2900;CMD:CAPT;"));assert(!printer_driver_match(0x03f0,0x3e17,"CMD:ACL;"));
assert(!printer_driver_match(0x04a9,0,"MFG:Canon;MDL:LBP6030;CMD:UFRII;"));assert(!printer_driver_match(1,2,"CMD:PCLXL,PCL6;"));assert(!printer_driver_match(1,2,"CMD:NOTPCL5;"));return 0;}''')
 for kind in ['PIXMA','STANDARD']:
  sources=['components/printers/printers.c','components/airprint/printer_driver.c']
  if kind=='PIXMA':sources+=['components/printers/canon_pixma.c','components/canon_bjl/canon_bjl.c','components/canon_bjl/testpage.c','components/canon_bjl/bjl_render.c']
  else:sources+=['components/printers/standard_mono.c']
  exe=Path(td)/kind
  subprocess.run(['cc','-O2','-DPRINTSERVER_PROFILE_'+kind,'-I'+str(r/'components/airprint/include'),'-I'+str(r/'components/canon_bjl/include'),str(p),*[str(r/x) for x in sources],'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True);print(kind+': driver group isolated; unsupported CAPT/XQX/UFR/PCL6 rejected')
