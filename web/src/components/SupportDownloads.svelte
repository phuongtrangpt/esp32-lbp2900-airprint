<script lang="ts">
  let { firmware }: { firmware: string } = $props();
  const supported = $derived(firmware.includes('hp-print-connect') || firmware.includes('hp-pc-'));
</script>

{#if supported}
  <details class="support-downloads">
    <summary>Tải phần mềm hỗ trợ · Windows / Android</summary>
    <p>Cài Print Connect trên máy tính hoặc điện thoại để xử lý tài liệu cho HP P1006. Nhập địa chỉ IP của ESP sau khi cài; thiết bị cần ở cùng mạng.</p>
    <div class="download-platforms">
      <a href="https://esp.vietphuong.io.vn/print-connect/" target="_blank" rel="noopener noreferrer"><strong>Windows</strong><span>Tải bộ cài và hướng dẫn</span></a>
      <a href="https://esp.vietphuong.io.vn/print-connect/android/" target="_blank" rel="noopener noreferrer"><strong>Android</strong><span>Tải APK và bật dịch vụ in</span></a>
    </div>
    <p class="hint">Chỉ mở trang tải khi bạn bấm nút. Tài liệu in được xử lý trên thiết bị của bạn.</p>
  </details>
{/if}

<style>
  .support-downloads { margin-top: 22px; border: 1px solid #50667f; border-radius: 16px; background: linear-gradient(135deg,#1f3048,#132235); padding: 18px 20px; box-shadow: 0 8px 24px #0003; color: #eaf2ff; font-family: Arial, sans-serif; }
  summary { font-size: 16px; font-weight: 700; cursor: pointer; line-height: 1.6; }
  p { font-size: 15px; color: #b7c8df; line-height: 1.7; }
  .download-platforms { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
  a { text-decoration: none; border-radius: 12px; border: 1px solid #a8c978; background: #c5f36b; color: #192518; padding: 17px; display: flex; flex-direction: column; gap: 6px; }
  strong { font-size: 18px; } a span { font-size: 14px; } a:focus-visible { outline: 3px solid #8bbcff; outline-offset: 3px; }
  .hint { margin-bottom: 0; font-size: 13px; }
  @media(max-width:520px) { .download-platforms { grid-template-columns: 1fr; } }
</style>
