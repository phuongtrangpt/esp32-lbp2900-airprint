HP P1006 RAW3 — ESP32-S3 N16R8

Firmware thử riêng cho HP LaserJet P1006 trên nhánh experiment/hp-p1006-raw-bridge.
Windows gửi dữ liệu do driver HP tạo đến TCP 9100. Điện thoại tiếp tục dùng IPP.
Firmware .dl của HP đã được nhúng. Kết nối RAW chỉ mở ở địa chỉ Wi-Fi của ESP.
Dùng cổng 9100 trong mạng nội bộ tin cậy.

Nạp USB
Giải nén gói, mở Terminal trong thư mục giải nén và chạy:
  py -m pip install esptool==4.9.0
  py flash.py --port COM14 --build-dir .
Thay COM14 bằng cổng ESP32. Muốn quay về test4, nạp lại gói test4 qua USB.

Thêm máy in riêng trên Windows
1. Cài hoặc chọn driver đúng model HP LaserJet P1006.
2. Thêm máy in thủ công. Tạo cổng Standard TCP/IP Port, nhập địa chỉ IP của ESP.
3. Trong cấu hình cổng, chọn RAW, Port Number 9100, bỏ SNMP Status Enabled.
4. Chọn driver HP LaserJet P1006 và đặt tên hàng đợi HP P1006 RAW TEST.
5. In thử một trang từ Word, rồi in tài liệu bốn trang. Đếm tờ thực tế
   và đo đến khi tờ cuối ra. Sau đó in cùng tài liệu từ điện thoại như trước.
   Thử lần lượt vì RAW và IPP dùng chung một máy in.

RAW3 gửi nốt phần dữ liệu đệm qua USB khi TCP im lặng một giây.
ESP giới hạn chờ bộ đệm hoặc lượt truyền USB khoảng 12 giây và vẫn chờ TCP
gửi tiếp tối đa khoảng 30 giây. Khi TCP reset, chỉ hoàn tất USB nếu ESP
đã đọc được XQX END_DOC ở đúng ranh giới record sau ít nhất một trang,
và phần đuôi khớp định dạng đã biết. Reset giữa tài liệu hoặc timeout vẫn
báo lỗi. Log kiểm tra là “RAW TCP reset after XQX end-document”.

Log “RAW USB output flushed” cho biết dữ liệu đã được gửi hết qua USB.
Cần máy in thật để xác nhận driver Windows chạy được qua cổng RAW và đủ tờ.
