// SPDX-License-Identifier: GPL-3.0-or-later
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
#[path="../../src-tauri/src/service_common.rs"]
mod service_common;
use service_common::*;
use serde_json::{json,Value};
use std::{collections::{HashSet,VecDeque}, ffi::OsString, io::{BufRead,BufReader,Write}, os::windows::{io::AsRawHandle,process::CommandExt},process::{Child,Command,Stdio}, sync::{Arc,atomic::{AtomicBool,Ordering},mpsc},time::{Duration,Instant,SystemTime,UNIX_EPOCH}};
use windows_service::{define_windows_service,service::*,service_control_handler::{self,ServiceControlHandlerResult},service_dispatcher};
use windows_sys::Win32::{Foundation::{CloseHandle,HANDLE},System::JobObjects::*};
define_windows_service!(ffi_main,service_main);
struct Job(HANDLE);
impl Job{fn attach(child:&Child)->Result<Self,String>{unsafe{
    let h=CreateJobObjectW(std::ptr::null(),std::ptr::null());if h.is_null(){return Err(std::io::Error::last_os_error().to_string());}
    let mut info:JOBOBJECT_EXTENDED_LIMIT_INFORMATION=std::mem::zeroed();info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if SetInformationJobObject(h,JobObjectExtendedLimitInformation,&info as *const _ as *const _,std::mem::size_of_val(&info) as u32)==0 || AssignProcessToJobObject(h,child.as_raw_handle())==0{CloseHandle(h);return Err(std::io::Error::last_os_error().to_string());}
    Ok(Self(h))
}}}
impl Drop for Job{fn drop(&mut self){unsafe{CloseHandle(self.0);}}}
struct Report{events:VecDeque<Value>,seq:u64,boot:String,busy:HashSet<u64>,ready:bool,ip:Option<String>,pid:Option<u32>,device:Value}
impl Report{
 fn new()->Self{Self{events:VecDeque::new(),seq:0,boot:format!("{}-{}",SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_millis(),std::process::id()),busy:HashSet::new(),ready:false,ip:None,pid:None,device:json!({"online":false,"state":"checking","message":"Waiting for ESP"})}}
 fn event(&mut self,mut e:Value){
  if e["phase"]=="device-status"{self.device=e["device"].clone();}
  if let Some(id)=e["job"].as_u64(){match e["phase"].as_str().unwrap_or(""){"spooled"=>{self.busy.insert(id);},"usb-delivered"|"failed"|"canceled"=>{self.busy.remove(&id);},_=>{}}}
  if e["phase"]=="ready"{self.ready=true;}if e["phase"]=="stopped"{self.ready=false;self.busy.clear();self.device=json!({"online":false,"state":"offline","message":"Print worker is stopped"});}
  self.seq+=1;e["time_ms"]=json!(SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_millis() as u64);e["sequence"]=json!(self.seq);self.events.push_back(e);while self.events.len()>80{self.events.pop_front();}self.save();
 }
 fn save(&self){let _=atomic_json(&data_dir().join("status.json"),&json!({"boot_id":self.boot,"ready":self.ready,"device":self.device,"esp":self.ip,"worker_pid":self.pid,"busy":!self.busy.is_empty(),"events":self.events,"active_job":self.busy.iter().min().copied()}));}
}
fn stop_child(c:&mut Child){if let Some(input)=c.stdin.as_mut(){let _=writeln!(input,"stop");}let until=Instant::now()+Duration::from_secs(8);while Instant::now()<until{if c.try_wait().ok().flatten().is_some(){return;}std::thread::sleep(Duration::from_millis(100));}let _=c.kill();let _=c.wait();}
fn supervise(stop:Arc<AtomicBool>){
 let mut report=Report::new();report.event(json!({"phase":"service-started","message":"Windows print service running independently of the manager"}));
 while !stop.load(Ordering::Relaxed){
  let config=settings();
  let ip=config.esp_ip.filter(|ip|ip.parse::<std::net::Ipv4Addr>().is_ok()).unwrap_or_default();
  let parent=match std::env::current_exe().ok().and_then(|p|p.parent().map(|p|p.to_path_buf())){Some(p)=>p,None=>return};
  let helper=parent.join("helper/hp-pc-worker.exe");let spool=data_dir().join("spool");let _=std::fs::create_dir_all(&spool);
  // A restarted worker never restores/replays an interrupted document.
  if let Ok(entries)=std::fs::read_dir(&spool){for entry in entries.flatten(){if (entry.file_name().to_string_lossy().starts_with("hp-pc-spool-") || entry.file_name().to_string_lossy().starts_with("hp-pc-test-")) && entry.file_type().map(|t|t.is_dir()).unwrap_or(false){let _=std::fs::remove_dir_all(entry.path());}}}
  let status_port=config.esp_status_port.to_string();
  let mut c=match Command::new(helper).args(["--esp",&ip,"--esp-status-port",&status_port]).env("TEMP",&spool).env("TMP",&spool).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped()).creation_flags(0x08000000).spawn(){Ok(c)=>c,Err(e)=>{report.event(json!({"phase":"failed","message":format!("Worker launch failed: {e}")}));std::thread::sleep(Duration::from_secs(3));continue;}};
  let job=match Job::attach(&c){Ok(j)=>j,Err(e)=>{let _=c.kill();let _=c.wait();report.event(json!({"phase":"failed","message":format!("Worker isolation failed: {e}")}));std::thread::sleep(Duration::from_secs(3));continue;}};
  report.ip=if ip.is_empty(){None}else{Some(ip.clone())};report.pid=Some(c.id());report.ready=false;report.device=json!({"online":false,"state":"checking","message":"Checking ESP and USB printer"});report.busy.clear();report.save();
  let (tx,rx)=mpsc::sync_channel(128);
  if let Some(out)=c.stdout.take(){let t=tx.clone();std::thread::spawn(move||{for l in BufReader::new(out).lines().map_while(Result::ok){if l.len()<=16384{if let Ok(e)=serde_json::from_str::<Value>(&l){if t.send(e).is_err(){break;}}}}});}
  if let Some(err)=c.stderr.take(){std::thread::spawn(move||{for l in BufReader::new(err).lines().map_while(Result::ok){if tx.send(json!({"phase":"failed","message":l.chars().take(1000).collect::<String>()})).is_err(){break;}}});}
  loop{
   while let Ok(e)=rx.try_recv(){report.event(e);}
   if stop.load(Ordering::Relaxed){break;}
   let latest=settings();
   if (latest.esp_ip.as_deref().unwrap_or("")!=ip || latest.esp_status_port!=config.esp_status_port) && report.busy.is_empty(){break;}
   match c.try_wait(){Ok(Some(code))=>{report.event(json!({"phase":"failed","message":format!("Worker stopped ({code}); service will restart it. Jobs are not automatically replayed.")}));break;},Err(_)=>break,_=>{}}
   std::thread::sleep(Duration::from_millis(100));
  }
  stop_child(&mut c);drop(job);report.pid=None;report.event(json!({"phase":"stopped","message":"Print worker stopped"}));
  if !stop.load(Ordering::Relaxed){std::thread::sleep(Duration::from_secs(2));}
 }
 report.event(json!({"phase":"service-stopped","message":"Windows print service stopped"}));
}
fn service_main(_args:Vec<OsString>){
 let stop=Arc::new(AtomicBool::new(false));let flag=stop.clone();
 let status=match service_control_handler::register(SERVICE,move|control|match control{ServiceControl::Stop|ServiceControl::Shutdown=>{flag.store(true,Ordering::Relaxed);ServiceControlHandlerResult::NoError},ServiceControl::Interrogate=>ServiceControlHandlerResult::NoError,_=>ServiceControlHandlerResult::NotImplemented}){Ok(s)=>s,Err(_)=>return};
 let make=|state|ServiceStatus{service_type:ServiceType::OWN_PROCESS,current_state:state,controls_accepted:ServiceControlAccept::STOP|ServiceControlAccept::SHUTDOWN,exit_code:ServiceExitCode::Win32(0),checkpoint:0,wait_hint:Duration::from_secs(15),process_id:None};
 let _=status.set_service_status(make(ServiceState::Running));supervise(stop);let _=status.set_service_status(make(ServiceState::Stopped));
}
fn diagnostic(e:String)->u8{let _=std::fs::create_dir_all(data_dir());let _=std::fs::write(data_dir().join("control-error.txt"),e);13}
fn main(){
 let args:Vec<String>=std::env::args().collect();
 let result=match args.get(1).map(String::as_str){Some("--install")=>install().map_err(diagnostic),Some("--install-queue")=>install_queue(false).map_err(diagnostic),Some("--remove-queue")=>install_queue(true).map_err(diagnostic),Some("--remove")=>{let _=stop_service();service(ServiceAccess::DELETE).and_then(|s|s.delete().map_err(|e|e.to_string())).map_err(|_|13)},Some("--stop")=>stop_service().map_err(diagnostic),Some("--start")=>start_service().map_err(diagnostic),Some("--set-ip")=>args.get(2).ok_or(11).and_then(|ip|configure(ip)),_=>service_dispatcher::start(SERVICE,ffi_main).map_err(|_|13)};
 if let Err(code)=result{std::process::exit(code as i32);}
}
