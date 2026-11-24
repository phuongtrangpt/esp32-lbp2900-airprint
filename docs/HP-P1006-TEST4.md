HP P1006 – 0.9.10-hp-test4

Gói nạp USB cho ESP32-S3 N16R8 kết nối HP LaserJet P1006.
Firmware .dl của HP đã nhúng trong firmware.bin.
Giải nén rồi chạy py flash.py --port COM14 --build-dir .
Thay COM14 bằng cổng ESP32 thực tế.

Thiết bị công bố đầu vào 300 dpi. Luồng 600 dpi cũ vẫn được nhận.
Dữ liệu USB giữ định dạng XQX đã kiểm thử. ESP chờ USB truyền xong
sau mỗi trang và sau khi đóng lệnh. Cần đếm tờ thật để xác nhận trang cuối.
Nếu Windows kết thúc luồng HTTP trước khi gửi hết tài liệu, ESP vẫn
báo lỗi và đóng phần đã gửi. Bản này chưa xác định nguyên nhân phía Windows.

Thử tài liệu Word và điện thoại trước đây, ghi số tờ thật rồi gửi log.
Kiểm tra dòng A4 300 dpi và FWVER confirmed.
