# ESP32-S3 AirPrint cho Canon LBP2900

Print server Wi-Fi nhỏ gọn cho Canon LBP2900, phát triển từ
[ngxson/esp32-usb-to-airprint](https://github.com/ngxson/esp32-usb-to-airprint).
Driver CAPT/Hi-SCoA được chuyển từ [ValdikSS/captdriver, nhánh val](https://github.com/ValdikSS/captdriver/tree/val).

**Bản thử nghiệm, chưa xác nhận trên máy in thật.** Build thành công và test mô phỏng
không đồng nghĩa với việc đã in ổn định. Cần thử USB, trang đầu tiên và nhiều trang
trên LBP2900 trước khi coi là bản dùng hằng ngày. Không cần máy tính hoặc CUPS chạy
thường trực: ESP32 thực hiện chuyển raster sang CAPT và giao tiếp với máy in.

## Phần cứng

- ESP32-S3 **N16R8** (16 MB flash, 8 MB octal PSRAM), có cổng UART/COM và USB OTG riêng.
- Nguồn 5 V ổn định cho board; máy in dùng nguồn điện riêng.
- Cáp USB nối cổng **USB OTG** của board với cổng USB-B máy in, bảo đảm có VBUS 5 V.
- Wi-Fi 2.4 GHz; điện thoại và ESP32 cùng LAN, không bị AP isolation chặn mDNS.

ESP32 thường, ESP8266 hoặc board S3 không đúng flash/PSRAM không phải mục tiêu của
bản firmware này. Chỉ nối VBUS theo sơ đồ của đúng board; không nối tắt nguồn USB
của hai bộ cấp điện khác nhau.

## Tính năng của bản đầu

- Nhận IPP/AirPrint, raster URF/PWG; quảng bá Bonjour/mDNS.
- Canon LBP2900: USB `04a9:2676`, CAPT hai chiều, Hi-SCoA, đen trắng 600 dpi.
- Khổ A4 và Letter, lề theo driver CAPT, chuyển ảnh RGB/xám thành raster 1 bit.
- Giao diện web để chọn PDF/ảnh, xem tiến trình và lịch sử lệnh in.
- Wi-Fi nhập qua COM ở lần khởi động đầu, lưu trong NVS; không nằm trong bản build chung.
- Nút BOOT: nhấn để in trang kiểm tra; giữ 2 giây để hủy lệnh đang chạy.
- Timeout khi đọc/ghi CAPT; lỗi phản hồi dừng lệnh thay vì thoát toàn bộ firmware.

LBP2900 không có scanner: thiết bị không quảng bá AirScan. Một số thành phần scanner
của dự án gốc vẫn nằm trong mã nguồn và bộ test hồi quy. In iOS/Mac/Android qua hệ
thống in sẵn có là mục tiêu cần thử trên thiết bị thật, chưa phải chứng nhận AirPrint/Mopria.

## Nạp firmware đã build

1. Cài Python 3 và `python -m pip install esptool pyserial` trên Windows/Linux/macOS.
2. Cắm máy tính vào cổng **UART/COM**, xác định COM (ví dụ `COM5`).
3. Giải nén gói, chạy `python tools/flash.py --port COM5 --build-dir firmware`.
   Lệnh ghi bootloader, partition table, khởi tạo lựa chọn OTA và firmware; không tự xóa NVS đã có.
4. Mở monitor: `python -m serial.tools.miniterm COM5 115200 --eol LF`.
   Nếu chưa thấy lời nhắc, nhấn RESET/EN trên board khi monitor đang mở.
5. Nhập SSID, Enter, nhập mật khẩu, Enter. Firmware không in lại mật khẩu.
   Theo dõi dòng IP kết nối. Chỉ dùng Wi-Fi 2.4 GHz, mật khẩu 8–63 ký tự hoặc để trống
   cho mạng mở. SSID tối đa 32 byte UTF-8.
6. Nối máy in vào USB OTG. Truy cập `http://canon-lbp2900.local/` hoặc IP in trên COM.
7. In trang kiểm tra bằng BOOT, rồi thử một trang PDF và hai trang liên tiếp.

Nếu đổi Wi-Fi, dùng `python tools/flash.py --port COM5 --reset-wifi` để xóa vùng NVS
`0x9000..0xefff`; thao tác này xóa thiết lập Wi-Fi của board. Sau reset, nhập lại qua COM.
Không gửi mật khẩu, `.env`, `src/local_config.h` hoặc ảnh chụp có mật khẩu lên GitHub.

## Tự build

Cài Python 3, Node.js 24 LTS và Git. Trong thư mục dự án:

```sh
python -m pip install platformio==6.2.0
pio run -e esp32s3
python tools/package_firmware.py
```

Firmware mặc định có Wi-Fi trống, nhập qua COM. Nếu muốn cấu hình lúc build, sao chép
`.env.example` thành `.env` và sửa trên máy riêng. `.env` và header sinh ra được git-ignore.
Không chia sẻ firmware có chứa Wi-Fi đã biên dịch vào. Chế độ mặc định `normal` tắt
các cổng debug/OTA không có xác thực của dự án gốc.

Build PlatformIO được cố định ở Espressif32 6.12.0. Không thay board hoặc partition
nếu chưa kiểm tra. `firmware/` được tạo bởi script đóng gói, có SHA256SUMS và các file
`.bin`; giữ nguyên tên và địa chỉ nạp. Không nạp riêng firmware.bin vào địa chỉ 0.

## Kiểm tra

```sh
make -C host all
ASAN_OPTIONS=detect_leaks=0 host/capt_host
python tools/test_capt_ipp.py
cd web
npm ci
npm run check
node --test tests/*.test.ts
npm run build
```

`capt_host` dùng phản hồi CAPT mô phỏng, chia nhỏ dữ liệu đọc, kiểm tra nhiều trang
A4/Letter, mất kết nối và header sai. Bộ giải nén độc lập trong `host/capt_reference`
đối chiếu từng pixel của các band, kể cả bit padding và kết thúc trang. `test_capt_ipp.py`
kiểm tra toàn luồng IPP → URF → CAPT, giới hạn độ phân giải và trạng thái lệnh.

Công cụ hồi quy PIXMA/scanner của upstream được giữ lại. Xem `VALIDATION.md` để phân
biệt các kiểm tra đã chạy, phần không chạy được và phần cần máy thật.

## Thử nghiệm trên máy thật và báo lỗi

1. Xác nhận COM báo đúng `04a9:2676`, bulk IN/OUT, PSRAM và IP.
2. BOOT → trang hình chữ nhật và các ô xám; kiểm tra hướng giấy, lề, mức đậm.
3. In PDF 1 trang rồi 2 trang; thử A4, Letter nếu có giấy tương ứng.
4. Tắt/bật máy in rồi thử lại. Hủy một lệnh đang in và gửi lệnh mới.
5. Thử hết giấy/kẹt giấy chỉ khi có thể xử lý trực tiếp. Chưa hỗ trợ tự in lại trang
   sau lỗi; có thể cần bật lại máy in và gửi lại lệnh.
6. Khi báo lỗi: phiên bản, loại board, kết nối cáp, thời điểm lỗi và log COM đã
   loại bỏ thông tin riêng. Không báo là "đã hỗ trợ ổn định" chỉ dựa vào build.

Đây là cổng mạng in raster, không phải bộ chuyển USB trong suốt cho driver Canon
Windows. Chưa có in hai mặt tự động, khổ tùy biến, IPPS hoặc giao diện quản trị xác thực.
Chỉ dùng trong LAN tin cậy. Thời gian/độ ổn định thực tế sẽ được ghi sau khi thử máy thật.

## GitHub

Kho dự án: https://github.com/phuongtrangpt/esp32-lbp2900-airprint (công khai).
Workflow `.github/workflows/build.yml` chạy test và build, xuất artifact firmware
cho mỗi commit hoặc chạy tay. Các ảnh chụp PIXMA của upstream không cần cho việc
build được dẫn liên kết trong `docs/UPSTREAM_IMAGES.md`.

## Giấy phép

Bản kết hợp phát hành theo **GPL-3.0-or-later** do sử dụng driver CAPT GPL.
Giữ nguyên bản quyền MIT của firmware gốc trong `LICENSE.upstream-MIT`; nguồn CAPT,
commit và thay đổi ghi trong `components/capt/UPSTREAM.md`. Phải cung cấp mã nguồn
và thông báo bản quyền tương ứng khi phân phối firmware. Dự án cộng đồng, không
được Canon bảo trợ hoặc chứng nhận.
