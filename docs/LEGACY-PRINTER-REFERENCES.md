# Máy in USB đời cũ trên ESP32-S3

Mỗi bản thử nghiệm có profile riêng. Chuỗi dữ liệu in có thể được dùng lại khi tài liệu upstream khớp. Mỗi model vẫn cần xác minh nhận diện USB, khởi động nguội và in lặp trên phần cứng.

| Model | Mã nguồn tham khảo | Phần áp dụng hoặc cần đối chiếu |
|---|---|---|
| HP LaserJet P1008 | [OpenPrinting/foo2zjs](https://github.com/OpenPrinting/foo2zjs/tree/80499ed5bf6caa2963ad337e37cfda78a80aab1e) | PPD P1008 và P1006 chỉ khác tên model. `hplj1000` gán P1008 mã USB `03f0:4917` và dùng file khởi động P1006. Profile `hp_p1008` dùng chung bộ mã XQX/JBIG nhưng nhận đúng USB ID và có định danh ứng dụng riêng. Bản thử dùng A4, Letter và A5 theo PPD. |
| HP LaserJet P1005/P1007 và P1505 | [OpenPrinting/foo2zjs](https://github.com/OpenPrinting/foo2zjs) | P1007 dùng file khởi động của P1005. P1505 dùng file riêng. Cần đối chiếu lựa chọn độ phân giải và các tham số XQX trong PPD từng model trước khi tái sử dụng bộ mã hóa. |
| HP LaserJet 1018/1020 | [OpenPrinting/foo2zjs](https://github.com/OpenPrinting/foo2zjs) | Chuỗi in ZjStream và file khởi động riêng. |
| Brother HL-1201/HL-1111/HL-2240D | [Owl-Maintain/brlaser](https://github.com/Owl-Maintain/brlaser/tree/edf2770c4573973d5b90ce57ec407e675348c196) | [Brother xếp HL-1201 cùng nhóm HL-1200](https://support.brother.com/g/b/spec.aspx?c=vn&lang=en&prod=hl1201_as). Driver CUPS có họ HL-1200, HL-1110 và HL-2240D. Dùng `src/line.cc`, `src/block.h` và `src/job.cc` cùng các bài kiểm thử upstream để đối chiếu bộ nén từng dòng và đóng gói lệnh. Xác minh tên USB và lệnh bắt đầu/kết thúc trang trên đúng model. |
| Canon LBP3010/3018/3050 | [mounaiban/captdriver](https://github.com/mounaiban/captdriver) | Nhóm CAPT USB `04a9:26da`. Mã CAPT hiện tại đã có `lbp3010_job_prologue` và `lbp3010_cancel_cleanup` trong `components/capt/vendor/prn_lbp2900.c`. Đối chiếu các hàm này với nguồn tham khảo và kiểm thử chuỗi điều khiển, báo lỗi và hủy lệnh trên LBP3050. |

## Kiểm tra P1008

```sh
make -C host hp_p1008_host
ASAN_OPTIONS=detect_leaks=0 python tools/test_hp_reference.py /path/to/foo2xqx host/hp_p1008_host --paper-count 3
pio run -e hp_p1008
python tools/package_firmware.py --env hp_p1008 --out firmware/hp_p1008
```

Kiểm thử phần cứng cần dùng ESP32-S3 N16R8 và HP LaserJet P1008, khởi động máy in nguội để kiểm tra bước nạp firmware HP, sau đó đo thời gian từ lúc nhận lệnh A4 đến khi tờ đầu ra máy. Gửi ba lệnh đánh dấu A, B, C từ ba thiết bị, kiểm tra đủ trang và giữ thứ tự nhận lệnh. Lặp lại sau khi hết giấy, rút cáp USB và bật lại máy in để kiểm tra việc khởi động và tránh in trùng.
