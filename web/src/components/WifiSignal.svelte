<script lang="ts">
  import { onMount } from 'svelte';
  import { device } from '../lib/device.svelte';
  import { wifiSignal } from '../lib/wifi-signal';
  let connected = $state<boolean | null>(null);
  let rssi = $state<unknown>(null);
  let failed = $state(false);
  const signal = $derived(wifiSignal(connected, rssi));
  const printing = $derived(device.printer?.state === 'processing');
  onMount(() => {
    let stopped = false;
    let pending = false;
    let timer: ReturnType<typeof setTimeout>;
    let controller: AbortController | undefined;
    async function poll() {
      if (stopped || pending) return;
      clearTimeout(timer);
      // Leave scarce HTTP worker RAM available to print uploads.
      if (!document.hidden && device.printer?.state !== 'processing') {
        pending = true;
        controller = new AbortController();
        const timeout = setTimeout(() => controller?.abort(), 4000);
        try {
          const response = await fetch('/api/wifi-signal', { cache: 'no-store', signal: controller.signal });
          if (!response.ok) throw new Error('Signal unavailable');
          const data = await response.json();
          if (!stopped) {
            connected = typeof data.connected === 'boolean' ? data.connected : null;
            rssi = data.rssi;
            failed = false;
          }
        } catch {
          if (!stopped) { connected = null; rssi = null; failed = true; }
        } finally { clearTimeout(timeout); pending = false; }
      }
      if (!stopped) timer = setTimeout(poll, 15000);
    }
    const visible = () => { if (!document.hidden) void poll(); };
    document.addEventListener('visibilitychange', visible);
    void poll();
    return () => { stopped = true; clearTimeout(timer); controller?.abort(); document.removeEventListener('visibilitychange', visible); };
  });
</script>

<section class="wifi-signal" class:weak={signal.weak} aria-label="Chất lượng sóng Wi-Fi">
  <div class="summary">
    <span class="bars" aria-hidden="true">
      {#each [1, 2, 3, 4] as bar}<span class:active={bar <= signal.bars} style:height={6 + bar * 5 + 'px'}></span>{/each}
    </span>
    <strong>{signal.label}</strong>
    {#if signal.rssi !== null}<span class="reading">{signal.rssi} dBm · {signal.bars}/4 vạch</span>{/if}
  </div>
  {#if signal.weak}<p>Sóng yếu có thể làm nhận lệnh in chậm hoặc gián đoạn. Hãy đặt thiết bị gần bộ phát Wi-Fi hơn.</p>{/if}
  {#if printing && signal.rssi !== null}<p class="detail">Số đo trước khi in · Tự cập nhật sau khi in xong.</p>
  {:else if failed}<p class="detail">Chưa lấy được số đo từ ESP. Mức sóng sẽ được thử đọc lại tự động.</p>{/if}
</section>

<style>
  .wifi-signal { padding: 12px 16px; border: 1px solid var(--border); border-radius: 12px; background: var(--surface); margin: 0 0 16px; }
  .summary { display: flex; align-items: center; flex-wrap: wrap; gap: 10px; }
  .bars { display: inline-flex; gap: 3px; align-items: flex-end; height: 28px; flex: none; }
  .bars span { display: block; width: 6px; border: 1px solid var(--muted); border-radius: 2px; background: transparent; }
  .bars span.active { background: var(--text); border-color: var(--text); }
  .weak { border-color: #e8b84b; }
  .weak .bars span.active { background: #e8b84b; border-color: #e8b84b; }
  .reading, p { font-size: 0.9rem; }
  p { margin: 8px 0 0; line-height: 1.5; }
  .detail { color: var(--muted); }
</style>
