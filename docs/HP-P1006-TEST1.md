HP P1006 – 0.9.10-hp-test1

Bản kiểm tra riêng HP P1006 trên ESP32-S3 N16R8.
Không cập nhật bản Canon trên website. Không nạp gói này cho máy Canon.

Thay đổi:
- Tối ưu chuyển bitmap 1 bit sang dữ liệu 2 bit của P1006, giữ nguyên dữ liệu gửi máy in.
- Profile P1006 không còn tự hủy USB sau 15 giây chờ máy in nhận dữ liệu. Vẫn kiểm tra hủy lệnh và mất kết nối, ghi log khi chờ lâu.
- Thêm thời gian mở rộng bitmap và JBIG/output vào log để xác định phần chậm trên ESP thật.

Chưa kiểm thử trên máy in thật. Kiểm tra mô phỏng không chứng minh lỗi dừng thực tế đã hết.

Nạp USB: giải nén gói, mở Terminal trong thư mục đã giải nén:
  py -m pip install esptool==4.9.0
  py flash.py --port COM14 --build-dir .
Thay COM14 bằng cổng thực tế. Đóng trình theo dõi COM trước khi nạp.
Không dùng tùy chọn reset Wi-Fi nếu muốn giữ cấu hình hiện có.

Test lần lượt một trang, 5 trang, rồi lặp lại ít nhất 3 lần.
Gửi log từ lúc cắm HP đến khi lệnh in kết thúc hoặc bị dừng; ghi số trang gửi/in được và thời điểm bắt đầu/dừng.
