<script lang="ts">
  import { onDestroy } from 'svelte';
  import Printer from '@lucide/svelte/icons/printer';
  import ChevronLeft from '@lucide/svelte/icons/chevron-left';
  import ChevronRight from '@lucide/svelte/icons/chevron-right';
  import Minus from '@lucide/svelte/icons/minus';
  import Plus from '@lucide/svelte/icons/plus';
  import LoaderCircle from '@lucide/svelte/icons/loader-circle';
  import Modal from './Modal.svelte';
  import { device, inkSummary } from '../lib/device.svelte';
  import { fileSource, type StoredFile } from '../lib/files.svelte';
  import {
    defaultPaper,
    MARGINS,
    PAPERS,
    parseRanges,
    place,
    type Fit,
    type Orientation,
    type PaperId,
  } from '../lib/layout';
  import { renderJob, sendJob, type PrintOptions } from '../lib/print';
  import { whiteCanvas, type Source } from '../lib/source';
  import { toast } from '../lib/ui.svelte';

  let { file, onclose }: { file: StoredFile; onclose: () => void } = $props();

  const saved = (() => {
    try {
      return JSON.parse(localStorage.getItem('printSettings.v2') || '{}');
    } catch {
      return {};
    }
  })();

  let src = $state.raw<Source | null>(null);
  let loadError = $state('');
  let copies = $state(1);
  let range = $state('');
  let paper = $state<PaperId>(saved.paper in PAPERS ? saved.paper : defaultPaper());
  let orientation = $state<Orientation>('auto');
  // a scan prints at its real size (a copy); anything else fits the page
  // svelte-ignore state_referenced_locally
  let fit = $state<Fit>(file.source === 'scan' && file.dpi ? 'actual' : 'fit'); // a scan: a 1:1 copy
  let gray = $state<boolean>(true);
  const p = $derived(device.printer);
  const isCanon = $derived((p?.model || '').includes('LBP2900'));
  let dpi = $state<300 | 600>(!device.printer || device.printer.model.includes('LBP2900') || saved.dpi === 300 ? 300 : 600);
  let pageIdx = $state(0);
  let previewEl: HTMLDivElement;

  type Phase = 'setup' | 'render' | 'send' | 'finish';
  let phase = $state<Phase>('setup');
  let frac = $state(0);
  let detail = $state('');
  let abort: AbortController | null = null;
  let closed = false;
  $effect(() => { if (dpi === 300 && p && !isCanon && !p.resolutions?.includes(300)) dpi = 600; });
  const paperOptions = $derived((Object.keys(PAPERS) as PaperId[]).filter((id) =>
    device.printer?.papers ? device.printer.papers.includes(PAPERS[id].pwg) : id === 'a4' || id === 'letter'));
  $effect(() => {
    if (phase === 'setup' && !paperOptions.includes(paper)) paper = defaultPaper();
  });

  // svelte-ignore state_referenced_locally
  fileSource(file).then(
    (s) => (src = s),
    (e) => (loadError = (e as Error).message),
  );

  const count = $derived(src?.pages.length ?? file.pages ?? 1);
  const selected = $derived(parseRanges(range, count));
  const blocker = $derived.by(() => {
    if (!device.online) return 'The printer box is not reachable.';
    if (!p?.connected) return 'No printer is connected to the printer box.';
    if ((p.jobLimit ?? 1) > 1) {
      if ((p.jobCount ?? 0) >= (p.jobLimit ?? 1)) return 'Hàng đợi đã đầy. Hãy thử lại khi có lệnh hoàn tất.';
    } else if (p.state === 'processing') {
      return 'The printer is busy with another job.';
    }
    if (device.scan?.state === 'scanning') return 'The printer is scanning.';
    return '';
  });

  $effect(() => {
    try {
      localStorage.setItem('printSettings.v2', JSON.stringify({ paper, gray, dpi: isCanon ? saved.dpi : dpi }));
    } catch {
      /* private mode */
    }
  });

  $effect(() => {
    if (pageIdx >= count) pageIdx = Math.max(0, count - 1);
  });

  // preview of the sheet
  let previewToken = 0;
  $effect(() => {
    const s = src;
    const opts = { paper, fit, orientation, pageIdx, gray };
    if (!s || !previewEl) return;
    const token = ++previewToken;
    const P = PAPERS[opts.paper];
    const box = previewEl.getBoundingClientRect();
    const scale = Math.min((box.width || 300) / P.w, (box.height || 420) / P.h) * Math.min(2, devicePixelRatio || 1);
    const ps = s.pages[opts.pageIdx];
    const pl = place(ps.w, ps.h, opts.paper, opts.fit, opts.orientation);
    const c = whiteCanvas(P.w * scale, P.h * scale);
    s.draw(c, opts.pageIdx, pl.rot, pl.scale * scale, pl.x * scale, pl.y * scale).then(() => {
      if (token !== previewToken) return;
      if (opts.gray) {
        const ctx = c.getContext('2d')!;
        ctx.globalCompositeOperation = 'saturation';
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, c.width, c.height);
      }
      c.className = 'sheet';
      previewEl.querySelector('canvas')?.remove();
      previewEl.prepend(c);
    });
  });

  async function print() {
    if (!src || !selected?.length) return;
    const o: PrintOptions = { paper, fit, orientation, pages: selected, copies, gray, dpi };
    abort = new AbortController();
    const signal = abort.signal;
    const sheets = selected.length * copies;
    const name = file.name;
    try {
      phase = 'render';
      const doc = await renderJob(
        src,
        o,
        (done, total) => {
          frac = done / total;
          detail = `Preparing page ${Math.min(total, Math.floor(done) + 1)} of ${total}`;
        },
        signal,
      );
      phase = 'send';
      frac = 0;
      detail = 'Sending to the printer';
      const result = await sendJob(
        doc,
        name,
        (f) => {
          frac = f;
          if (f >= 1) {
            phase = 'finish';
            detail = 'Finishing the last page';
          }
        },
        signal,
      );
      if (result.queued) toast(`Đã xếp hàng lệnh #${result.jobId}: ${name}`, 'ok');
      else toast(`Printed ${sheets} page${sheets === 1 ? '' : 's'} of ${name}`, 'ok');
      if (!closed) onclose();
    } catch (e) {
      if ((e as Error).name === 'AbortError') {
        toast('Print canceled');
      } else {
        toast(`Print failed: ${(e as Error).message}`, 'error', [], 9000);
      }
      phase = 'setup';
    } finally {
      abort = null;
    }
  }

  function close() {
    // a job being sent goes on in the background
    closed = true;
    if (phase === 'render') abort?.abort();
    onclose();
  }

  onDestroy(() => {
    closed = true;
  });

  const marginStyle = $derived.by(() => {
    const P = PAPERS[paper];
    return `left:${(MARGINS.left / P.w) * 100}%;right:${(MARGINS.right / P.w) * 100}%;top:${(MARGINS.top / P.h) * 100}%;bottom:${(MARGINS.bottom / P.h) * 100}%`;
  });
