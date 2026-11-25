# HP P1006 PC Offload — TEST 1

Nhánh riêng `feat/hp-pc-offload`. Không hợp nhất hoặc phát hành lên web chính thức.
Chưa thử trên ESP / máy in thật hoặc máy Windows của người dùng.

Windows dùng Microsoft IPP Class Driver gửi PWG raster đến dịch vụ chỉ lắng nghe
`127.0.0.1:8632/ipp/print`. Bộ chuyển đổi C trên PC dùng nguyên mã raster / mono
packing / JBIG / XQX của HP hiện tại. Nhận đủ và chuyển đổi thành công **toàn bộ**
tài liệu mới bắt đầu gửi dữ liệu đã nén qua TCP 9101. ESP chỉ kiểm tra khung,
CRC, trình tự XQX và chuyển qua USB; không render/nén trên đường này.
Không dùng trình điều khiển HP1006PrintProc/IMF hay bắt tay RAW 9100 của Windows.

## Kiểm thử dự kiến

1. Dùng ESP32-S3 N16R8 dành riêng cho thử nghiệm; nạp profile **hp_pc**, phiên bản
   **0.1.0-hp-pc-test1** qua USB. Không thay firmware chính thức của khách.
2. Cài `HP P1006 PC TEST` trên Windows 10/11 x64, mở app, nhập IP ESP và kiểm tra.
3. Bấm **Bật dịch vụ in**, sau đó **Thêm HP P1006 PC TEST**. Hàng đợi cũ giữ nguyên.
   Cmdlet `Add-Printer -IppURL` phải có trên Windows; nếu Windows báo thiếu quyền,
   mở app bằng Run as administrator chỉ để thêm hàng đợi.
4. Giữ app mở; Ctrl+P từ PDF/Word, chọn **HP P1006 PC TEST**, A4/A5/A6/Letter,
   300 hoặc 600 dpi, một mặt, một bản. Chọn trang trong ứng dụng nguồn.
5. Thử lần lượt một trang, PDF chọn trang 5–10, 10 trang, hủy lệnh, rút mạng khi
   đang gửi rồi in lại bằng tay sau khi kiểm tra giấy. Ghi thời gian nhận/render/
   gửi và log ESP; so sánh với IPP trực tiếp trên cùng tài liệu/thiết lập.

Điện thoại vẫn có thể IPP trực tiếp tới ESP qua 631 khi USB rảnh. Dịch vụ PC không
nhận yêu cầu từ LAN. Không hỗ trợ in từ điện thoại qua PC trong TEST 1.

## Giới hạn và ý nghĩa trạng thái

- Bản này dành riêng HP P1006 USB `03f0:3e17`; không dùng cho Canon.
- Giữ app và Windows hoạt động trong khi in. Chưa có tự khởi động hoặc chạy nền
  khi đóng cửa sổ. Hàng đợi thử sẽ báo offline khi app đóng.
- Không nhận PDF trực tiếp qua IPP; Windows raster hóa PDF/Word trước. Chỉ gray8
  và RGB8 PWG/URF, khổ giấy/dpi đồng nhất trong một lệnh. Không duplex/copies>1.
- Tối đa 256 MiB tài liệu, tối đa ba lệnh chưa kết thúc, hai lệnh chờ xử lý.
  Dữ liệu tạm xóa sau khi kết thúc; không phục hồi/in lại tự động khi app mở lại.
- Lỗi trang cuối bị thiếu dữ liệu sẽ từ chối cả lệnh **trước khi gửi USB**. Không
  thể phát hiện trường hợp phía gửi khai báo một tài liệu ít trang nhưng hợp lệ.
- CRC và ACK xác nhận byte đã được chuyển qua USB, **không** xác nhận giấy đã ra.
  Lỗi mạng/USB có thể để lại một phần giấy đã in: kiểm tra trước khi gửi lại.
- Giao thức 9101 chỉ dùng trong LAN tin cậy, chưa mã hóa/xác thực. 4 KiB buffer
  PSRAM + task stack 4 KiB, ngoài buffer USB có sẵn; chưa đo RAM trên ESP thật.
- Dự kiến giảm tải CPU ESP; mức tăng tốc chỉ có thể kết luận sau đo trên máy thật.

## Build / kiểm tra

Linux native (gcc): xem các lệnh trong `.github/workflows/hp-pc-offload.yml`.
Windows: chạy `python desktop/hp-pc/make_icon.py` tạo icon tái lập,
MinGW-w64 build converter, Python 3.12 + PyInstaller 6.16.0 build worker,
Rust stable + Tauri 2 build NSIS. Installer gói sẵn worker và converter; người dùng
không cần cài Python. Xem workflow để tái tạo đúng các bước.
ESP: `pio run -e hp_pc`; firmware có tên app/OTA profile riêng.

