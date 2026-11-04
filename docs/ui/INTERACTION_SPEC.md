# PsyRoot — interface study 03

Owner: Astra. Status: design card approved by Linus on 2026-11-04; study 03 export prepared for handoff. Architecture authority: merged rev 0.1.5 (main 35c79c8). Unresolved presentation/integration details remain review items.

Linus's feedback on study 02: “looks good bro, i will think about a better name”. PsyRoot is the locked product name. This feedback approves continuing the visual direction, not every open musical or technical proposal. Keep display branding replaceable and distinguish it from persistent parameter/state identifiers.

## Visual direction

Reference: https://www.42codon.com/. Near-black background, warm-white text, fine grey separators, strong sans-serif titles and compact monospace labels. Proposed tokens: background #0A0A0A, foreground #F4F4F2, panel #101010, border #323232, secondary text #989894. Small desaturated engine accents supplement names and state labels. Flat knobs, no neon rings or decorative glow. The companion SVG is an editable layout study, not a running plugin.

## Layout

1440 × 1000 reference canvas. Header: working product name / 42codon, preset, host tempo, MIDI activity, and a compact `TRIAL · 7 DAYS LEFT` status with `ACTIVATE` entry point. Activation screen layout waits for Cursor’s Moonbase integration flow. Three equal engine panels expose enable, level and SEQ/MIDI source. Each has four primary controls; parameter-derived envelope/glide displays belong in the detail inspector. Pattern toolbar and aligned lanes occupy the upper half, above the compact engine panels. The lanes and their pitch controls lead the visual hierarchy: this is where the held-root + latch + rolling pattern combination becomes clear. Put LATCH beside the shared ARP ORDER in the toolbar, default OFF. Put TRANSPOSE on each lane control row, range ±24 semitones. Selected-step controls sit below the grid; shared duck controls, duck source/status and master controls form the footer.

Rolling primary controls: cutoff, envelope amount, filter decay, amp decay. Phase reset and filter reset remain visible. Sliding: cutoff, resonance, default glide, accent. FX: FM amount, formant, movement, synchronized rate. Details opens a persistent inspector for the selected engine; it replaces the selected-step inspector rather than covering the pattern.

## Pattern interaction

Click a step to select it; double-click toggles enabled state. Space toggles the selected step when the grid has keyboard focus. Arrow keys move selection. Explicit on/off control in the inspector provides an alternative to double-click. Pattern edits support undo/redo and do not change the current engine selection unexpectedly.

Rolling INIT demonstrates the product in one glance: hold a MIDI root, or enable LATCH and play a root, then the host-synced roll produces the kick–bass–bass–bass line with ducking. With no held notes and LATCH off, all SEQ lanes stay silent. LATCH clears on a new note-on set, when switched off, on CC123, and on transport stop. Rolling INIT has exactly 12 enabled steps: 2,3,4,6,7,8,10,11,12,14,15,16. Sliding and FX start disabled with empty lanes. The draft uses architecture v0.1 values, including 50% gate and 70 ms amp decay. Merged architecture rev 0.1.5 §4.4 specifies amp A 0.5 ms / D 70 ms / S 0% / R 12 ms, with D and R measured to −60 dB of their span. At 145 BPM and 50% gate, release begins around 51.7 ms and reaches that threshold around 63.7 ms. This describes the amp envelope, not a guarantee about every downstream processing tail.

SEQ uses held MIDI notes with the host-synchronized lane. MIDI bypasses that engine's lane but preserves its pattern; replace playhead with BYPASSED — MIDI. Engine enable, source and pattern presence remain separate states. Disabled engines retain editable parameters and patterns.

Step inspector: enabled, pitch offset, gate, velocity, accent, tie and destination glide. Glide has FOLLOW LANE or OVERRIDE selection. SEQ displays percentage and current-tempo milliseconds; clamping shows requested and effective duration. Direct MIDI uses a separately stored duration in milliseconds (architecture revision 0.1.1), without a predicted clamp. Ties are shown as connecting lines and never conveyed by color alone.

Global swing has explicit FOLLOW GLOBAL / OVERRIDE per lane. Lane rate and length are independently displayed. Shared arp order (UP / DOWN / UP-DOWN / AS PLAYED / RANDOM; default UP) uses an independent arp index per lane. Lane pitch is arp note + step offset + lane TRANSPOSE (each ±24 st). Per-step probability and an FX expression lane are later work, outside v1.

## Trial status

The agreed model is a full-featured 7-day trial, no payment card, then locked until activated through Moonbase. Show days remaining in the header and provide an ACTIVATE entry point. Exact activation dialog, network/error messages and success state depend on Cursor’s Moonbase flow and remain unspecified. The audio thread only reads a licensed flag; trial and network checks stay outside audio processing.

