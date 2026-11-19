> Canon LBP6030: bản USB **thử nghiệm riêng**, chưa kiểm thử máy thật. Xem [ghi chú và cách nạp](docs/LBP6030.md). Không dùng bản LBP2900 thay thế.

## v0.9.0 — Chọn firmware theo model USB

| Bản | Model / điều kiện | Trạng thái |
|---|---|---|
| hp | HP LaserJet P1006 | Bản riêng, có file HP nhúng |
| canon | Canon LBP2900 | CAPT thử nghiệm |
| pixma | 23 dòng Canon PIXMA trong danh sách trên web | Driver có sẵn; bản ghép mới chưa thử giấy thật |
| standard | HP 1320, P2015, P2055 là ứng viên; USB phải khai báo PCL5 hoặc PostScript | Thử nghiệm, in một mặt đen trắng |

Trang web có tra cứu model và danh sách dòng đầy đủ. Không hỗ trợ UFR II/UFR II LT,
GDI hoặc máy chỉ có PCL6/PCL XL. Model không nằm trong danh sách: chưa xác nhận.
Máy có Wi-Fi hoạt động sẵn không cần thêm ESP. Mỗi bản có nhận diện OTA riêng;
chuyển nhóm máy bằng USB, không cài chéo profile qua OTA.

HP P1006 printer bootstrap is embedded in HP firmware (222891 bytes, hash verified). First print and OTA recovery require no Internet on the ESP. Canon builds exclude the HP binary. Generated payload: tools/prepare_hp_firmware.py.

## 0.8.2-vietphuong · vietphuong esp

Brand and support: https://www.facebook.com/leviethoaiphuong.
Setup AP `vietphuong esp-XXXX`, open network, no password; first boot
without saved Wi-Fi opens setup. Captive DNS responds only on AP address
192.168.4.1; fallback browser URL http://192.168.4.1/setup. Phones do not need USB.
Hold BOOT 5 seconds after boot for maintenance/setup; hold 10 seconds for DHCP
recovery and setup after restart. Saved devices require a physical maintenance
window before changing Wi-Fi through the portal. Credentials save after GOT_IP;
invalid attempts keep previous saved settings and allow retry. AP closes 30 seconds
after successful setup. The setup AP is open while active; saved devices retain the physical maintenance requirement.
Handover at /setup clears saved Wi-Fi/IP only, preserves HP firmware, then reboots
when idle. Always use credential-free public builds for customer devices.
/update supports user-triggered HTTPS version checks and OTA with progress.
Install requires BOOT confirmation within 120 seconds; no updates while printing
or provisioning. New jobs are refused while update/provisioning is reserved.
SHA-256, exact byte count, chip image validation and app project_name/profile/version
checks precede boot-slot selection. Only the inactive app partition is written;
NVS/HP data remain. Existing bootloader rollback checks Wi-Fi health after reboot.
TLS trust and SHA-256 are used; no new device secure-boot/signing-key system is claimed.
Cloud manifests/images are static and default to the existing free GitHub Pages
origin. Configure FIRMWARE_UPDATE_BASE_URL when moving firmware hosting to Cloudflare.
Source remains on GitHub and is linked under Licenses, with matching source in ZIP.
Native/CI checks are used; captive popup, Wi-Fi/AP radio and OTA power-cut/recovery
remain pending physical validation. No board attached; QEMU unavailable locally.

# ESP32-S3 AirPrint cho Canon LBP2900 và HP P1006

