# Contextual screen translation research

This note turns primary-source evidence into bounded recommendations for d4r0. It
does not propose cloud processing or a document-layout dependency: all runtime
work remains local, revision-safe, and limited to changed screen regions.

## Recommended pipeline

```text
stable detector crops -> quadrilateral text lines -> contextual groups + order
    -> first OCR/translation result -> optional one-shot rectified OCR retry
    -> optional one-shot contextual retranslation -> revision-safe overlay
```

The key policy is progressive but bounded work: show a useful result after the
first stable pass, then replace it only when one specifically better input becomes
available. A source revision change cancels either replacement.

## Contextual grouping and reading order

OCR boxes are not yet translation units. Google documented its 2019 Lens variant
for Google Go as merging character boxes into lines, detecting coherent blocks
using style and color, and using alignment, language, and paragraph geometry to
derive reading order. It also corrected OCR with surrounding words and translated
whole sentences rather than isolated words. [Google Research: Giving Lens New
Reading Capabilities in Google Go](https://research.google/blog/giving-lens-new-reading-capabilities-in-google-go/)

Research on image translation reaches the same conclusion in a different visual
domain. *Towards Fully Automated Manga Translation* groups text by scene and
orders text within each scene because one sentence can span multiple regions and
same-scene text supplies more useful context than unrelated text elsewhere on the
page. [AAAI 2021 paper](https://cdn.aaai.org/ojs/17537/17537-13-21031-1-2-20210518.pdf)
PaddleOCR likewise treats layout and reading-order recovery as distinct work:
PP-StructureV3 returns ordered layout blocks and explicit `block_order` values,
whereas the ordinary OCR source uses only a top-to-bottom, left-to-right box sort.
[PP-StructureV3 documentation](https://www.paddleocr.ai/main/en/version3.x/pipeline_usage/PP-StructureV3.html)
[ordinary OCR sorting source](https://github.com/PaddlePaddle/PaddleOCR/blob/main/tools/infer/predict_system.py)

For d4r0:

- Form a group only from lines that plausibly belong to one UI component or
  utterance. Useful signals are containment in the same stable crop/panel,
  overlapping vertical span or aligned baselines, distance normalized by median
  text height, similar scale/style/color, and temporal co-change. Proximity alone
  must not join a subtitle to an unrelated HUD counter.
- Determine reading order inside each group, not across the whole screen. For the
  v1 German/Latin case, order line bands top-to-bottom and lines left-to-right,
  while preserving detected line breaks. Keep the original polygons so ordering
  can use baselines rather than axis-aligned top-left points alone.
- Send one numbered translation batch in that order, but retain item-to-polygon
  identity for overlay placement and stale-revision rejection. The group cache key
  should include ordered normalized source text and every member source revision.
- Treat grouping and ordering heuristics as tunable and observable. PP-Structure's
  document model is evidence that the problem matters, not a recommendation to
  run a heavy document parser over game frames.

## Rectification and selective OCR retry

Perspective and curved text are established scene-text failure modes. RARE
rectifies irregular word images before recognition, and ESIR reports improved
recognition by iteratively removing perspective distortion and curvature.
[RARE, CVPR 2016](https://openaccess.thecvf.com/content_cvpr_2016/papers/Shi_Robust_Scene_Text_CVPR_2016_paper.pdf)
[ESIR, CVPR 2019](https://openaccess.thecvf.com/content_CVPR_2019/html/Zhan_ESIR_End-To-End_Scene_Text_Recognition_via_Iterative_Image_Rectification_CVPR_2019_paper.html)
PaddleOCR's own inference source maps each four-point detection to a rectangle
with `getPerspectiveTransform` and `warpPerspective` before recognition.
[PaddleOCR crop source](https://github.com/PaddlePaddle/PaddleOCR/blob/main/tools/infer/utility.py)

That evidence supports a narrow retry, not unconditional whole-frame correction:

1. Keep the current fast recognition path for ordinary horizontal crops.
2. Preserve detector quadrilaterals. If the first result is empty or below the
   configured confidence threshold *and* geometry indicates rotation, skew,
   perspective, or clipping, construct a padded perspective-rectified line crop
   and recognize it once more.
3. Accept the retry only when it is non-empty, revision-current, and improves the
   recognition score (with basic length/character sanity checks). Otherwise retain
   the first low-confidence result and its existing low-confidence styling.
4. Permit at most one transformed OCR retry per line revision. Never retry a
   changed source crop, and never cascade transformations.

PaddleOCR's optional text-image unwarping module targets distortion, inclination,
and perspective deformation, but its documented model is a document unwarper;
running it on every game crop would require separate evidence and benchmarking.
[PaddleOCR rectification module](https://www.paddleocr.ai/main/en/version3.x/module_usage/text_image_unwarping.html)
Also, PaddleOCR's text-line orientation classifier has only 0-degree and 180-degree
classes, so it is not evidence of arbitrary-angle handling.
[orientation-classifier documentation](https://www.paddleocr.ai/main/en/version3.x/module_usage/text_line_orientation_classification.html)

## Fast result plus bounded retranslation

The first translation should be published as soon as its stable ordered group is
ready. A single later replacement is justified when nearby lines arrive within a
short settling window, rectified OCR changes a member, or the reading order becomes
better determined. This is a d4r0 scheduling decision, not behavior claimed for
Google Lens. Its quality rationale is supported by the scene-context paper above
and by Google's official TranslateGemma model card, which says output can improve
with additional context up to a point.
[TranslateGemma 4B model card](https://huggingface.co/google/translategemma-4b-it)

Bound the refinement explicitly:

- Allow zero or one contextual retranslation for a group generation.
- Trigger it only when the ordered source payload or membership materially changes;
  style-only changes do not qualify.
- Use a short configurable deadline after the first result. Once expired, wait for
  a new source revision rather than repeatedly revisiting the same scene.
- Atomically replace the first translation only if every member revision is still
  current. Record first-result and replacement latency separately, plus the reason
  the replacement was requested.
- Keep failed items visible/untranslated after the existing bounded retry; do not
  let one item force repeated translation of the batch.

### Google Lens claim boundary

The detailed source is specifically a 2019 description of **Lens in Google Go**.
It documents that product's capture strategy, contextual OCR correction, block and
reading-order analysis, sentence translation, and context-preserving overlay. It
does **not** document the internal pipeline of current Google Lens, nor a fast-pass
followed by retranslation. Current Google material documents user-visible Lens
translation and overlay, but not those internals.
[Google's 2023 Lens feature description](https://blog.google/products-and-platforms/devices/google-lens/google-lens-features/)
Accordingly, d4r0 may use Google Go Lens as prior design evidence; any statement
that present-day Lens uses the same pipeline would be inference and should not be
presented as fact.

## Win32 notification-area presence

A notification-area icon fits a long-running overlay with no ordinary application
window when it communicates status and gives access to controls. Microsoft calls
this the *notification area*, not the system tray, and advises one icon per
component, restrained status changes, user control, and an Exit command.
[Notification-area UX guidance](https://learn.microsoft.com/en-us/windows/win32/uxguide/winenv-notification)

Implement the smallest native pattern:

- Add one GUID-identified `NOTIFYICONDATAW` icon with `Shell_NotifyIconW(NIM_ADD)`
  and immediately request `NOTIFYICON_VERSION_4` with `NIM_SETVERSION`; Microsoft
  requires the version call after every add and recommends `guidItem` on Windows 7
  and later. Route callbacks to the existing Win32 message window.
  [Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw)
  [NOTIFYICONDATAW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw)
- Expose current translation/replay/error status in a concise tooltip and a normal
  context menu for translation toggle, show original, diagnostics, replay, settings,
  and Exit. Avoid periodic balloons and rapidly animated status.
- Register the `TaskbarCreated` message and re-add the icon after Explorer/taskbar
  recreation; Windows also broadcasts this message for a primary-display DPI change
  on Windows 10. Remove the icon with `NIM_DELETE` during clean shutdown.
  [taskbar creation notification](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar#taskbar-creation-notification)

## Per-process resource accounting

Diagnostics should attribute d4r0 and its owned model process instead of reporting
whole-machine load as if d4r0 caused it.

| Quantity | Runtime mechanism | Reporting rule |
| --- | --- | --- |
| CPU | Delta user + kernel time from `GetProcessTimes` for each owned process | Divide by elapsed wall time and state whether the percentage is normalized to one logical CPU or the whole machine. |
| Memory | `GetProcessMemoryInfo` with `PROCESS_MEMORY_COUNTERS_EX` | Show private committed bytes (`PrivateUsage`) and resident working set separately; they are not interchangeable. |
| Process-tree CPU/I/O | Put the owned model process and descendants in a job object; query `JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION` | Job accounting includes associated processes that have already exited, making it suitable for cumulative model-server accounting. Keep d4r0's own process separate, then show an optional combined total. |
| d4r0 GPU memory | `IDXGIAdapter3::QueryVideoMemoryInfo` for local and non-local segment groups | This reports the calling process's usage and budget. Keep local and shared/non-local values separate. |
| model-process GPU memory | `D3DKMTQueryVideoMemoryInfo` with the model process handle, adapter, and segment group | Label this lower-level measurement and degrade to unavailable if the query is unsupported; do not substitute total adapter memory. |
| GPU engine contention | WPR/WPA GPU Activity during acceptance benchmarks, filtered by process/PID | Use trace evidence for p1/p99 contention analysis rather than an undocumented in-app estimate. |

Primary API references:
[GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes),
[process memory sample](https://learn.microsoft.com/en-us/windows/win32/psapi/collecting-memory-usage-information-for-a-process),
[job-object accounting](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects#resource-accounting-for-jobs),
[job CPU/I/O structure](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_and_io_accounting_information),
[DXGI process video memory](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo),
[D3DKMT per-process video-memory fields](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_queryvideomemoryinfo), and
[WPR GPU Activity profile](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/built-in-recording-profiles#resource-analysis-profiles).

Task Manager's GPU counters come from WDDM's VidSch and VidMm and work across
DirectX, Vulkan, OpenGL, OpenCL, and vendor APIs. Microsoft also cautions that
summing per-process video memory can double-count allocations shared between
processes. [DirectX Developer Blog: GPUs in Task
Manager](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/)
Do not use `D3DKMTQueryStatistics` to imitate Task Manager: Microsoft's reference
marks that structure reserved for system use.
[D3DKMT_QUERYSTATISTICS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-d3dkmt_querystatistics)

Sample inexpensive counters at a low fixed cadence (for example, once per second),
retain only bounded rolling aggregates, and keep measurements advisory. They must
never auto-switch, unload, or throttle the model selected by the user.
