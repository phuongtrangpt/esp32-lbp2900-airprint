# SPDX-License-Identifier: GPL-3.0-or-later
import socket,threading,time,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'worker'))
from transport import frame_response,REPLY
class UsbWait(unittest.TestCase):
 def test_partial_delayed_ack_is_read_once_despite_short_socket_timeout(self):
  a,b=socket.socketpair();a.settimeout(.02)
  data=REPLY.pack(b'VPHPA01\n',1,645,2637824)
  def send():
   time.sleep(.1);b.sendall(data[:9]);time.sleep(.1);b.sendall(data[9:])
  t=threading.Thread(target=send);t.start()
  try:self.assertEqual(frame_response(a,threading.Event(),timeout=2),(1,645,2637824))
  finally:t.join();a.close();b.close()
 def test_wait_is_bounded(self):
  a,b=socket.socketpair()
  try:
   with self.assertRaises(TimeoutError):frame_response(a,threading.Event(),timeout=.05)
  finally:a.close();b.close()
 def test_cancel_does_not_wait_for_ack_or_replay(self):
  a,b=socket.socketpair();cancel=threading.Event();cancel.set()
  try:
   with self.assertRaises(InterruptedError):frame_response(a,cancel,timeout=2)
   b.settimeout(.02)
   with self.assertRaises(socket.timeout):b.recv(1)
  finally:a.close();b.close()
if __name__=='__main__':unittest.main()
