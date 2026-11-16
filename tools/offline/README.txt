ESP 32 PRINT SERVER - BO NAP OFFLINE WINDOWS x64

1. Giai nen TOAN BO ZIP vao thu muc co quyen ghi.
2. Chay START.cmd, giu cua so nay mo den khi nap xong.
3. Chrome/Edge: chon HP P1006 hoac Canon LBP2900, cam UART/COM, chon cong va nap.
4. Nhap Wi-Fi 2.4 GHz qua USB; sau do noi may in vao USB OTG cua ESP.
5. Trang ESP: /network cai IP dong/tinh; /diagnostics tai log loi.

Khong can cai Python, Arduino IDE hay runtime. Board ESP32-S3 N16R8.
Mot so board van can driver USB-UART CH340/CP210x cua nha san xuat.

HP P1006: LAN DAU can Internet de launcher tu tai firmware HP, kiem tra SHA256
va luu cache. Nhung lan sau mo va nap offline. ZIP cong khai khong chua blob HP
co giay phep rieng. ESP cung co the tu tai/cache khi in lan dau neu chua co file.
File HP da luu tren ESP duoc giu neu KHONG chon Erase device.
START_CANON.cmd bo qua buoc chuan bi HP.

Ten: HP LaserJet P1006 ESP 32 / Canon LBP2900 ESP 32. Moi ban chi ho tro 1 model.
Dat IP tinh ngay tren ESP tai /network; khong can cau hinh router.
Neu dat sai IP: doi ESP khoi dong xong roi giu BOOT 10 giay de ve DHCP,
giu nguyen Wi-Fi va firmware HP. Khong giu BOOT trong luc cap nguon.
Nhan BOOT ngan de in test; giu 2 giay de huy lenh in.
May tinh nay chi can de nap, ESP tu xu ly in sau khi cau hinh.
Ma nguon tuong ung va giay phep di kem; giu chung khi chia se.