</script>

<Modal title="Print {file.name}" onclose={close} wide>
  <div class="layout">
    <div class="left">
      <div class="preview" bind:this={previewEl} style:aspect-ratio={`${PAPERS[paper].w} / ${PAPERS[paper].h}`}>
        <div class="margins" style={marginStyle}></div>
        {#if !src && !loadError}<div class="loading"><LoaderCircle class="spin" /></div>{/if}
        {#if loadError}<div class="loading err">{loadError}</div>{/if}
      </div>
      {#if count > 1}
        <div class="pager row">
          <button class="icon-btn" onclick={() => pageIdx--} disabled={pageIdx === 0} aria-label="Previous page"><ChevronLeft /></button>
          <span class="small muted">Page {pageIdx + 1} of {count}</span>
          <button class="icon-btn" onclick={() => pageIdx++} disabled={pageIdx >= count - 1} aria-label="Next page"><ChevronRight /></button>
        </div>
      {/if}
      <p class="small muted center">The dashed line shows what the printer can reach.</p>
    </div>

    <div class="opts">
      {#if phase !== 'setup'}
        <div class="progress card card-pad">
          <div class="row"><LoaderCircle size={18} class="spin" /><b>{detail}</b></div>
          <div class="bar"><div style:width={`${Math.max(2, frac * 100)}%`}></div></div>
          <p class="small muted">
            {#if phase === 'render'}
              Pages are prepared here in the browser, then sent.
            {:else}
              The printer prints while it receives. You can close this window; progress shows on the Printer page.
            {/if}
          </p>
          <button class="btn danger" onclick={() => abort?.abort()}>Cancel</button>
        </div>
      {/if}
      <fieldset disabled={phase !== 'setup'}>
        <div class="field">
          <span class="label">Copies</span>
          <div class="stepper">
            <button class="icon-btn" onclick={() => (copies = Math.max(1, copies - 1))} aria-label="Fewer"><Minus size={16} /></button>
            <input class="input" type="number" min="1" max="99" bind:value={copies} aria-label="Copies" />
            <button class="icon-btn" onclick={() => (copies = Math.min(99, copies + 1))} aria-label="More"><Plus size={16} /></button>
          </div>
        </div>
        {#if count > 1}
          <div class="field">
            <span class="label">Pages</span>
            <input class="input" placeholder="All {count} pages, or e.g. 1-3, 5" bind:value={range} />
            {#if !selected}<span class="err small">Pages 1 to {count}, e.g. "1-3, 5"</span>{/if}
          </div>
        {/if}
        <div class="field">
          <span class="label">Paper</span>
          <div class="seg">
            {#each paperOptions as id}
              <button class:on={paper === id} onclick={() => (paper = id)}>{PAPERS[id].name}</button>
            {/each}
            {#if paper === 'a6'}<small>A6: đặt giấy 105 × 148 mm, cạnh ngắn vào trước. LBP2900 dùng khổ tùy chỉnh, cần thử thực tế.</small>{/if}
          </div>
        </div>
        <div class="field">
          <span class="label">Size</span>
          <div class="seg">
            <button class:on={fit === 'fit'} onclick={() => (fit = 'fit')}>Fit to page</button>
            <button class:on={fit === 'fill'} onclick={() => (fit = 'fill')}>Fill page</button>
            <button class:on={fit === 'actual'} onclick={() => (fit = 'actual')}>Actual size</button>
          </div>
        </div>
        <div class="field">
          <span class="label">Orientation</span>
          <div class="seg">
            <button class:on={orientation === 'auto'} onclick={() => (orientation = 'auto')}>Auto</button>
            <button class:on={orientation === 'portrait'} onclick={() => (orientation = 'portrait')}>Portrait</button>
            <button class:on={orientation === 'landscape'} onclick={() => (orientation = 'landscape')}>Landscape</button>
          </div>
        </div>
        <div class="field">
          <span class="label">Color</span>
          <div class="seg">

            <button class:on={gray} onclick={() => (gray = true)}>Black &amp; white</button>
          </div>
        </div>
        <div class="field">
          <span class="label">Quality</span>
          <div class="seg">
            {#if p?.resolutions?.includes(300) || isCanon}
              <button class:on={dpi === 300} onclick={() => (dpi = 300)}>In nhanh · 300 dpi</button>
            {/if}
            <button class:on={dpi === 600} onclick={() => (dpi = 600)}>In nét · 600 dpi</button>

          </div>
          <span class="small muted">{p?.name ?? 'ESP 32 Print Server'} · {dpi} dpi · đen trắng. {dpi === 300 ? 'Lấy mẫu 300 dpi, xuất CAPT 600 dpi; phù hợp văn bản, giảm nét chi tiết nhỏ.' : ''}</span>
        </div>
      </fieldset>
    </div>
  </div>

  {#snippet footer()}
    <div class="grow small">
      {#if blocker}
        <span class="err">{blocker}</span>
      {:else}
        <span class="muted">{p?.name} · {inkSummary(p)}</span>
      {/if}
    </div>
    <button class="btn" onclick={close}>{phase === 'setup' ? 'Cancel' : 'Hide'}</button>
    <button class="btn primary" onclick={print} disabled={!src || !selected?.length || !!blocker || phase !== 'setup'}>
      <Printer size={17} /> Print {selected && selected.length * copies > 1 ? `${selected.length * copies} pages` : ''}
    </button>
  {/snippet}
</Modal>

<style>
  .layout {
    display: grid;
    grid-template-columns: 1.1fr 1fr;
    gap: 24px;
    padding: 20px;
  }
  .left {
    display: grid;
    gap: 10px;
    align-content: start;
    justify-items: center;
    background: var(--surface-3);
    border-radius: var(--radius);
    padding: 22px;
  }
  .preview {
    position: relative;
    width: 100%;
    max-width: 380px;
    max-height: 62vh;
    background: var(--paper);
    box-shadow: var(--shadow);
  }
  .preview :global(canvas.sheet) {
    display: block;
    width: 100%;
    height: 100%;
  }
  .margins {
    position: absolute;
    border: 1px dashed rgba(63, 86, 224, 0.55);
    pointer-events: none;
  }
  .loading {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
    color: #888;
    padding: 20px;
    text-align: center;
  }
  .pager {
    justify-content: center;
  }
  .center {
    text-align: center;
  }
  .opts {
    display: grid;
    gap: 16px;
    align-content: start;
  }
  fieldset {
    border: 0;
    margin: 0;
    padding: 0;
    display: grid;
    gap: 16px;
    min-width: 0;
  }
  fieldset:disabled {
    opacity: 0.55;
  }
  .stepper {
    display: flex;
    align-items: center;
    gap: 4px;
  }
  .stepper .input {
    width: 70px;
    text-align: center;
  }
  .progress {
    display: grid;
    gap: 10px;
    border-color: var(--accent);
  }
  .progress .btn {
    justify-self: start;
  }
  @media (max-width: 760px) {
    .layout {
      grid-template-columns: 1fr;
      padding: 14px;
      gap: 16px;
    }
    .left {
      padding: 14px;
    }
    .preview {
      max-width: 240px;
    }
  }
</style>
