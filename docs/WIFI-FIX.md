Sửa đổi Wi-Fi từ trang in – dựa trên 83fac5d (HP RAW12 NET1).

HP: 0.9.10-hp-raw12-net2. Canon: 0.9.11-wifi1.
Chỉ nạp gói đúng model máy in. Phần xử lý in được giữ nguyên từ bản gốc.

Giải nén rồi mở Terminal tại thư mục chứa firmware:
  py -m pip install esptool==4.9.0
  py flash.py --port COM12 --build-dir .
Thay COM12 bằng cổng ESP. Không xóa Wi-Fi cũ trước khi nạp.

Sau nạp: vào trang in qua Wi-Fi đang dùng, chọn Đổi Wi-Fi.
Trang tự quét các mạng 2.4 GHz; có thể bấm Quét lại hoặc nhập tên thủ công.
Chọn mạng mới, nhập mật khẩu và bấm Kết nối Wi-Fi. Không cần giữ BOOT khi ESP đang kết nối mạng.
Không đổi mạng khi đang in/cập nhật firmware.

Khi đổi mạng, trình duyệt có thể mất kết nối với địa chỉ IP cũ. Chuyển máy tính/điện thoại sang mạng mới, tìm IP ESP trên router rồi mở lại trang in.
Trong khi chuyển, ESP mở hotspot vietphuong esp-XXXX để có thể kiểm tra ở http://192.168.4.1/setup.
Chỉ lưu Wi-Fi/IP mới sau khi kết nối thành công. Nếu sai mật khẩu hoặc mạng không khả dụng, ESP thử quay lại Wi-Fi cũ.
Giữ BOOT 5 giây vẫn dùng được để phục hồi khi không truy cập được ESP qua Wi-Fi.

Đã kiểm tra giao diện và logic qua mô phỏng, biên dịch firmware; chưa xác nhận trên radio ESP thật.
