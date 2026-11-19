#!/usr/bin/env python3
"""Real IPP/URF pipeline, simulated printer only; A5/A6 must still be fed on hardware."""
import http.client,json,os,socket,subprocess,time
from pathlib import Path
from test_capt_ipp import ipp,white_urf,ROOT
for driver in ['hp-p1006','canon-lbp2900']:
 with socket.socket() as sock:
  sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 with (ROOT/'host/out'/('small-'+driver+'.log')).open('w') as log:
  process=subprocess.Popen([str(ROOT/'host/airprint_host'),str(port)],cwd=ROOT/'host',env=dict(os.environ,PRINTER_DRIVER=driver,ASAN_OPTIONS='detect_leaks=0'),stdout=log,stderr=log)
  try:
   for _ in range(100):
    if process.poll() is not None:raise RuntimeError('host exited')
    try:
     with socket.create_connection(('127.0.0.1',port),.2):break
    except OSError:time.sleep(.1)
   else:raise RuntimeError('host timeout')
   status,caps=ipp(port,0x0b);assert status==0
   for paper,w,h in [('iso_a5_148x210mm',3496,4961),('iso_a6_105x148mm',2480,3496)]:
    assert paper.encode() in caps
    status,_=ipp(port,2,white_urf(2,width=w,height=h));assert status==0,hex(status)
    conn=http.client.HTTPConnection('127.0.0.1',port);conn.request('GET','/api/state');st=json.loads(conn.getresponse().read());conn.close()
    assert paper in st['printer']['papers']
    assert st['printer']['jobs'][0]['state']=='completed' and st['printer']['jobs'][0]['pages']==2,st
   print(driver+': A5/A6 advertised via IPP and JSON, two-page URF jobs complete')
  finally:
   process.terminate()
   try:process.wait(5)
   except subprocess.TimeoutExpired:process.kill();process.wait()
