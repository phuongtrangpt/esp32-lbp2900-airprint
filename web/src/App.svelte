<script lang="ts">
  import { onMount } from 'svelte';
  import ArrowLeft from '@lucide/svelte/icons/arrow-left';
  import Sidebar from './components/Sidebar.svelte';
  import Toasts from './components/Toasts.svelte';
  import PrintDialog from './components/PrintDialog.svelte';
  import Viewer from './components/Viewer.svelte';
  import FilePicker from './components/FilePicker.svelte';
  import PrinterView from './views/PrinterView.svelte';
  import ScannerView from './views/ScannerView.svelte';
  import FilesView from './views/FilesView.svelte';
  import ProcessView from './views/ProcessView.svelte';
  import { connect, device, onState } from './lib/device.svelte';
  import { loadFiles } from './lib/files.svelte';
  import { loadWork } from './lib/process.svelte';
  import { initScanSync } from './lib/scan.svelte';
  import { ui, toast } from './lib/ui.svelte';

  const ROUTES = ['printer', 'scanner', 'files', 'process'] as const;
  type Route = (typeof ROUTES)[number];
  const TITLES: Record<Route, string> = { printer: 'Trang in', scanner: 'Quét tài liệu', files: 'Tệp của bạn', process: 'Xử lý tài liệu' };

  let audioEnabled = $state(false);
  let audioPending = $state(false);
  let audio: AudioContext | null = null;
  let nextSound = 0;
  const audioSupported = typeof AudioContext !== 'undefined';

  function chime(repeats: number) {
    const ctx = audio;
    if (!audioEnabled || !ctx || ctx.state !== 'running') return;
    let start = Math.max(ctx.currentTime + 0.015, nextSound);
    for (let i = 0; i < repeats; i++) {
      for (const [frequency, delay] of [[880, 0], [660, 0.16]]) {
        const at = start + delay;
        const note = ctx.createOscillator();
        const level = ctx.createGain();
        note.frequency.value = frequency;
        level.gain.setValueAtTime(0, at);
        level.gain.linearRampToValueAtTime(0.14, at + 0.015);
        level.gain.exponentialRampToValueAtTime(0.001, at + 0.20);
        note.connect(level).connect(ctx.destination);
        note.onended = () => { note.disconnect(); level.disconnect(); };
        note.start(at);
        note.stop(at + 0.21);
      }
      start += 0.55;
    }
    nextSound = start;
  }

  function closeAudio() {
    const ctx = audio;
    audio = null;
    nextSound = 0;
    if (ctx) void ctx.close().catch(() => {});
  }

  async function toggleAudio() {
    if (audioEnabled && audio?.state === 'running') {
      audioEnabled = false;
      closeAudio();
      return;
    }
    audioPending = true;
    try {
      const ctx = audio ??= new AudioContext();
      await ctx.resume();
      if (audio !== ctx) return;
      audioEnabled = ctx.state === 'running';
      if (device.printer?.reasons.includes('media-empty-error')) chime(3);
    } catch {
      audioEnabled = false;
      closeAudio();
      toast('Không bật được âm báo trên trình duyệt này.', 'error');
    } finally {
      audioPending = false;
    }
  }

  onMount(() => {
    const unsubscribe = onState(({ printer: before }) => {
      const after = device.printer;
      if (!before || !after) return;
      if (after.reasons.includes('media-empty-error') && !before.reasons.includes('media-empty-error'))
        chime(3);
      for (const job of after.jobs) {
        if (job.state === 'completed' && !before.jobs.some(old => old.id === job.id && old.state === 'completed'))
          chime(1);
      }
    });
    return () => { unsubscribe(); closeAudio(); };
  });

  let firmware = $state('Đang đọc…');
  onMount(() => {
    fetch('/api/setup/status', { cache: 'no-store' }).then(r => r.ok ? r.json() : null).then(j => { firmware = j?.firmware || 'Không đọc được'; }).catch(() => { firmware = 'Không đọc được'; });
  });

  let hash = $state(location.hash);
  let narrow = $state(false);

  const route = $derived.by((): Route | null => {
    const r = hash.replace(/^#\/?/, '').split('/')[0];
    return (ROUTES as readonly string[]).includes(r) ? (r as Route) : null;
  });
  // phones open on the menu; wider screens on the printer
  const current = $derived<Route | null>(route ?? (narrow ? null : 'printer'));

  onMount(() => {
    const mq = matchMedia('(max-width: 760px)');
    narrow = mq.matches;
    mq.addEventListener('change', (e) => (narrow = e.matches));
    const onHash = () => (hash = location.hash);
    addEventListener('hashchange', onHash);
    initScanSync();
    connect();
    loadFiles();
    loadWork();
    return () => removeEventListener('hashchange', onHash);
  });

  $effect(() => {
    const name = device.printer?.name;
    document.title = current ? `${TITLES[current]} · vietphuong esp` : 'vietphuong esp · Máy in Wi-Fi';
  });
</script>

<div class="app" class:home={narrow && !current}>
  <Sidebar {firmware} {audioSupported} {audioEnabled} {audioPending} {toggleAudio} current={current ?? ''} home={narrow && !current} />
  {#if current}
    <main>
      {#if narrow}
        <header class="topbar">
          <a class="icon-btn" href="#/" aria-label="Back to the menu"><ArrowLeft size={20} /></a>
          <h1>{TITLES[current]}</h1>
          {#if current === 'printer' && audioSupported}
            <button class="btn" type="button" onclick={toggleAudio} disabled={audioPending}>
              {audioEnabled ? 'Tắt âm báo' : 'Bật âm báo'}
            </button>
          {/if}
        </header>
      {/if}
      <div class="content">
        {#if current === 'printer'}
          <PrinterView />
        {:else if current === 'scanner'}
          <ScannerView />
        {:else if current === 'files'}
          <FilesView />
        {:else}
          <ProcessView />
        {/if}
      </div>
      <footer class="copyright"><span>© {new Date().getFullYear()} Việt Phương · <b>vietphuong esp</b></span><span>Firmware {firmware}</span><a href="https://www.facebook.com/leviethoaiphuong" target="_blank" rel="noopener noreferrer">Facebook hỗ trợ ↗</a></footer>
    </main>
  {/if}
</div>

{#if ui.printFile}
  <PrintDialog file={ui.printFile} onclose={() => (ui.printFile = null)} />
{/if}
{#if ui.viewFile}
  <Viewer file={ui.viewFile} onclose={() => (ui.viewFile = null)} />
{/if}
{#if ui.picker}
  <FilePicker {...ui.picker} onclose={() => (ui.picker = null)} />
{/if}
<Toasts />

<style>
  .copyright { display:flex; flex-wrap:wrap; justify-content:space-between; gap:12px; padding:20px 32px; border-top:1px solid var(--border); color:var(--muted); font-size:.8rem; }
  .app {
    display: grid;
    grid-template-columns: var(--sidebar) 1fr;
    height: 100%;
  }
  main {
    min-width: 0;
    height: 100%;
    overflow: auto;
    display: flex;
    flex-direction: column;
  }
  .content {
    width: 100%;
    max-width: 1180px;
    margin: 0 auto;
    padding: 28px 32px 48px;
    flex: 1;
  }
  .topbar {
    position: sticky;
    top: 0;
    z-index: 5;
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 8px 10px;
    padding-top: max(8px, env(safe-area-inset-top));
    background: color-mix(in srgb, var(--bg) 88%, transparent);
    backdrop-filter: blur(10px);
    -webkit-backdrop-filter: blur(10px);
    border-bottom: 1px solid var(--border);
  }
  .topbar h1 {
    font-size: 1.1rem;
  }
  @media (max-width: 760px) {
    .app {
      grid-template-columns: 1fr;
    }
    .app:not(.home) :global(.sidebar) {
      display: none;
    }
    .content {
      padding: 16px 14px 40px;
      padding-bottom: max(40px, env(safe-area-inset-bottom));
    }
  }
</style>
