// SPDX-License-Identifier: GPL-3.0-or-later
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::{fs, net::Ipv4Addr, path::{Path,PathBuf}, time::{Duration,Instant}};
use windows_service::{service::*, service_manager::{ServiceManager,ServiceManagerAccess}};
pub const SERVICE: &str="VietPhuongHPP1006PC";
#[derive(Default,Clone,Serialize,Deserialize)]
pub struct Settings { pub esp_ip: Option<String> }
pub fn data_dir()->PathBuf { PathBuf::from(std::env::var_os("PROGRAMDATA").unwrap_or_else(||r"C:\ProgramData".into())).join("VietPhuong").join("HP-P1006-PC") }
pub fn atomic_json(path:&Path,value:&Value)->Result<(),String>{
    fs::create_dir_all(path.parent().ok_or("Missing parent")?).map_err(|e|e.to_string())?;
    let tmp=path.with_extension("tmp");
    fs::write(&tmp,serde_json::to_vec(value).map_err(|e|e.to_string())?).map_err(|e|e.to_string())?;
    fs::rename(tmp,path).map_err(|e|e.to_string())
}
pub fn settings()->Settings { fs::read(data_dir().join("settings.json")).ok().and_then(|b|serde_json::from_slice(&b).ok()).unwrap_or_default() }
pub fn status_file()->Value { fs::read(data_dir().join("status.json")).ok().and_then(|b|serde_json::from_slice(&b).ok()).unwrap_or_else(||json!({"ready":false,"events":[],"busy":false})) }
pub fn service(access:ServiceAccess)->Result<Service,String>{
    ServiceManager::local_computer(None::<&str>,ServiceManagerAccess::CONNECT).map_err(|e|e.to_string())?.open_service(SERVICE,access).map_err(|e|e.to_string())
}
pub fn stop_service()->Result<(),String>{
    let s=service(ServiceAccess::STOP|ServiceAccess::QUERY_STATUS)?;
    if s.query_status().map_err(|e|e.to_string())?.current_state==ServiceState::Stopped{return Ok(());}
    s.stop().map_err(|e|e.to_string())?;
    let deadline=Instant::now()+Duration::from_secs(35);
    while Instant::now()<deadline {
        if s.query_status().map_err(|e|e.to_string())?.current_state==ServiceState::Stopped{return Ok(());}
        std::thread::sleep(Duration::from_millis(100));
    }
    Err("Service stop timed out".into())
}
pub fn start_service()->Result<(),String>{
    let s=service(ServiceAccess::START|ServiceAccess::QUERY_STATUS)?;
    if s.query_status().map_err(|e|e.to_string())?.current_state==ServiceState::Running{return Ok(());}
    s.start(&[] as &[&str]).map_err(|e|e.to_string())
}
pub fn install()->Result<(),String>{
    let mgr=ServiceManager::local_computer(None::<&str>,ServiceManagerAccess::CONNECT|ServiceManagerAccess::CREATE_SERVICE).map_err(|e|e.to_string())?;
    let info=ServiceInfo{name:SERVICE.into(),display_name:"Viet Phuong HP P1006 PC Print Service".into(),service_type:ServiceType::OWN_PROCESS,start_type:ServiceStartType::AutoStart,error_control:ServiceErrorControl::Normal,executable_path:std::env::current_exe().map_err(|e|e.to_string())?,launch_arguments:vec![],dependencies:vec![],account_name:Some(r"NT AUTHORITY\LocalService".into()),account_password:None};
    let access=ServiceAccess::CHANGE_CONFIG|ServiceAccess::START|ServiceAccess::QUERY_STATUS;
    let s=match mgr.create_service(&info,access){Ok(s)=>s,Err(_)=>{let s=mgr.open_service(SERVICE,access).map_err(|e|e.to_string())?;s.change_config(&info).map_err(|e|e.to_string())?;s}};
    s.set_description("HP P1006 PC raster/JBIG processing and ESP USB transport. Starts automatically without opening the manager.").map_err(|e|e.to_string())?;
    s.update_failure_actions(ServiceFailureActions{reset_period:ServiceFailureResetPeriod::After(Duration::from_secs(86400)),reboot_msg:None,command:None,actions:Some(vec![ServiceAction{action_type:ServiceActionType::Restart,delay:Duration::from_secs(5)};3])}).map_err(|e|e.to_string())?;
    s.set_failure_actions_on_non_crash_failures(true).map_err(|e|e.to_string())?;
    start_service()
}
pub fn configure(ip:&str)->Result<(),u8>{
    ip.parse::<Ipv4Addr>().map_err(|_|11)?;
    if status_file()["busy"].as_bool()==Some(true) && settings().esp_ip.as_deref()!=Some(ip){return Err(10);}
    atomic_json(&data_dir().join("settings.json"),&json!({"esp_ip":ip})).map_err(|_|12)?;
    if service(ServiceAccess::QUERY_STATUS).and_then(|s|s.query_status().map_err(|e|e.to_string())).map(|s|s.current_state==ServiceState::Running).unwrap_or(false){
        // The service notices changes and switches worker only while no print is active.
        Ok(())
    }else{start_service().map_err(|_|13)}
}
pub fn snapshot()->Value{
    let mut status=status_file();
    let state=service(ServiceAccess::QUERY_STATUS).and_then(|s|s.query_status().map_err(|e|e.to_string())).map(|s|format!("{:?}",s.current_state)).unwrap_or_else(|_|"NotInstalled".into());
    status["service_state"]=json!(state);
    status["settings"]=serde_json::to_value(settings()).unwrap_or_else(|_|json!({}));
    status
}
#[cfg(test)]mod tests {
    use super::*;
    #[test]fn configuration_round_trip(){let s:Settings=serde_json::from_str(r#"{"esp_ip":"192.168.10.21"}"#).unwrap();assert_eq!(s.esp_ip.as_deref(),Some("192.168.10.21"));assert!(serde_json::from_str::<Settings>("{}").unwrap().esp_ip.is_none());}
}
