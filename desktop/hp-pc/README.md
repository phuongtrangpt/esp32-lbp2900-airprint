# HP P1006 PC Offload 0.2.2 — Windows Service

Windows xử lý raster, halftone và JBIG/XQX; ESP chỉ nhận khung kiểm tra CRC rồi
chuyển sang USB trên cổng 9101. Nhánh `feat/hp-pc-offload`; firmware chính thức
Canon LBP2900 / HP P1006 và trang nạp web giữ nguyên.

## Cài và sử dụng

1. Chờ hết lệnh in, đóng bản 0.1.x cũ để giải phóng cổng 8632.
2. Cài bản 0.2.2, chấp nhận quyền quản trị để đăng ký Windows Service.
3. Mở giao diện một lần, nhập IP mạch ESP (mạch thử hiện tại: 192.168.10.21),
   bấm **Lưu IP / bật dịch vụ**, chấp nhận quyền quản trị khi Windows yêu cầu.
4. Dùng hàng đợi **HP P1006 PC TEST** đã thêm. Máy mới chưa có hàng đợi thì bấm
   **Thêm HP P1006 PC TEST** một lần.
5. Từ các lần sau chỉ cần Ctrl+P và chọn máy in này. Không cần mở giao diện.
   Dịch vụ tự chạy khi Windows bật, độc lập với phiên đăng nhập.

Nút X ẩn giao diện xuống khay hệ thống. Menu khay có Mở quản lý và Thoát giao diện;
thoát giao diện vẫn in được. Đổi IP hoặc dừng để thay máy cần quyền quản trị.
Đổi IP bị chặn khi đang xử lý lệnh; hãy chờ xong trước khi đổi.

**Bản 0.2.2 cần firmware ESP `0.1.1-hp-pc-usbwait` để sửa chờ USB khi in nhiều trang.**
Firmware chỉ hỗ trợ HP P1006 USB 03f0:3e17 trên ESP32-S3 N16R8. Điện thoại vẫn
có thể IPP trực tiếp vào ESP khi USB rảnh; ứng dụng Windows dùng cổng riêng.

## Cách chạy nền

- Service: `VietPhuongHPP1006PC`, Automatic, tài khoản LocalService.
- Windows SCM khởi động lại host sau lỗi; host tự khởi động lại worker nếu lỗi.
- Worker / converter thuộc Windows Job Object, tự kết thúc khi host chết;
  tránh tiến trình cũ chiếm cổng sau khi service phục hồi.
- Giao diện Tauri chỉ quản lý; đóng hoặc thoát giao diện không dừng service.
- Cấu hình và trạng thái nằm ở ProgramData/VietPhuong/HP-P1006-PC. Tài liệu tạm
  có ACL riêng: LocalService, SYSTEM và quản trị; không cấp đọc cho Users.
- Không phục hồi/in lại từ file tài liệu còn sót sau lỗi. Kiểm tra giấy trước
  khi tự gửi lại. Dữ liệu tạm cũ được dọn khi worker mới khởi động.
- Gỡ cài đặt sẽ dừng và xóa service; cấu hình được giữ lại để cài lại.

## In và giới hạn

Máy Windows 10/11 x64 cần Microsoft IPP Class Driver / Add-Printer -IppURL.
Driver tạo PWG/URF và gửi đến 127.0.0.1:8632/ipp/print. Nhận đủ và chuyển đổi
thành công toàn bộ tài liệu trên PC trước khi gửi dữ liệu nén tới ESP. Không
sử dụng driver HP1006PrintProc/IMF hoặc bắt tay RAW 9100 của Windows.

A4/Letter/A5/A6 dọc, 300/600 dpi, gray8/RGB8, một mặt, một bản; khổ/dpi đồng nhất
trong lệnh. Chấp nhận sai số kích thước giấy 0.5 mm do PDF/Windows làm tròn.
Tối đa 256 MiB, ba lệnh chưa kết thúc (hai lệnh chờ). Chưa nhận PDF trực tiếp
qua IPP; PDF/Word được Windows raster hóa trước. Chưa phục vụ điện thoại qua PC.

“Đã chuyển qua USB” xác nhận giao dữ liệu, không xác nhận giấy đã ra. Mất mạng /
USB có thể để lại một phần giấy đã in. TCP 9101 chỉ dành LAN tin cậy, chưa TLS /
xác thực. Dịch vụ IPP của PC chỉ lắng nghe loopback, không mở ra LAN.

