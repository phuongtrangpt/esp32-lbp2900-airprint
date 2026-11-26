"""Run only in GitHub Actions; credentials stay in environment and are never logged."""
import json,os,sys,urllib.request,urllib.error
from pathlib import Path
root=Path(__file__).resolve().parent
account=os.environ.get('CLOUDFLARE_ACCOUNT_ID');token=os.environ.get('CLOUDFLARE_API_TOKEN')
if not account or not token:raise SystemExit('Cloudflare credentials are not configured in GitHub Actions.')
base=f'https://api.cloudflare.com/client/v4/accounts/{account}'
def call(path,method='GET',data=None):
 req=urllib.request.Request(base+path,data=None if data is None else json.dumps(data).encode(),method=method,headers={'Authorization':'Bearer '+token,'Content-Type':'application/json'})
 try:
  with urllib.request.urlopen(req,timeout=45) as r:result=json.load(r)
 except urllib.error.HTTPError as e:
  try:errors=json.load(e).get('errors',[])
  except Exception:errors=[]
  codes=','.join(str(x.get('code')) for x in errors)
  raise SystemExit(f'Cloudflare request failed: HTTP {e.code}, error codes {codes}. D1 Edit permission is required; existing website was not replaced.') from None
 if not result.get('success'):raise SystemExit('Cloudflare operation failed; website was not replaced.')
 return result['result']
name='vietphuong-esp-access-codes'
items=call('/d1/database?name='+name)
db=next((x for x in items if x['name']==name),None)
if not db:db=call('/d1/database','POST',{'name':name})
dbid=db['uuid']
call('/d1/database/'+dbid+'/query','POST',{'sql':(root/'schema.sql').read_text()})
config={'name':'vietphuong-esp','pages_build_output_dir':'./dist','compatibility_date':'2026-11-01','d1_databases':[{'binding':'KEY_DB','database_name':name,'database_id':dbid}]}
(root.parent/'wrangler.json').write_text(json.dumps(config))
print('Cloudflare D1 ready; one-time code binding configured for deployment.')
