# HP RAWFIX2 — 0.9.10-hp-raw12-rawfix2

Dựa trên bản chính thức NET2 (3a9b72b). Chỉ sửa đường RAW 9100 cho HP P1006.
Phần IPP và Canon giữ nguyên. Chưa xác nhận trên máy in thật.

Log thực tế: Windows kết nối tới 9100, trao đổi 295 byte và nhận phản hồi USB,
nhưng chưa bắt đầu XQX. Sau 60 giây firmware hủy và soft-reset HP.

Sửa: TCP receive polling 50 ms thay cho 1 s để chuyển phản hồi USB nhanh hơn;
giữ send timeout 1 s. Phiên chưa bắt đầu XQX hết thời gian sẽ nhả máy in mà
không soft-reset. Lỗi USB thật hoặc timeout sau khi bắt đầu XQX vẫn hủy/reset.

Mô phỏng dùng chính vòng forward_job, client giả lập đợi phản hồi trong 500 ms:
trước sửa phản hồi đầu sau 2000 ms, client không gửi trang; sau sửa 100 ms và
hoàn tất XQX. Đây là mô hình thời gian để kiểm tra tác động thay đổi, chưa phải
đo deadline của driver Windows thật hay bằng chứng nguyên nhân duy nhất.

Nạp: giải nén, mở PowerShell trong thư mục:
  py flash.py --port COM12 --build-dir .
Thay COM12 bằng cổng thực tế. Giữ Wi-Fi đã lưu.

Test Windows dùng cổng RAW 9100 tại IP ESP hiện tại, driver HP P1006.
In một trang trước, rồi in PDF trang 5–10. Tải log kể cả khi vẫn không ra giấy.
Tìm RAW job started, RAW USB reply relayed, RAW XQX page, RAW USB output.

RAWFIX2: log thật của RAWFIX1 vẫn dừng sau 295 byte. Phản hồi nhanh hơn chưa
đủ để xác nhận Windows gửi trang in. Sửa điều kiện nhận diện phiên chưa XQX:
chuỗi UEL ESC%-12345X là lệnh điều khiển bình thường, nhưng trùng đầu marker
XQX; RAWFIX1 đã nhầm prefix này với việc bắt đầu tài liệu và vẫn reset.
Native test nay có UEL và tái hiện được lỗi điều kiện này ở RAWFIX1.
RAWFIX2 ghi tối đa 32 byte HEX của phản hồi USB trong giai đoạn trước XQX để
xác định mã trả lời trạng thái của HP. Không ghi dữ liệu trang/XQX.
Đây là bản chẩn đoán tiếp, chưa xác nhận đã giải quyết được lỗi không in.
