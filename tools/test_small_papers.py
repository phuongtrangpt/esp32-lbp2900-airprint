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
    cases = [(600, '/ipp/print', 600)]
    if driver == 'canon-lbp2900':
     cases = [(300, '/ipp/print', 300), (600, '/ipp/print', 300), (600, '/ipp/web-print', 600)]
    for dpi,path,output_dpi in cases:
     raster_w, raster_h = (w // 2, h // 2) if dpi == 300 else (w, h)
     log_path = ROOT/'host/out'/('small-'+driver+'.log')
     log_offset = log_path.stat().st_size
     status,_=ipp(port,2,white_urf(2,dpi=dpi,width=raster_w,height=raster_h),dpi=dpi,path=path)
     assert status==0,hex(status)
     conn=http.client.HTTPConnection('127.0.0.1',port);conn.request('GET','/api/state');st=json.loads(conn.getresponse().read());conn.close()
     assert paper in st['printer']['papers']
     assert st['printer']['jobs'][0]['state']=='completed' and st['printer']['jobs'][0]['pages']==2,st
     if driver == 'canon-lbp2900':
      with log_path.open() as capture:
       capture.seek(log_offset)
       job_log = capture.read()
      size = 'A5' if 'a5' in paper else 'A6'
      assert f': {size} {dpi} dpi, gray' in job_log, job_log
      assert f'CAPT input={output_dpi} dpi' in job_log, job_log
   print(driver+': A5/A6 IPP capabilities and two-page jobs passed')
  finally:
   process.terminate()
   try:process.wait(5)
   except subprocess.TimeoutExpired:process.kill();process.wait()
