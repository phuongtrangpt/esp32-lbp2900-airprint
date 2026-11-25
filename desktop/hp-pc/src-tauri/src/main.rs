#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod service_common;
use std::{io::{Read,Write},net::{Ipv4Addr,SocketAddr,TcpStream},process::Command,time::Duration};
use std::os::windows::process::CommandExt;
use tauri::{Manager,menu::{Menu,MenuItem},tray::{TrayIconBuilder,TrayIconEvent}};
fn hidden(c:&mut Command)->&mut Command{c.creation_flags(0x08000000);c}
fn show(app:&tauri::AppHandle){if let Some(w)=app.get_webview_window("main"){let _=w.unminimize();let _=w.show();let _=w.set_focus();}}
fn elevated(app:&tauri::AppHandle,args:&[&str])->Result<(),String>{
    let exe=app.path().resource_dir().map_err(|e|e.to_string())?.join("hp-pc-service.exe");
    let quote=|s:&str|format!("'{}'",s.replace('\'',"''"));
    let arguments=args.iter().map(|s|quote(s)).collect::<Vec<_>>().join(",");
    let script=format!("$ErrorActionPreference='Stop'; $p=Start-Process -FilePath {} -ArgumentList @({}) -Verb RunAs -Wait -PassThru; exit $p.ExitCode",quote(&exe.to_string_lossy()),arguments);
    let result=hidden(Command::new("powershell.exe").args(["-NoProfile","-NonInteractive","-Command",&script])).output().map_err(|e|e.to_string())?;
    match result.status.code(){Some(0)=>Ok(()),Some(10)=>Err("Đang in. Hãy chờ lệnh in xong trước khi đổi IP.".into()),Some(11)=>Err("IP không hợp lệ".into()),Some(12)=>Err("Không lưu được cấu hình dịch vụ".into()),_=>Err("Chưa đổi được dịch vụ. Hãy chấp nhận yêu cầu quyền quản trị của Windows hoặc kiểm tra dịch vụ đã cài.".into())}
}
#[tauri::command]
async fn start_worker(app:tauri::AppHandle,ip:String)->Result<(),String>{ip.parse::<Ipv4Addr>().map_err(|_|"IPv4 không hợp lệ")?;tauri::async_runtime::spawn_blocking(move||elevated(&app,&["--set-ip",&ip])).await.map_err(|e|e.to_string())?}
#[tauri::command]
async fn stop_worker(app:tauri::AppHandle)->Result<(),String>{tauri::async_runtime::spawn_blocking(move||elevated(&app,&["--stop"])).await.map_err(|e|e.to_string())?}
#[tauri::command]
fn get_snapshot()->serde_json::Value{service_common::snapshot()}
#[tauri::command]
fn hide_window(app:tauri::AppHandle){if let Some(w)=app.get_webview_window("main"){let _=w.hide();}}
#[tauri::command]
fn quit_manager(app:tauri::AppHandle){app.exit(0);}
#[tauri::command]
async fn cancel_job()->Result<(),String>{tauri::async_runtime::spawn_blocking(||{
    let id=service_common::status_file()["active_job"].as_u64().ok_or("Không có lệnh đang in")? as u32;
    let mut body=vec![2,0,0,8,0,0,0,1,1];
    fn attr(b:&mut Vec<u8>,tag:u8,name:&str,value:&[u8]){b.push(tag);b.extend_from_slice(&(name.len() as u16).to_be_bytes());b.extend_from_slice(name.as_bytes());b.extend_from_slice(&(value.len() as u16).to_be_bytes());b.extend_from_slice(value);}
    attr(&mut body,0x47,"attributes-charset",b"utf-8");attr(&mut body,0x48,"attributes-natural-language",b"en");attr(&mut body,0x45,"printer-uri",b"ipp://127.0.0.1:8632/ipp/print");attr(&mut body,0x21,"job-id",&id.to_be_bytes());body.push(3);
    let mut s=TcpStream::connect_timeout(&SocketAddr::from(([127,0,0,1],8632)),Duration::from_secs(5)).map_err(|e|e.to_string())?;
    s.set_read_timeout(Some(Duration::from_secs(5))).map_err(|e|e.to_string())?;s.set_write_timeout(Some(Duration::from_secs(5))).map_err(|e|e.to_string())?;
    let header=format!("POST /ipp/print HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/ipp\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",body.len());s.write_all(header.as_bytes()).and_then(|_|s.write_all(&body)).map_err(|e|e.to_string())?;
    let mut response=Vec::new();s.take(65536).read_to_end(&mut response).map_err(|e|e.to_string())?;
    let start=response.windows(4).position(|x|x==b"\r\n\r\n").ok_or("Invalid IPP response")?+4;
    if response.get(start+2..start+4)!=Some(&[0,0]){return Err("Không hủy được lệnh; kiểm tra hàng đợi Windows".into());}Ok(())
}).await.map_err(|e|e.to_string())?}
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
fn main(){
    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app,args,_cwd|{if args.iter().any(|s|s=="--quit"){app.exit(0);}else{show(app);}}))
        .invoke_handler(tauri::generate_handler![start_worker,stop_worker,cancel_job,check_esp,install_queue,get_snapshot,hide_window,quit_manager])
        .setup(|app|{
            if std::env::args().any(|a|a=="--quit"){app.handle().exit(0);return Ok(());}
            let open=MenuItem::with_id(app,"open","Mở quản lý / đổi IP",true,None::<&str>)?;
            let hide=MenuItem::with_id(app,"hide","Ẩn giao diện",true,None::<&str>)?;
            let quit=MenuItem::with_id(app,"quit","Thoát giao diện (dịch vụ in vẫn chạy)",true,None::<&str>)?;
            let menu=Menu::with_items(app,&[&open,&hide,&quit])?;
            TrayIconBuilder::with_id("hp-pc").icon(app.default_window_icon().ok_or("Missing icon")?.clone()).tooltip("HP P1006 — Windows print service").menu(&menu)
                .on_menu_event(|app,event|match event.id.as_ref(){"open"=>show(app),"hide"=>{if let Some(w)=app.get_webview_window("main"){let _=w.hide();}},"quit"=>app.exit(0),_=>{}})
                .on_tray_icon_event(|tray,event|{if matches!(event,TrayIconEvent::DoubleClick{..}){show(tray.app_handle());}}).build(app)?;
            show(app.handle());Ok(())
        })
        .on_window_event(|w,event|{if let tauri::WindowEvent::CloseRequested{api,..}=event{api.prevent_close();let _=w.hide();}})
        .run(tauri::generate_context!()).expect("HP PC manager startup failed");
}
