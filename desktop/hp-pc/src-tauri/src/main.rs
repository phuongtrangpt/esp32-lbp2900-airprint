#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
use std::{io::{BufRead, BufReader, Write}, net::{Ipv4Addr, SocketAddr, TcpStream}, process::{Child, Command, Stdio}, sync::Mutex, time::Duration};
use tauri::{Emitter, Manager};
#[cfg(windows)]
use std::os::windows::process::CommandExt;
#[derive(Default)]
struct Worker(Mutex<Option<Child>>);
fn hidden(c: &mut Command) -> &mut Command {
    #[cfg(windows)] c.creation_flags(0x08000000);
    c
}
fn stop(state: &Worker) -> Result<(), String> {
    let mut lock=state.0.lock().map_err(|e|e.to_string())?;
    if let Some(child)=lock.as_mut() {
        if let Some(input)=child.stdin.as_mut() { let _=writeln!(input,"stop"); }
        // Keep the child until it actually exits: a new worker must not race
        // the old listener while shutting down.
    }
    Ok(())
}
#[tauri::command]
fn start_worker(app: tauri::AppHandle, state: tauri::State<Worker>, ip: String) -> Result<(), String> {
    ip.parse::<Ipv4Addr>().map_err(|_|"Địa chỉ IPv4 không hợp lệ")?;
    let mut lock=state.0.lock().map_err(|e|e.to_string())?;
    if let Some(child)=lock.as_mut() {
        if child.try_wait().map_err(|e|e.to_string())?.is_none() { return Err("Dịch vụ đang chạy. Hãy dừng trước khi đổi IP.".into()); }
    }
    let exe=app.path().resource_dir().map_err(|e|e.to_string())?.join("helper").join("hp-pc-worker.exe");
    let mut child=hidden(Command::new(exe).args(["--esp", &ip]).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped())).spawn().map_err(|e|e.to_string())?;
    let output=child.stdout.take().ok_or("Missing worker stdout")?;
    let errors=child.stderr.take().ok_or("Missing worker stderr")?;
    let copy=app.clone();
    std::thread::spawn(move || {
        for line in BufReader::new(output).lines().map_while(Result::ok) {
            if let Ok(event)=serde_json::from_str::<serde_json::Value>(&line) { let _=copy.emit("worker-event",event); }
        }
        let _=copy.emit("worker-event",serde_json::json!({"phase":"stopped","message":"Dịch vụ đã dừng"}));
    });
    std::thread::spawn(move || {
        for line in BufReader::new(errors).lines().map_while(Result::ok) { let _=app.emit("worker-event",serde_json::json!({"phase":"failed","message":line})); }
    });
    *lock=Some(child);Ok(())
}
#[tauri::command]
fn stop_worker(state: tauri::State<Worker>) -> Result<(), String> { stop(&state) }
#[tauri::command]
fn cancel_job(state: tauri::State<Worker>) -> Result<(), String> {
    let mut lock=state.0.lock().map_err(|e|e.to_string())?;
    if let Some(child)=lock.as_mut() { if let Some(input)=child.stdin.as_mut() { return writeln!(input,"cancel").map_err(|e|e.to_string()); } }
    Err("Dịch vụ chưa chạy".into())
}
#[tauri::command]
async fn check_esp(ip: String) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || {
        use std::io::Read;
        let addr=ip.parse::<Ipv4Addr>().map_err(|_|"IPv4 không hợp lệ".to_string())?;
        let mut s=TcpStream::connect_timeout(&SocketAddr::from((addr,9101)),Duration::from_secs(5)).map_err(|e|e.to_string())?;
        s.set_read_timeout(Some(Duration::from_secs(5))).map_err(|e|e.to_string())?;
        s.set_write_timeout(Some(Duration::from_secs(5))).map_err(|e|e.to_string())?;
        let mut request=[0u8;32];request[..8].copy_from_slice(b"VPHPO01\n");request[11]=1;
        s.write_all(&request).map_err(|e|e.to_string())?;
        let mut reply=[0u8;24];s.read_exact(&mut reply).map_err(|e|e.to_string())?;
        if &reply[..8]!=b"VPHPA01\n" { return Err("Sai firmware/giao thức ESP".into()); }
        let status=u32::from_be_bytes(reply[8..12].try_into().unwrap());
        match status {0=>Ok("ESP và HP P1006 sẵn sàng".into()),101=>Err("Máy in đang bận".into()),102=>Err("Chưa nhận đúng máy HP P1006 qua USB".into()),_=>Err(format!("ESP báo lỗi {status}"))}
    }).await.map_err(|e|e.to_string())?
}
#[tauri::command]
async fn install_queue() -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(|| {
        // Fixed command: no user input/shell interpolation. Separate queue only.
        let script="$ErrorActionPreference='Stop'; if (-not (Get-Printer -Name 'HP P1006 PC TEST' -ErrorAction SilentlyContinue)) { Add-Printer -Name 'HP P1006 PC TEST' -IppURL 'http://127.0.0.1:8632/ipp/print' }; Get-Printer -Name 'HP P1006 PC TEST' | Select-Object Name,DriverName,PortName | Format-List";
        let result=hidden(Command::new("powershell.exe").args(["-NoProfile","-NonInteractive","-Command",script])).output().map_err(|e|e.to_string())?;
        if !result.status.success() { return Err(String::from_utf8_lossy(&result.stderr).into_owned()); }
        Ok(String::from_utf8_lossy(&result.stdout).into_owned())
    }).await.map_err(|e|e.to_string())?
}
fn main() {
    tauri::Builder::default().manage(Worker::default())
        .invoke_handler(tauri::generate_handler![start_worker,stop_worker,cancel_job,check_esp,install_queue])
        .on_window_event(|w,e| {if matches!(e,tauri::WindowEvent::CloseRequested{..}) { let state=w.state::<Worker>();let _=stop(&state); }})
        .run(tauri::generate_context!()).expect("HP PC TEST startup failed");
}
