# SPDX-License-Identifier: GPL-3.0-or-later
import io
from pathlib import Path
import sys
import time
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_offload import ipp,raster,RENDER
from fake_esp import FakeESP
from device_status import probe_device,offline
from worker import Service
from ipp_codec import decode,integer,text

class DeviceStatus(unittest.TestCase):
 def test_physical_usb_network_and_busy(self):
  with FakeESP(http_port=0,pc_port=0) as peer:
   read=lambda:probe_device('127.0.0.1',peer.http_port,peer.pc_port,timeout=.2)
   self.assertTrue(read()['can_print'])
   peer.connected=False;self.assertFalse(read()['online'])
   peer.connected=True;peer.busy=True;self.assertEqual(read()['state'],'busy')
   peer.busy=False;peer.reachable=False
   with self.assertRaises(Exception):read()
   peer.reachable=True;self.assertTrue(read()['can_print'])
 def test_http_refusal_during_other_print_is_not_false_offline(self):
  with FakeESP(http_port=0,pc_port=0) as peer:
   peer.reject_http=True;peer.busy=True
   device=probe_device('127.0.0.1',peer.http_port,peer.pc_port,timeout=.2)
   self.assertTrue(device['online']);self.assertEqual(device['state'],'busy')
 def test_ipp_state_and_job_wait_for_reconnect(self):
  service=Service('127.0.0.1',RENDER)
  def attrs():
   result=service.handle(io.BytesIO(ipp(11)))
   return decode(io.BytesIO(result))[2]
  try:
   self.assertEqual(integer(attrs(),'printer-state'),3)
   service.update_device(offline('Offline: USB printer not connected'))
   self.assertEqual(integer(attrs(),'printer-state'),5)
   self.assertIn('Offline',text(attrs(),'printer-state-message'))
   with patch('worker.send_job') as send:
    service.handle(io.BytesIO(ipp(doc=raster())))
    until=time.monotonic()+10
    job=next(iter(service.jobs.values()))
    while job['reason']!='printer-stopped' and time.monotonic()<until:time.sleep(.02)
    self.assertEqual(job['state'],4);send.assert_not_called()
    service.update_device(dict(online=True,can_print=True,state='ready',message='Ready',reasons=['none']))
    until=time.monotonic()+10
    while job['state']<7 and time.monotonic()<until:time.sleep(.02)
    self.assertEqual(job['state'],9);self.assertEqual(send.call_count,1)
  finally:service.stop()
 def test_canceled_job_stays_canceled_even_if_device_is_ready(self):
  service=Service('127.0.0.1',RENDER)
  try:
   job=service.new_job({});service.cancel(job)
   with self.assertRaises(InterruptedError):service.wait_for_device(job)
   self.assertEqual(job['state'],7)
  finally:service.stop()
 def test_monitor_recovers_without_manager(self):
  with FakeESP(http_port=0,pc_port=0) as peer:
   with patch('worker.probe_device',lambda h,**kw:probe_device(h,peer.http_port,peer.pc_port,timeout=.2)):
    service=Service('127.0.0.1',RENDER,monitor=True,poll_interval=.05)
    def wait_online(expected):
     until=time.monotonic()+5
     while service.device['online']!=expected and time.monotonic()<until:time.sleep(.02)
     self.assertEqual(service.device['online'],expected)
    try:
     wait_online(True)
     peer.connected=False;wait_online(False)
     peer.connected=True;wait_online(True)
     peer.reachable=False;wait_online(False)
     peer.reachable=True;wait_online(True)
    finally:service.stop()

if __name__=='__main__':unittest.main()