GPL-3.0-or-later; bộ JBIG gốc GPL-2.0-or-later. Giữ giấy phép gốc trong repo.
Mã nguồn tương ứng nằm trên cùng nhánh/commit với artifact Actions.

## Kết quả kiểm tra trước phần cứng

- 24 bài kiểm thử native chạy qua trên Linux: PWG/URF, gray/RGB, nhiều trang,
  cắt trang cuối, số trang URF không khớp, HTTP chunked/EOF, Create/Send/Cancel,
  chống lặp khi mất HTTP reply, khung TCP bị chia nhỏ, sai CRC/thứ tự, máy sai/
  bận, lệnh gửi trùng và truy vấn sau khi mất ACK cuối. USB dùng sink mô phỏng.
- Giao thức C và adapter chuyển đổi chạy với AddressSanitizer + UndefinedBehaviorSanitizer.
- Bộ HP hiện tại vẫn cho kết quả XQX 300 dpi gray giống từng byte với đường
  600 dpi đã phóng lớn; kiểm thử IPP/CAPT hiện có chạy qua.
- Các bài native Windows chạy trên Actions, kiểm tra thêm worker đã đóng gói
  với ESP giả lập. Trên runner Windows, thêm hàng đợi Microsoft IPP Class Driver
  và PrintTestPage đã qua: raster thực của Windows chuyển đổi thành HP XQX,
  chỉ thay tầng USB bằng mô phỏng. Bản 064c33d: 12 bài Windows chạy qua, 10 bài
  POSIX được bỏ qua; luồng packaged worker cũng đã qua. Hai kiểm tra nhiều khung
  và CRC toàn lệnh bổ sung chạy qua native Linux sau build này.
- QEMU chưa chạy được tại môi trường phát triển hiện tại vì thiếu PlatformIO/
  QEMU. Kiểm thử IPP CUPS conformance không chạy do thiếu ipptool; script cũ ghi
  0 pass/0 fail. Không dùng các mục này làm bằng chứng kiểm thử thành công.
- Chưa đo tốc độ in, RAM trên ESP thật hoặc thời gian giấy ra.

Mã nguồn: https://github.com/phuongtrangpt/esp32-lbp2900-airprint/tree/feat/hp-pc-offload

## Windows 0.1.1 FIX 1

Chỉ cập nhật ứng dụng PC, dùng lại firmware `0.1.0-hp-pc-test1`; không cần nạp ESP.
Nhận sai số làm tròn kích thước giấy tối đa 0.5 mm cho A4/Letter/A5/A6, kể cả
Windows đặt tên form PDF là custom; vẫn từ chối khổ tùy chỉnh ngoài giới hạn.
Bộ chuyển đổi trên PC dùng cùng dung sai. Lỗi khổ giấy ghi số đo nhận thực tế.
Khóa IP khi dịch vụ đang chạy và hiển thị đúng đích ESP:9101. Dừng dịch vụ, chờ
đã dừng rồi đổi IP/bật lại; không khởi tạo worker mới khi worker cũ chưa thoát.

## 0.2.0 — Windows Service

Dịch vụ `VietPhuongHPP1006PC` chạy tự động ngay khi Windows khởi động, bằng
tài khoản LocalService, độc lập với giao diện và phiên đăng nhập. Cài bằng quyền
quản trị; mở giao diện một lần, nhập IP ESP rồi Lưu IP / bật dịch vụ. Các lần
in sau chọn HP P1006 PC TEST như máy in thường, không cần mở ứng dụng.

Nút X ẩn giao diện xuống khay. Thoát giao diện cũng không dừng dịch vụ. Đổi IP
hoặc dừng để thay máy cần quyền quản trị; đổi IP bị chặn khi đang in. Windows
tự khởi động lại service host nếu lỗi; host tự phục hồi worker. Lệnh in bị gián
đoạn không được service tự phục hồi từ file tạm; kiểm tra giấy trước khi gửi lại.

Cấu hình và trạng thái ở ProgramData/VietPhuong/HP-P1006-PC; file tài liệu tạm
có ACL riêng, chỉ LocalService, SYSTEM và quản trị được truy cập. Gỡ ứng dụng
sẽ dừng và xóa dịch vụ. Dùng lại firmware ESP 0.1.0-hp-pc-test1.

Cài nâng cấp: chờ hết lệnh in, đóng bản 0.1.x cũ (để giải phóng cổng 8632),
cài 0.2.0 rồi lưu IP một lần. Giữ hàng đợi HP P1006 PC TEST hiện có.