## Kiểm thử và tái tạo build

Bản 0.1.1 đã được người dùng xác nhận ra đủ sáu tờ PDF trang 5–10 trên HP P1006:
PC xử lý 0.45 s, truyền 3.20 s; đây không phải tổng thời gian giấy ra. Lượt đầu
phải bootstrap firmware HP nên lâu hơn. Service 0.2.0 cần kiểm thử thực tế tiếp.

Xem `.github/workflows/hp-pc-offload.yml`: native C + ASan/UBSan, 27 kiểm thử
PWG/URF/IPP và khung truyền; UI lifecycle; native Windows converter, Microsoft
IPP Class Driver TestPage, worker đóng gói; Windows Service cài từ NSIS,
LocalService render -> ESP giả lập, đóng/thoát GUI không dừng in, phục hồi sau
khi kill worker/host và gỡ service. USB trong CI là mô phỏng; không thay cho
kiểm thử máy in thật. QEMU / ipptool không có ở môi trường local hiện tại.

Build: MinGW-w64 C converter, Python 3.12 + PyInstaller 6.16.0 worker, Rust
service-host + Tauri 2 manager, NSIS per-machine. Installer gồm đầy đủ runtime;
người dùng không cần Python hoặc Rust. License GPL-3.0-or-later; giữ các thông
báo gốc trong mã HP / JBIG (JBIG gốc GPL-2.0-or-later).

Mã nguồn: https://github.com/phuongtrangpt/esp32-lbp2900-airprint/tree/feat/hp-pc-offload

### Trạng thái máy in thật

Worker kiểm tra API trạng thái USB của ESP và giao thức 9101 khoảng mỗi 3 giây.
Không coi việc dịch vụ PC đang chạy là máy in sẵn sàng. Trạng thái kết nối được
đưa vào IPP printer-state / printer-state-reasons / printer-state-message để
Windows đọc, và vào giao diện quản lý. Windows có thể cập nhật chậm theo chu kỳ
truy vấn của driver. Không quét thêm USB khi đang truyền lệnh của chính PC.

Lệnh chưa gửi USB được giữ ở PC nếu mất kết nối hoặc máy in đang bận, tiếp tục
khi kết nối trở lại. Lệnh đã truyền một phần rồi lỗi vẫn dừng, không tự in lại.
Trạng thái thiết bị hoạt động với cả firmware cũ; sửa chờ USB cần firmware mới như ghi trên.

### Bản 0.2.2

Sửa cache IPP nhầm các lần in mới có cùng request-id/nội dung thành lệnh cũ.
Print-Job / Create-Job luôn tạo lệnh mới; Send-Document chỉ chống gửi trùng theo
lệnh đã có job-id. Không thay đổi cơ chế chống phát lại USB sau truyền lỗi.

Giao diện tự trở về trạng thái không có lệnh sau khi xử lý xong, hủy hoặc lỗi;
ẩn tiến trình và vô hiệu nút Hủy khi không có lệnh thực sự. Kết quả vẫn lưu
trong nhật ký. Truy vấn khổ giấy và trạng thái thiết bị không ghi đè tiến trình.
Không thay đổi bộ xử lý raster, giao thức USB hoặc firmware ESP.

### Chờ USB khi in nhiều trang — 0.2.2 / 0.1.1

Log thực tế: ESP dừng sau 2637824 byte với status=103 và bulk OUT canceled.
Đường PC cũ dùng API RAW giới hạn 12 giây; chỉ đường `hp_pc` nay dùng ngân sách
120 giây cho write/flush. Driver HP, CAPT và RAW 9100 chính thức giữ nguyên.
PC chờ cùng ACK tối đa 150 giây, hiện tiến độ chờ mỗi 5 giây và vẫn nhận hủy.
Không gửi lại khối đã gửi; lỗi có status, frame và số byte đã được xác nhận.
Log HELLO 3 giây được giảm để không che khuất lỗi USB trong ring buffer.

Kiểm tra 30 trang qua giao thức C thật + USB giả lập giữ ACK 13 giây, toàn bộ
byte khớp; kiểm tra ACK phân mảnh, timeout, hủy và mất ACK cuối vẫn được giữ.
Cần người dùng xác nhận lệnh 30 trang trên máy HP thật. Gói thử gồm bộ cài
Windows 0.2.2 và firmware ESP 0.1.1; không đưa lên trang nạp chính thức.