Print server Wi-Fi nhỏ gọn cho Canon LBP2900 và HP LaserJet P1006, phát triển từ
[ngxson/esp32-usb-to-airprint](https://github.com/ngxson/esp32-usb-to-airprint).
Driver CAPT/Hi-SCoA được chuyển từ [ValdikSS/captdriver, nhánh val](https://github.com/ValdikSS/captdriver/tree/val).

## v0.8.2: hai firmware riêng, có IP tĩnh

- **HP LaserJet P1006 ESP 32**: chỉ HP P1006 (`03f0:3e17`), XQX/JBIG 600 dpi.
- **Canon LBP2900 ESP 32**: chỉ Canon LBP2900 (`04a9:2676`), CAPT/Hi-SCoA 600 dpi.
- Mỗi ESP dùng một máy in; A4/Letter, đen trắng, một mặt. Bản HP không chứa driver Canon/PCL5/PS trong registry, và ngược lại. Mã driver thử nghiệm khác vẫn được giữ trong profile `all` cho phát triển.
- HP v0.6 đã được người dùng xác nhận in trang kiểm tra A4 và Letter trên máy thật. Chức năng mạng/tách profile v0.7 cần thử trên thiết bị; Canon chưa xác nhận in thật.

### Cài IP tĩnh ngay trên ESP

1. Nạp firmware, nhập Wi-Fi bằng USB; lần đầu mặc định **DHCP**.
2. Mở trang ESP → **Cài đặt IP động / IP tĩnh**, hoặc `http://<IP-ESP>/network`.
3. Chọn **IP tĩnh**, nhập IP, subnet mask, gateway và DNS rồi lưu. Trang điền sẵn IP/mask/gateway hiện tại để dễ sửa. ESP lưu trong NVS và khởi động lại khi không có lệnh in.
4. Chọn địa chỉ chưa dùng và tránh vùng DHCP đang cấp cho thiết bị khác. Ví dụ IP hiện tại `192.168.10.36` chỉ nên giữ cố định nếu chắc chắn không bị cấp cho thiết bị khác; không tự đoán gateway của mạng.
5. Nếu đặt sai: **đợi ESP khởi động xong, giữ BOOT 10 giây** để trở lại DHCP. Thao tác này giữ Wi-Fi và file HP; đừng giữ BOOT khi vừa cấp nguồn vì đó là chế độ nạp.

Word không yêu cầu IP tĩnh. Nếu Windows đã thêm hàng đợi bằng URL chứa IP, IP thay đổi sẽ làm hàng đợi đó mất kết nối; IP tĩnh trên ESP giúp tránh việc phải thêm lại. Với Windows đã thêm IPP trước đây, có thể cần đổi tên/xóa hàng đợi cũ để thấy tên mới. URL IPP: `http://<IP-ESP>:631/ipp/print`; dùng Microsoft IPP Class Driver, không gửi dữ liệu driver USB CAPT/XQX vào đường IPP raster.

### HP P1006 tự chuẩn bị firmware máy in

P1006 cần firmware khởi động HP sau mỗi lần bật nguồn. Firmware ESP khác firmware HP.
Từ v0.8.2, dữ liệu HPDL 222891 byte được nhúng sẵn trong ứng dụng HP.
Nếu cache chưa có hoặc sai CRC, ESP kiểm tra SHA-256 dữ liệu nhúng rồi tự khôi phục
phân vùng HP. Không tải Internet ở lần in đầu, không cần tải file thủ công.
OTA cũng mang theo dữ liệu này. Bản Canon không nhúng HPDL.

Máy build lấy nguyên dữ liệu từ upstream bằng tools/prepare_hp_firmware.py;
checksum được cố định. Dữ liệu HP có điều khoản riêng, không cấp lại GPL.
Trang /hp-firmware vẫn dùng được khi cần file riêng.

### Nạp offline Windows, không cài ứng dụng

[Tải ZIP v0.8.2](https://phuongtrangpt.github.io/esp32-lbp2900-airprint/downloads/ESP32-PrintServer-Offline-v0.8.2.zip).
Giải nén toàn bộ vào thư mục có quyền ghi, chạy **START.cmd**, mở bằng Chrome/Edge.
Chọn đúng HP/Canon, cắm UART/COM, chọn cổng và nạp theo tiến trình. Không cần Python,
Arduino IDE hay Internet cho trang nạp; **bản HP đã có sẵn dữ liệu máy in**.
Giữ cửa sổ launcher mở đến khi nạp/cấu hình xong. `START_CANON.cmd` bỏ bước tải HP.
Launcher chỉ lắng nghe loopback; mã nguồn và giấy phép đi kèm ZIP. Windows x64.
USB-UART trên một số board vẫn cần driver CH340/CP210x; browser luôn yêu cầu chọn cổng.

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

## Nhật ký tự động (v0.5.0-diagnostics-preview)

Không phải bật debug hay cài phần mềm đọc COM. Firmware tự thu log từ lúc ứng dụng
khởi động, kể cả `MODE=normal`. Sau khi nạp firmware và cấu hình Wi-Fi:

1. Vào trang quản trị của ESP, bấm **Nhật ký / Tải log lỗi**.
2. Hoặc mở `http://<IP-ESP>/diagnostics` trên điện thoại/máy tính.
3. Khi in lỗi, bấm **Tải báo cáo lỗi (.txt)** và gửi file `printserver-log.txt`.
   Đường tải trực tiếp: `http://<IP-ESP>/diagnostics.txt`.
4. Nếu ESP từng crash và ghi được core dump, có thể tải thêm ở
   `http://<IP-ESP>/diagnostics/coredump` (không có dump thì trả 404).

Báo cáo gồm version/build firmware, VID:PID/model USB, driver, trạng thái bận,
RAM/PSRAM còn trống/thấp nhất/khối lớn nhất, nguyên nhân reset và log gần nhất.
CAPT, timeout USB, bước nạp firmware HP và RAM đầu/cuối lệnh in được gom chung.
Không ghi dữ liệu raster hay mật khẩu Wi-Fi vào báo cáo. Log có thể có tên lệnh
in/người dùng do IPP gửi; core dump là dữ liệu kỹ thuật bộ nhớ, chỉ chia sẻ khi cần.

Bộ đệm vòng **64 KB trong PSRAM**, có fallback 8 KB khi không cấp phát được; không
phình theo số trang. Phần cũ được ghi đè. Đuôi log 3 KB trong RTC có thể còn sau
reset/watchdog, **không giữ log RAM khi mất nguồn**: hãy tải trước khi rút điện.
Crash dump nằm riêng trong flash nếu ESP ghi được trước khi khởi động lại.
Trang chẩn đoán chỉ đọc; không tự bật OTA/reboot/panic của chế độ debug.

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
- Nút BOOT: nhấn để in trang kiểm tra; giữ 2 giây để hủy lệnh đang chạy, giữ 10 giây khi đã khởi động để về DHCP.
- Timeout khi đọc/ghi CAPT; lỗi phản hồi dừng lệnh thay vì thoát toàn bộ firmware.

LBP2900 không có scanner: thiết bị không quảng bá AirScan. Một số thành phần scanner
của dự án gốc vẫn nằm trong mã nguồn và bộ test hồi quy. In iOS/Mac/Android qua hệ
thống in sẵn có là mục tiêu cần thử trên thiết bị thật, chưa phải chứng nhận AirPrint/Mopria.

## Nạp ngay trên trình duyệt (không cần cài ứng dụng)

Mở **https://phuongtrangpt.github.io/esp32-lbp2900-airprint/** bằng Chrome hoặc Edge
trên máy tính. Trang dùng Web Serial và ESP Web Tools 10.5.2, tự chọn firmware cho
ESP32-S3. Cần đúng board **N16R8**, cáp USB truyền dữ liệu và cổng **UART/COM**.

1. Cắm ESP vào máy tính qua UART/COM, để cổng USB OTG trống cho máy in.
2. Chọn **HP P1006** hoặc **Canon LBP2900**, bấm **Kết nối USB & nạp firmware**, chọn cổng của ESP rồi chọn **Install**.
3. Làm theo hộp thoại tiến trình. Nếu chọn xóa dữ liệu, Wi-Fi cũ sẽ bị xóa.
4. Sau khi nạp, chọn mạng Wi-Fi 2.4 GHz và nhập mật khẩu ngay trong hộp thoại.
   Firmware v0.2 dùng Improv Serial; mật khẩu đi từ trình duyệt tới ESP qua USB,
   không được gửi lên GitHub hoặc đưa vào firmware chung.
5. Khi kết nối thành công, hộp thoại cung cấp địa chỉ trang quản lý thiết bị.
   Nối đúng máy in đã chọn vào USB OTG, cấp điện cho máy in và thử trang kiểm tra.

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
6. Nối máy in vào USB OTG. Truy cập `http://hp-p1006-esp32.local/`, `http://canon-lbp2900-esp32.local/` hoặc IP in trên COM.
7. In trang kiểm tra bằng BOOT, rồi thử một trang PDF và hai trang liên tiếp.

Nếu đổi Wi-Fi, dùng `python tools/flash.py --port COM5 --reset-wifi` để xóa vùng NVS
`0x9000..0xefff`; thao tác này xóa thiết lập Wi-Fi của board. Sau reset, nhập lại qua COM.
Không gửi mật khẩu, `.env`, `src/local_config.h` hoặc ảnh chụp có mật khẩu lên GitHub.

## Tự build

Cài Python 3, Node.js 24 LTS và Git. Trong thư mục dự án:

```sh
python -m pip install platformio==6.2.0
pio run -e hp
python tools/package_firmware.py --env hp --out firmware/hp
# Hoặc: pio run -e canon; đóng gói --env canon --out firmware/canon
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
