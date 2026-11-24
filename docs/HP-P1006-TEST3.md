HP P1006 – 0.9.10-hp-test3

Gói nạp USB dành cho ESP32-S3 N16R8 kết nối HP LaserJet P1006.
Bản này gom xử lý JBIG theo byte để giảm số lần gọi bộ mã hóa trên mỗi trang.
Bộ kiểm thử so sánh từng byte XQX với foo2xqx cho A4, Letter, A5 và A6.

Giải nén gói, mở Terminal trong thư mục giải nén rồi chạy:
  py -m pip install esptool==4.9.0
  py flash.py --port COM14 --build-dir .
Thay COM14 bằng cổng ESP32 thực tế và đóng trình theo dõi COM trước khi nạp.

Để đo tốc độ, in lại cùng tài liệu 3 trang ở 600 dpi, cùng trình duyệt và cài đặt.
Lấy log có dòng “HP P1006 expand=… JBIG+output=…” và tổng thời gian lệnh.
Mốc bản test1 trên tài liệu báo lỗi là 53,201 giây JBIG+output và 62,8 giây tổng cộng.

Sau khi tắt bật HP, kiểm tra log có “HP P1006 firmware ready (FWVER confirmed)”
trước khi raster bắt đầu. Thử một lệnh Word và một lệnh PDF từ điện thoại.
Ghi số tờ thật. Nếu Word báo truncated ở 524288 byte, thử xuất PDF rồi in từ
trang in. Bản này đóng phiên in bị cắt, dữ liệu Word gửi thiếu vẫn báo lỗi.
