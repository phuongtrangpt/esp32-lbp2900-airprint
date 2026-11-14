# ESP32-S3 AirPrint cho Canon LBP2900 và HP P1006

Print server Wi-Fi nhỏ gọn cho Canon LBP2900 và HP LaserJet P1006, phát triển từ
[ngxson/esp32-usb-to-airprint](https://github.com/ngxson/esp32-usb-to-airprint).
Driver CAPT/Hi-SCoA được chuyển từ [ValdikSS/captdriver, nhánh val](https://github.com/ValdikSS/captdriver/tree/val).

**Bản thử nghiệm, chưa xác nhận trên máy in thật.** Build thành công và test mô phỏng
không đồng nghĩa với việc đã in ổn định. Cần thử USB, trang đầu tiên và nhiều trang
trên LBP2900 trước khi coi là bản dùng hằng ngày. Không cần máy tính hoặc CUPS chạy
thường trực: ESP32 thực hiện chuyển raster sang CAPT và giao tiếp với máy in.

## HP LaserJet P1006 (v0.3.0-dual-preview)

- Tự chọn driver khi cắm Canon `04a9:2676` hoặc HP `03f0:3e17`.
- HP dùng XQX + JBIG, đen trắng 600 dpi, A4/Letter, in một mặt.
- Mỗi ESP nhận **một máy in USB tại một thời điểm**, chưa hỗ trợ hub hay hai máy đồng thời.
- P1006 cần firmware riêng mỗi lần bật nguồn. Sau khi cấu hình Wi-Fi, mở
  `http://<IP-ESP>/hp-firmware`, chọn **sihpP1006.dl** từ bộ driver HP/foo2xqx của bạn,
  bấm lưu, sau đó tắt/bật máy in và cắm lại USB. ESP giữ file trong flash và gửi
  trước lệnh in đầu tiên của mỗi lần kết nối nếu IEEE1284 chưa có `FWVER:`.
- File HP này không được đóng gói trong firmware ESP. Không chọn `.bin` ESP,
  `.img` chưa chuyển HPDL, hay firmware của model khác. Tham khảo
  [hướng dẫn foo2zjs](https://github.com/OpenPrinting/foo2zjs/blob/main-fixes/INSTALL.in).
- Thiếu file thì lệnh in thất bại an toàn; log hướng dẫn mở `/hp-firmware`.
- Trang nạp USB hiện tại vẫn dùng chung cho cả hai máy; nạp lại bản mới để có
  phân vùng lưu firmware HP. Nạp app qua OTA từ bản cũ không tự đổi bảng phân vùng.
- Chưa xác nhận trên phần cứng. Giới hạn hiện tại: chờ khởi động HP 6 giây,
  chưa phân tích trạng thái PJL/hết giấy, chưa xác nhận giấy đã ra khi IPP báo xong.

## Mở rộng theo giao thức (v0.4.0-standard-preview)

Hai driver dùng chung mới: **PCL5 đơn sắc** và **PostScript Level 2/3 đơn sắc**,
300/600 dpi, A4/Letter, in một mặt, lề bảo thủ 6,35 mm. Mỗi dòng raster được gửi
ngay; bộ đệm driver khoảng 2 KB, không tăng theo số trang. Máy màu có PCL5c/PS
có thể dùng đường in đơn sắc này; chưa có in màu / duplex / scanner cho driver chung.

Máy phải dùng USB Printer Class và khai báo CMD phù hợp trong IEEE1284 ID.
Firmware ưu tiên driver Canon/HP chuyên dụng, sau đó mới chọn PCL5/PS từ CMD;
không đoán giao thức dựa trên tên thương hiệu hoặc tự gửi dữ liệu thử vào máy lạ.
Các token PCLXL/PCL6 riêng lẻ, PCL3GUI/PCLm, UFRII, SPL và ZJS không được nhận
nhầm là PCL5. Thiếu CMD phù hợp thì từ chối in. Cần kiểm chứng trên từng model.

| Dòng máy / nhóm | Tình trạng trong firmware |
|---|---|
| Canon LBP2900 | Driver CAPT riêng, chưa thử phần cứng |
| HP P1006 | Driver XQX/JBIG riêng + HPDL do người dùng cung cấp |
| Canon PIXMA MG/MP/iP/E/TS/MX có sẵn trong registry | Giữ nguyên driver BJL của dự án gốc; không có nghĩa mọi PIXMA đều được hỗ trợ |
| HP LaserJet 1320, P2035, P2055 | Ứng viên PCL5/PS theo thông số hãng; chỉ tự chọn nếu USB CMD công bố phù hợp, chưa in thử |
| Máy USB khác khai báo PCL5-compatible hoặc PostScript | Driver chung thử nghiệm, cần in trang kiểm tra |
| Canon LBP6030/6030w | **Chưa hỗ trợ**: UFR II LT, khác CAPT |
| HP P1102; HP1020 và các máy host-based chưa có adapter | **Chưa hỗ trợ**: cần giao thức/khởi tạo riêng |
| Brother HL-L2321D | **Chưa hỗ trợ**: không giả định PCL6 của model khác trong cùng họ |
| Epson L3210/L3250, Canon G-series chưa có adapter, Samsung SPL | **Chưa hỗ trợ**: không thêm tên để giả lập tương thích |

Nguồn kỹ thuật:
[HP PCL5 reference](https://www.hp.com/ctg/Manual/bpl13210.pdf),
[Adobe PostScript reference](https://www.adobe.com/jp/print/postscript/pdfs/PLRM.pdf),
[HP P2035](https://support.hp.com/us-en/product/product-specs/hp-laserjet-p2035-printer-series/3662026),
[HP P2055](https://support.hp.com/vn-en/product/product-specs/hp-laser-p2055-series/3662052),
[HP1320](https://support.hp.com/us-en/product/product-specs/hp-laser-1320-series/410622),
[Canon6030](https://vn.canon/en/consumer/imageclass-lbp6030/main/product),
[Brother2321D](https://support.brother.com/g/b/spec.aspx?c=vn&lang=en&prod=hll2321d_as),
[Epson3210](https://www.epson.com.vn/p/C11CJ68501).
Đây là danh sách tương thích kỹ thuật / ứng viên thử nghiệm, không phải bảng xếp
hạng thị phần Việt Nam hay chứng nhận đã in ổn định trên tất cả model.

## Bộ nhớ và tài liệu nhiều trang

- ESP xử lý luồng URF/PWG, không lưu toàn bộ PDF hoặc cả tài liệu trong RAM.
- Canon giữ dải 70 dòng; HP P1006 giữ một trang 1 bit và bộ đệm JBIG, tái sử dụng
  cho trang tiếp theo; các block JBIG được cấp phát rõ vào PSRAM để tránh chiếm
  RAM trong của Wi-Fi/USB. HP nặng RAM hơn Canon nhưng không tăng tuyến tính theo số trang.
- Test native có kiểm đếm từng malloc/calloc/realloc/free, ASan/UBSan: 100 trang
  Canon A4, 100 HP A4, 100 HP Letter, 100 PCL5, 100 PS; kiểm tra sau mỗi trang
  và sau khi kết thúc lệnh, cùng hai trang HP nhiễu khó nén và lỗi thiếu RAM có chủ đích.
- Không có tăng trưởng bộ nhớ được giữ lại trong những bài thử này. Chưa đo heap
  thực tế ESP khi Wi-Fi/USB hoạt động, nên chưa thể khẳng định không bao giờ hết RAM.
- **Trình duyệt khác ESP:** web vẫn giữ Blob của các trang đã render trước khi gửi.
  Tài liệu nhiều ảnh/nhiều trang có thể dùng nhiều RAM trên điện thoại/PC. Nên chia
  lô nếu trình duyệt thiếu RAM. Driver chỉ hỗ trợ khổ giấy cố định trong một lệnh in.

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
- Wi-Fi cấu hình ngay trên web bằng Improv Serial, lưu trong NVS; vẫn có chế độ nhập qua COM.
- Nút BOOT: nhấn để in trang kiểm tra; giữ 2 giây để hủy lệnh đang chạy.
- Timeout khi đọc/ghi CAPT; lỗi phản hồi dừng lệnh thay vì thoát toàn bộ firmware.

LBP2900 không có scanner: thiết bị không quảng bá AirScan. Một số thành phần scanner
của dự án gốc vẫn nằm trong mã nguồn và bộ test hồi quy. In iOS/Mac/Android qua hệ
thống in sẵn có là mục tiêu cần thử trên thiết bị thật, chưa phải chứng nhận AirPrint/Mopria.

## Nạp ngay trên trình duyệt (không cần cài ứng dụng)

Mở **https://phuongtrangpt.github.io/esp32-lbp2900-airprint/** bằng Chrome hoặc Edge
trên máy tính. Trang dùng Web Serial và ESP Web Tools 10.5.2, tự chọn firmware cho
ESP32-S3. Cần đúng board **N16R8**, cáp USB truyền dữ liệu và cổng **UART/COM**.

1. Cắm ESP vào máy tính qua UART/COM, để cổng USB OTG trống cho máy in.
2. Bấm **Kết nối USB & nạp firmware**, chọn cổng của ESP rồi chọn **Install**.
3. Làm theo hộp thoại tiến trình. Nếu chọn xóa dữ liệu, Wi-Fi cũ sẽ bị xóa.
4. Sau khi nạp, chọn mạng Wi-Fi 2.4 GHz và nhập mật khẩu ngay trong hộp thoại.
   Firmware v0.2 dùng Improv Serial; mật khẩu đi từ trình duyệt tới ESP qua USB,
   không được gửi lên GitHub hoặc đưa vào firmware chung.
5. Khi kết nối thành công, hộp thoại cung cấp địa chỉ trang quản lý thiết bị.
   Nối LBP2900 vào USB OTG, cấp điện cho máy in và thử trang kiểm tra.

Không cần Python, Arduino IDE hoặc PlatformIO để nạp theo cách này. Trình duyệt
luôn yêu cầu người dùng chọn cổng USB; trang không thể tự cấp quyền USB ngầm.
Windows thường tự nhận USB–UART, nhưng một số board vẫn cần driver của nhà sản xuất.
Không dùng trình duyệt trong Facebook/Zalo hoặc Safari để nạp. Logs & Console và
cấu hình lại Wi-Fi cũng có trong hộp thoại kết nối.

Mỗi lần push lên main, workflow kiểm tra firmware rồi xuất bản trang cùng các file
`.bin` trên một origin HTTPS. Dữ liệu NVS ở 0x9000–0xefff không nằm trong các phần
nạp, nên có thể giữ Wi-Fi khi không chọn xóa dữ liệu. Ảnh gộp được tạo bằng esptool
để vá đúng header flash, sau đó tách theo các vùng không ghi đè NVS.

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