## Bus and status

Duck exposes depth, release, curve and target toggles for Roll/Slide/FX. Source selection is separate from signal activity. INTERNAL states its quarter-note trigger assumption. MIDI shows numeric note first, channel, and pass-through. AUDIO shows input availability and activity where supported. No silent source fallback. Curve display reflects configured gain envelope; it is not a live audio scope.

SUB OWNER is ROLL / SLIDE / OFF. Each bass engine exposes LOW CUT AUTO / ON / OFF and its effective state. Shared non-owner cutoff is independent of MONO LOW. SUB OFF means no sine sub; AUTO low-cut becomes off on both bass engines. Explicit low-cut overrides, FX low-cut, mono-low and duck remain active. A quiet overlap indicator appears when both active bass engines are full-range.

MONO LOW copy: “Stereo width below X Hz. Mono content untouched.” Architecture rev 0.1.5 §6.4 specifies a side-channel high-pass with the mid channel unchanged. This statement describes the MONO LOW stage only; it does not promise unchanged audio through the entire instrument or unchanged phase in the stereo difference signal.

## Controls and accessibility

All values have units, keyboard focus and a text-entry path. Drag adjusts knobs; shift-drag is fine adjustment; double-click restores documented default. Menus provide the same actions. Selected, disabled, bypassed and waiting states have textual or shape cues independent of color. Control hit targets should be at least 28 logical pixels; reference labels at least 12 pixels, with plugin UI scaling available. The SVG is reference-size only and does not yet prove minimum-size usability.

## Review gates

Fable: review the rendered control labels against merged architecture rev 0.1.5. Envelope, direct-MIDI glide storage, step inheritance and source routing have already received architecture review. Cursor: validate UI scaling, thread-safe display snapshots and bounded note reset strategy. Host sidechain availability is reported passed in Ableton VST3/AU and Logic AU. Linus approved the design card; outstanding musical proposals still require his decision. The 7-day Moonbase trial model is agreed; the activation flow presentation awaits integration details.

## Revision 0.1.1 alignment

Audio duck distinguishes NO BUS from SILENT when host information allows it, plus ACTIVE, WAITING and STOPPED. Transport stop releases SEQ notes; direct MIDI voices and their latched routing remain valid. Fable has addressed the prior envelope, routing and glide-inheritance review comments. Musical proposals and visual approval remain separate.

## Decisions added 2026-11-04

Trial: 7 days, full features, no card, then locked until Moonbase activation. Arp orders UP / DOWN / UP-DOWN / AS PLAYED / RANDOM; default UP. LATCH defaults OFF. No held note and no latch means silence. Per-lane arp index with shared order. Per-lane TRANSPOSE ±24 st. Sequencer, arp and router are the product’s first-use demonstration; INIT should show the rolling K-B-B-B line immediately when a root is held or latched.


## Product name

**PsyRoot** is the approved product name as of 2026-11-04. Use this in visible UI, docs and team communication. Update the artwork wordmark accordingly. Existing bundle identifiers and plugin codes remain name-neutral; do not change them for this naming decision.

## Study 03 export and state coverage

`psyroot-interface-v3.svg` is the editable source; `psyroot-interface-v3.png` is a visually inspected 1440 × 1000 export. `render.py` reproduces the SVG. The square SVG is only a Quick Look rendering intermediate.

The overview depicts INIT with transport stopped, MIDI idle, no held note, and step 02 selected. Step numerals inside active cells are pitch offsets, not resolved notes. The selected-step NOTE stays an em dash until a root exists. LATCH is off; clicking it arms latching but requires a MIDI note before playback. The 12 enabled Roll cells are visible while silent.

Study 03 moves the pattern ahead of engine knobs to reflect the approved first-use priority. It adds the previously missing visible LATCH, lane TRANSPOSE, lane GLOBAL swing status, SEQ glide milliseconds, EXP/LIN duck curve buttons and quality switch. The PsyRoot wordmark and trial status have separate space. Visible values are aligned with the merged architecture §8 default table, including 2x quality, master drive 0% and output 0 dB. This picture does not redefine DSP defaults.

Additional states for implementation review: a tied Slide destination shows a connector annotated with effective glide duration; selecting it exposes FOLLOW LANE / OVERRIDE, requested duration and effective clamped duration. No valid transition shows an em dash. In MIDI source mode, keep the saved pattern visible with BYPASSED — MIDI and use the separately stored millisecond glide. The INIT screenshot cannot demonstrate a slide transition because Slide starts disabled and empty.

Remaining implementation presentation work: activation-flow screens after Cursor supplies Moonbase states; smallest supported plugin-size verification. This export is a static interface study, not implemented plugin behavior.
