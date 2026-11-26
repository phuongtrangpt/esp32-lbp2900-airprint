"""Prove that only the dedicated hp_pc profile receives the longer USB wait."""
import re,subprocess
from pathlib import Path
base='c0d059e29deb78801001798e34f9d0151a77ccdc'
source=Path('src/usb_printer.c').read_text()
section=re.search(r'\n// HP_PC_ONLY_BEGIN\n(.*?)// HP_PC_ONLY_END\n',source,re.S)
assert section and section.group(1).startswith('#ifdef PRINTSERVER_HP_PC_OFFLOAD\n')
assert section.group(1).count('pdMS_TO_TICKS(120000)')==2
original=subprocess.check_output(['git','show',base+':src/usb_printer.c'],text=True)
assert source[:section.start()]+source[section.end():]==original,'Shared USB implementation changed outside hp_pc-only addition'
assert 'usb_printer_raw_write(p,n)' not in Path('src/hp_pc_bridge.c').read_text()
assert 'usb_printer_pc_flush()' in Path('src/hp_pc_bridge.c').read_text()
assert source.count('pdMS_TO_TICKS(12000)')==original.count('pdMS_TO_TICKS(12000)')
print('PASS: stable USB code unchanged; only hp_pc gets 120 s backpressure budget; legacy RAW retains 12 s')
