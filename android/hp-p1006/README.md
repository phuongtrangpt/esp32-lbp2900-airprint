# Viet Phuong Print Connect — Android / HP P1006

Bản Android 0.1.0 để kiểm thử, tách riêng bản Windows final 1.0.1. Chưa xác nhận in trên điện thoại + máy HP thật. GPL-3.0-or-later, giữ nguyên các thông báo bản quyền trong bộ nén HP/JBIG của dự án.

## Cài và sử dụng

1. Dùng ESP32-S3 đã nạp firmware Print Connect (bản Windows đang chạy được cũng dùng được; không bắt buộc nạp lại ESP).
2. Cài APK trên điện thoại Android 8.0 trở lên. Đây là APK phát triển ký debug, chưa phát hành trên Google Play; chỉ cấp quyền cài cho trình duyệt/trình quản lý tệp khi Android hỏi. Những bản thử sau có thể cần gỡ bản cũ trước khi cài do khóa debug của môi trường build thay đổi.
3. Mở Viet Phuong Print Connect, nhập IP ESP (ví dụ 192.168.10.21), bấm **Lưu IP và kiểm tra kết nối**.
4. Bấm **Mở cài đặt dịch vụ in Android**, bật **Viet Phuong · HP P1006**. Tên menu khác nhau tùy hãng điện thoại.
5. Trong ứng dụng PDF/ảnh có mục In, chọn **HP P1006 - Viet Phuong**. Chọn A4/Letter/A5/A6, trang cần in, số bản và hướng giấy.
6. Android tự quản lý PrintService. Không cần mở giao diện để in, không chạy dịch vụ vô hạn khi rảnh. Không hỗ trợ ứng dụng chỉ có in Bluetooth độc quyền.

Điện thoại và ESP cần liên lạc được qua Wi-Fi. Dùng firmware Print Connect riêng, không dùng firmware P1006 in trực tiếp với cổng 9100. Bộ nén HP giống byte-for-byte đường Windows với cùng raster; PDF được Android PdfRenderer xử lý 300 dpi, từng trang, một bitmap tối đa khoảng 35 MiB. In một mặt, đen trắng. Chưa có đảo mặt tự động.

## Trạng thái và an toàn lệnh in

- Dịch vụ xác minh ESP nhận đúng P1006, theo dõi trạng thái USB, báo bận hoặc offline; kết nối TCP đơn thuần không được coi là giấy đã in xong.
- Chỉ gửi sau khi nhận đầy đủ PDF và chuyển xong toàn bộ tài liệu. Các trang được chọn theo chỉ số PDF mà Android spooler cung cấp (không tự dịch lại số trang gốc).
- CRC toàn lệnh và từng khối, ACK thứ tự 4096 byte, chờ USB tối đa 150 giây như Windows.
- Mất ACK cuối chỉ hỏi lại kết quả, không truyền lại tài liệu. Android lưu dấu trước khi bắt đầu gửi; lệnh bị gián đoạn không tự chạy lại. Sau lỗi, kiểm tra giấy rồi tạo lệnh mới nếu cần.
- “Đã chuyển USB” chỉ xác nhận dữ liệu, không xác nhận giấy ra đủ. Hủy lệnh vẫn có thể còn giấy trong bộ đệm máy in.
- Không nhận tài liệu từ Internet, không upload tài liệu lên cloud. PDF/XQX dùng bộ nhớ riêng của app và xóa sau lệnh; khi hệ điều hành giết app, Android có thể để lại tệp cache tới lần dọn tiếp theo. Không ghi nội dung tài liệu vào log.

## Kiểm thử trước khi thử máy thật

CI build ARM64, ARMv7 và x86_64. Java transport đối chiếu C protocol thật của ESP với USB giả, các trường hợp bận, lỗi USB, chờ 13 giây, hủy và mất ACK cuối. Adapter C được so byte với Windows renderer. Android emulator API 35 thực thi PdfRenderer/JNI với 40 trang, chọn 5–10, số bản, landscape và A4/A5/A6/Letter. Đây không thay thế xác nhận giấy trên máy thật.

## Build

JDK17, Gradle8.11.1, AGP8.9.2, SDK35, NDK27.0.12077973, CMake3.22.1. Từ thư mục này: `gradle :app:assembleDebug`. CI `.github/workflows/hp-android.yml` tạo APK và chỉ phát hành prerelease khi các kiểm tra đều qua. Không đổi mã driver máy in, dịch vụ Windows hay firmware ổn định.
