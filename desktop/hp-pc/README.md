# Viet Phuong Print Connect 1.0.0

Windows xử lý và nén tài liệu cho HP LaserJet P1006; ESP32-S3 chuyển dữ liệu USB.
Máy in được thêm tự động khi cài đặt với tên **HP LaserJet P1006 - Viet Phuong**.

## Cài đặt

1. Nạp firmware Print Connect cho ESP32-S3 N16R8 tại trang nạp của Việt Phương.
2. Kết nối Wi-Fi cho ESP và ghi lại địa chỉ IP được cấp.
3. Cài Viet Phuong Print Connect trên Windows 10/11 x64, chấp nhận quyền quản trị.
   Bộ cài tự đăng ký Windows Service và thêm máy in bằng Microsoft IPP Class Driver.
4. Mở quản lý một lần, nhập IP ESP, bấm **Lưu IP / bật dịch vụ**.
5. Trong PDF/Word chọn **HP LaserJet P1006 - Viet Phuong** để in. Không cần mở lại
   giao diện mỗi lần in; dịch vụ tự chạy cùng Windows, độc lập phiên đăng nhập.

Máy chưa nhập IP vẫn được Windows nhận diện nhưng chưa nhận lệnh in vào ESP.
Nút **Khôi phục máy in Windows** dùng khi hàng đợi sản phẩm đã bị xóa. Nâng cấp
hỗ trợ đổi tên hàng đợi cũ của sản phẩm sang tên mới; không xóa máy in khác.
Giữ định danh bộ cài cũ để nâng cấp tại chỗ; định danh nội bộ không phải tên hiển thị.

Đóng bằng X để ẩn xuống khay. Thoát giao diện vẫn in được. Đổi IP hoặc dừng dịch
vụ cần quyền quản trị; không đổi IP khi đang nhận hoặc xử lý tài liệu.

## In và trạng thái

Hỗ trợ A4/Letter/A5/A6 dọc, 300/600 dpi, đen trắng, một mặt, một bản; khổ/dpi
đồng nhất trong lệnh. Windows raster hóa PDF/Word trước khi chuyển cho dịch vụ.
Nhận đầy đủ tài liệu vào đĩa một lần, kiểm tra hoàn tất rồi mới xử lý và gửi USB.
Không gửi sang ESP phần đầu của tài liệu HTTP còn thiếu. Tối đa 256 MiB, ba lệnh.

Giao diện phân biệt thời gian nhận dữ liệu từ Windows, xử lý/nén và truyền USB.
Sau khi truyền đủ, máy in có thể vẫn đang in giấy; không coi USB hoàn tất là
xác nhận giấy đã ra hết. Không tự phát lại tài liệu sau lỗi truyền một phần.
Địa chỉ TCP 9101 dành cho LAN tin cậy; chưa TLS/xác thực. IPP PC chỉ nghe loopback.

## Vận hành

Service VietPhuongHPP1006PC dùng LocalService, Automatic; SCM phục hồi host và
host phục hồi worker. Job Object kết thúc worker/converter khi host chết.
Tài liệu tạm ở ProgramData/VietPhuong/HP-P1006-PC/spool có ACL riêng; xóa sau lệnh
hoặc khi worker mới khởi động. Gỡ cài đặt dừng/xóa dịch vụ và hàng đợi sản phẩm.
Cấu hình IP được giữ để cài lại; không phục hồi tự động các lệnh đã in dở.

Firmware 1.0.0-hp-print-connect dùng cùng giao thức với 0.1.1-hp-pc-usbwait;
mạch đã chạy phiên bản đó vẫn tương thích. Không thay đổi firmware chính thức
Canon LBP2900 / HP P1006 hoặc các driver đang có trên trang nạp.

## Kiểm chứng

Người dùng đã xác nhận đủ 30 trang trên HP P1006 qua đường PC/ESP với bản USBWAIT.
Bản phát hành giữ nguyên raster/JBIG/USB của đường đó, đổi tên và thêm tự cài máy in.
CI kiểm tra native C/ASan/UBSan, ACK chờ dài, CRC, hủy, dữ liệu thiếu, Windows IPP
Class Driver, bộ cài thực, tự thêm máy in trước khi có IP, LocalService không GUI,
in lặp, offline/kết nối lại, tự phục hồi và gỡ cài đặt. USB trong CI được mô phỏng.

Nguồn: https://github.com/phuongtrangpt/esp32-lbp2900-airprint/tree/feat/hp-pc-offload
GPL-3.0-or-later; giữ thông báo HP/JBIG và các giấy phép gốc.
