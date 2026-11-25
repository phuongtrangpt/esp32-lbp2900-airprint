<script lang="ts">
  import WifiSignal from '../components/WifiSignal.svelte';
  import Printer from '@lucide/svelte/icons/printer';
  import Upload from '@lucide/svelte/icons/upload';
  import FolderOpen from '@lucide/svelte/icons/folder-open';
  import Lightbulb from '@lucide/svelte/icons/lightbulb';
  import CircleStop from '@lucide/svelte/icons/circle-stop';
  import CircleAlert from '@lucide/svelte/icons/circle-alert';
  import { device, type Job, type Supply } from '../lib/device.svelte';
  import { cancelJob, identifyPrinter, statusOk, statusText } from '../lib/ipp';
  import { addFile, formatSize, isSupported } from '../lib/files.svelte';
  import { openPrint, pickFiles, toast } from '../lib/ui.svelte';

  const p = $derived(device.printer);
  const active = $derived(p?.jobs.find((j) => j.id === p.active && (j.state === 'processing' || j.state === 'pending')));
  let input: HTMLInputElement;
  const status = $derived.by(() => {
    if (!device.loaded) return { text: 'Đang kết nối…', tone: '' };
    if (!p) return { text: 'Chưa sẵn sàng', tone: 'err' };
    if (p.reasons.some((r) => r.endsWith('-error'))) return { text: p.message, tone: 'err' };
    if (!p.connected) return { text: 'Chưa cắm máy in', tone: 'warn' };
    if (p.state === 'processing') return { text: 'Đang in', tone: 'busy' };
    if (device.scan?.state === 'scanning') return { text: 'Scanning', tone: 'busy' };
    return { text: 'Sẵn sàng', tone: 'ok' };
  });

  const REASONS: Record<string, string> = {
    'marker-supply-low-warning': 'Ink is running low',
    'marker-supply-empty-error': 'An ink cartridge is empty or missing',
    'media-empty-error': 'Out of paper: load paper in the rear tray',
    'media-jam-error': 'Paper jam',
    'cover-open-error': 'The cover is open',
    'offline-report': 'The printer is not connected',
    'other-error': 'Máy in đang chờ khôi phục',
  };

  function reasonText(r: string): string {
    return REASONS[r] || r.replace(/-(error|warning|report)$/, '').replace(/-/g, ' ');
  }

  function inkColor(s: Supply): string {
    const cs = s.color.match(/#[0-9a-f]{6}/gi) || ['#888'];
    return cs.length > 1 ? `linear-gradient(90deg, ${cs.join(', ')})` : cs[0];
  }

  const STATE: Record<Job['state'], [string, string]> = {
    pending: ['Đang chờ', 'busy'],
    held: ['Held', 'warn'],
    processing: ['Đang in', 'busy'],
    completed: ['Hoàn tất', 'ok'],
    canceled: ['Đã hủy', ''],
    aborted: ['Thất bại', 'err'],
  };

  // the device's clock is set over the network; without it, no time
  function ago(t: number): string {
    if (t < 1.6e9) return '';
    const s = Math.max(0, Date.now() / 1000 - t);
    if (s < 60) return 'just now';
    if (s < 3600) return `${Math.round(s / 60)} min ago`;
    if (s < 86400) return `${Math.round(s / 3600)} h ago`;
    return `${Math.round(s / 86400)} d ago`;
  }

  async function chooseFile() {
    const [f] = await pickFiles('Choose a file to print', false);
    if (f) openPrint(f);
  }

  async function upload(e: Event) {
    const list = (e.target as HTMLInputElement).files;
    const file = list?.[0];
    (e.target as HTMLInputElement).value = '';
    if (!file) return;
    if (!isSupported(file.type, file.name)) return toast('Choose a PDF or an image', 'error');
    try {
      const f = await addFile({ name: file.name, blob: file, source: 'upload' });
      openPrint(f);
    } catch (err) {
      toast(`Cannot open ${file.name}: ${(err as Error).message}`, 'error');
    }
  }

  async function identify() {
    try {
      await identifyPrinter();
      toast('The light on the printer box flashes blue', 'ok');
    } catch (e) {
      toast(`Failed: ${(e as Error).message}`, 'error');
    }
  }

  async function cancel(id: number) {
    try {
      const r = await cancelJob(id);
      if (!statusOk(r.status)) throw new Error(statusText(r));
      toast('Đã gửi yêu cầu hủy lệnh in');
    } catch (e) {
      toast(`Cannot cancel: ${(e as Error).message}`, 'error');
    }
  }
</script>

<div class="intro"><span>VIETPHUONG ESP / PRINT STUDIO</span><h2>In ấn thật đơn giản.</h2><p class="muted">Tài liệu của bạn. Kết nối không dây. Sẵn sàng trên mọi thiết bị.</p></div>
<WifiSignal />
<div class="grid">
  <section class="card hero">
    <div class="art"><Printer size={40} strokeWidth={1.5} /></div>
    <div class="grow">
      <div class="row wrap">
        <h1 class="ellipsis">{p?.name || 'Printer'}</h1>
        <span class="pill {status.tone}"><span class="dot"></span>{status.text}</span>
      </div>
      <p class="muted">{p?.model || ''}{p?.connected && p.message && p.message !== status.text ? ` · ${p.message}` : ''}</p>
      <div class="actions row wrap">
        <button class="btn primary" onclick={() => input.click()} disabled={!p?.connected}>
          <Upload size={17} /> Chọn tệp và in
        </button>
        <button class="btn" onclick={chooseFile} disabled={!p?.connected}><FolderOpen size={17} /> In từ tệp đã lưu</button>
        <button class="btn ghost" onclick={identify} disabled={!device.online}><Lightbulb size={17} /> Identify</button>
      </div>
      <input bind:this={input} type="file" accept="application/pdf,image/*" hidden onchange={upload} />
    </div>
  </section>

  {#if p && p.reasons.length}
    <section class="card card-pad alerts">
      {#each p.reasons as r}
        <div class="row">
          <CircleAlert size={18} class={r.endsWith('-error') ? 'err' : 'warn'} />
          <span>{reasonText(r)}</span>
        </div>
      {/each}
    </section>
  {/if}

  {#if active}
    <section class="card card-pad job">
      <div class="row">
        <div class="grow">
          <h2 class="ellipsis">{active.name}</h2>
          <p class="muted small">
            {active.state === 'pending' ? 'Waiting for the document' : `Page ${active.pages + 1}`} · {formatSize(active.bytes)}
            received{active.user ? ` · from ${active.user}` : ''}
          </p>
        </div>
        <button class="btn danger" onclick={() => cancel(active.id)}><CircleStop size={17} /> Cancel</button>
      </div>
      <div class="bar indeterminate"><div></div></div>
    </section>
  {/if}

  <section class="card card-pad">
    <h2>Mực in &amp; vật tư</h2>
    {#if p?.supplies.length}
      <div class="inks">
        {#each p.supplies as s}
          <div class="ink">
            <div class="row"><span class="grow">{s.name}</span><b>{s.level >= 0 ? s.level + '%' : 'unknown'}</b></div>
            <div class="bar">
              <div style:width={(s.level >= 0 ? s.level : 0) + '%'} style:background={inkColor(s)}></div>
            </div>
          </div>
        {/each}
      </div>
      <p class="muted small">Refilled cartridges report "unknown".</p>
    {:else}
      <p class="muted">{p?.connected ? 'Máy in chưa cung cấp mức mực. Bạn vẫn có thể in bình thường.' : 'Cắm cáp máy in vào cổng USB OTG trên mạch.'}</p>
    {/if}
  </section>

  <section class="card card-pad">
    <h2>Lệnh in</h2>
    {#if (p?.jobLimit ?? 1) > 1}
      <p class="muted small">Đang nhận, chờ hoặc in {p?.jobCount ?? 0}/{p?.jobLimit} lệnh</p>
    {/if}
    {#if p?.jobs.length}
      <ul class="jobs">
        {#each p.jobs as j (j.id)}
          {@const [label, tone] = STATE[j.state] || [j.state, '']}
          <li class="row">
            <div class="grow">
              <div class="ellipsis">{j.name}</div>
              <div class="muted small">
                {[`${j.pages} page${j.pages === 1 ? '' : 's'}`, ago(j.time), j.user].filter(Boolean).join(' · ')}
              </div>
            </div>
            {#if j.state === 'pending' && j.id !== p.active}
              <button class="btn" onclick={() => cancel(j.id)}>Hủy</button>
            {/if}
            <span class="pill {tone}">
              {j.receiving ? 'Đang nhận' : j.queuePosition ? `Chờ #${j.queuePosition}` : label}
            </span>
          </li>
        {/each}
      </ul>
    {:else}
      <p class="muted">Chưa có lệnh in từ khi thiết bị khởi động.</p>
    {/if}
  </section>

  <section class="card card-pad tips">
    <h2>In từ điện thoại &amp; máy tính</h2>
    <p class="muted small">
      iPhone, iPad và Mac: chọn AirPrint trong menu Chia sẻ → In. Android và Windows: thêm máy in trong cùng mạng Wi-Fi. Nhấn BOOT để in trang kiểm tra trên bản đơn sắc; giữ 2 giây để hủy lệnh in.
    </p>
  </section>
</div>

<style>
  .intro {margin:4px 0 26px}.intro span {color:var(--accent);font-size:.72rem;letter-spacing:2px;font-weight:700}.intro h2{font-size:clamp(1.7rem,4vw,2.4rem);margin:8px 0}.hero{border-color:#43706f;position:relative}.hero::before{content:"";position:absolute;top:0;left:24px;right:24px;height:2px;background:linear-gradient(90deg,var(--accent),transparent)}
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 18px;
  }
  .hero,
  .alerts,
  .job {
    grid-column: 1 / -1;
  }
  .hero {
    display: flex;
    gap: 22px;
    padding: 24px;
    align-items: center;
  }
  .art {
    display: grid;
    place-items: center;
    width: 92px;
    height: 92px;
    border-radius: 24px;
    background: var(--accent-soft);
    color: var(--accent);
    flex: none;
  }
  .actions {
    margin-top: 16px;
  }
  h2 {
    margin-bottom: 12px;
  }
  .alerts {
    display: grid;
    gap: 6px;
  }
  .alerts :global(.warn) {
    color: var(--warn);
  }
  .alerts :global(.err) {
    color: var(--err);
  }
  .job h2 {
    margin: 0;
  }
  .job .bar {
    margin-top: 12px;
  }
  .indeterminate > div {
    width: 35%;
    animation: slide 1.4s ease-in-out infinite;
  }
  @keyframes slide {
    from {
      transform: translateX(-100%);
    }
    to {
      transform: translateX(300%);
    }
  }
  .inks {
    display: grid;
    gap: 14px;
    margin-bottom: 12px;
  }
  .ink .bar {
    height: 10px;
    margin-top: 6px;
    border-radius: 5px;
  }
  .ink .bar > div {
    border-radius: 5px;
  }
  .jobs {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
  }
  .jobs li {
    padding: 9px 0;
    border-top: 1px solid var(--border);
  }
  .jobs li:first-child {
    border-top: 0;
    padding-top: 0;
  }
  .tips {
    grid-column: 1 / -1;
  }
  .tips h2 {
    margin-bottom: 6px;
  }
  @media (max-width: 900px) {
    .grid {
      grid-template-columns: 1fr;
    }
    .hero {
      flex-direction: column;
      align-items: flex-start;
      gap: 14px;
      padding: 18px;
    }
    .art {
      width: 60px;
      height: 60px;
      border-radius: 16px;
    }
  }
</style>
