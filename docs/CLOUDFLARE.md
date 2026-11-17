# vietphuong esp — Cloudflare Pages miễn phí

Mã nguồn giữ trên GitHub. Actions kiểm tra/build HP + Canon, tạo ZIP và web assets,
chỉ xuất bản khi build thành công. GitHub Pages hiện tại vẫn là địa chỉ dự phòng.
Không cần VPS hoặc máy tính chạy thường trực. Website/firmware không chứa dữ liệu
bản in của khách; in và thiết lập Wi-Fi diễn ra trong mạng cục bộ của thiết bị.

Kết nối Cloudflare của chủ dự án (chỉ cần một lần):

1. Đăng nhập Cloudflare → Workers & Pages → tạo Pages project tên `vietphuong-esp`
   theo kiểu Direct Upload. Chưa có tài khoản thì tạo gói Free; dùng *.pages.dev
   hoặc gắn tên miền riêng. Không tạo public URL giả trước khi Cloudflare xác nhận.
2. Trong GitHub repository Settings → Secrets and variables → Actions, thêm:
   - `CLOUDFLARE_ACCOUNT_ID`: Account ID tài khoản đã chọn.
   - `CLOUDFLARE_API_TOKEN`: token giới hạn Account / Cloudflare Pages / Edit của
     đúng tài khoản; nhập trực tiếp vào Secrets, không đưa vào mã nguồn/chat.
3. Workflow cloudflare-pages.yml nhận web artifact của build thành công trên main
   và deploy. Khi chưa có Secrets, workflow chỉ báo chưa kết nối, không giả báo deploy.
4. Kiểm tra URL Cloudflare thực tế và các web-manifest/ota-manifest của cả HP/Canon.
   Muốn firmware lấy OTA từ Cloudflare thay GitHub: đặt repository Actions Variable
   `FIRMWARE_UPDATE_BASE_URL` thành HTTPS URL gốc thực tế, có dấu `/` cuối.
   Build/nạp bản chuyển tiếp lên thiết bị trước khi ngừng origin OTA cũ.

Mỗi file hiện nhỏ hơn giới hạn Pages 25 MiB. Khi nâng cấp tương lai cần kiểm tra
lại gói Free/quota. Không có service bảo mật nào đòi khách cung cấp token Cloudflare.
Website chỉ bỏ nút GitHub nổi bật; mục Giấy phép vẫn cung cấp mã nguồn GPL tương ứng.
