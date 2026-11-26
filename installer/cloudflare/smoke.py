"""Exercise the live API without logging codes/tokens or touching any ESP."""
import concurrent.futures,json,sys,time,urllib.request,urllib.error
base=sys.argv[1].rstrip('/')
def post(action,data=None):
 req=urllib.request.Request(base+'/api/access/'+action,method='POST',data=json.dumps(data or {}).encode(),headers={'Origin':base,'Content-Type':'application/json'})
 try:
  with urllib.request.urlopen(req,timeout=30) as r:return r.status,json.load(r)
 except urllib.error.HTTPError as e:return e.code,json.load(e)
for attempt in range(8):
 try:
  with urllib.request.urlopen(base+'/lay-ma/',timeout=30) as r:assert r.status==200 and 'Tạo mã'.encode() in r.read()
  status,issued=post('new');assert status==200,(status,issued)
  break
 except Exception:
  if attempt==7:raise
  time.sleep(5)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 answers=list(pool.map(lambda _:post('redeem',{'code':issued['code']}),range(2)))
assert sorted(x[0] for x in answers)==[200,409],[x[0] for x in answers]
token=next(x[1]['token'] for x in answers if x[0]==200)
assert post('check',{'token':token})[0]==200
assert post('check',{'token':'0'*32})[0]==401
with urllib.request.urlopen(base+'/firmware/hp/web-manifest.json',timeout=30) as r:assert r.status==200 and json.load(r)['builds']
print('LIVE PASS: code page, atomic single-use redemption under concurrency, session validation, invalid-session rejection, original firmware manifest.')
