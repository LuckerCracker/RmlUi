# Damage / NeedsRender (Core)

This document describes the Core-side damage tracking and needs-render API.

## Goals

- event-driven damage accumulation, no per-frame DOM scans
- conservative damage (prefer larger over smaller)
- backend-agnostic, no RenderInterface changes

## API summary

Context exposes:

- `NeedsRender()` -> bool
- `SetForceFullRedraw(bool)` / `RequestFullRedraw()`
- `ClearRenderRequests()`
- `GetDamageRegion()` / `TakeDamageRegion()`
- debug: `GetDebugDamageRects()`, `GetDamageAreaPercent()`, `GetDamageFullRedrawSuggested()`

Damage region settings:

- `SetDamageMergeDistance(int)`
- `SetDamageMaxRects(size_t)`
- `SetDamageFullRedrawThresholds(float area_percent, size_t rect_count)`

## What adds damage

Damage is accumulated only at dirty/event-driven points.

Main triggers (Core):

- layout changes: `Element::DirtyLayout()`
- geometry changes: `Element::DirtyAbsoluteOffset()`
- paint changes: `Element::OnPropertyChange()` when background/border/opacity/effects/etc change
- stacking context changes: `Element::DirtyStackingContext()`
- transforms: `Element::DirtyTransformState()`
- element removal: `Element::RemoveChild()` adds old bounds
- new bounds after layout/transform: `Element::Render()` adds bounds when `damage_needs_new_bounds` is set

Global/full redraw triggers:

- context resize: `Context::SetDimensions()`
- DPR change: `Context::SetDensityIndependentPixelRatio()`
- document load/unload
- font changes: `Element::DirtyFontFaceRecursive()`

## How bounds are computed

`Element::GetDamageBounds()` returns conservative bounds in screen/context coordinates:

- base: `ElementUtilities::GetBoundingBox(..., BoxArea::Auto)`
  - includes transforms and box-shadow (via BoxArea::Auto)
- fallback if transform is not resolvable: border box
- inflation: `ElementEffects::ExtendInkOverflowBounds()` adds filter ink overflow (blur/drop-shadow)

`last_painted_bounds` is stored per element and updated after rendering.

## Merge policy and full redraw heuristics

Damage rects are merged on insertion:

- `MergeOverlaps(max_rects, merge_distance_px)`
- default `merge_distance_px = 2`
- default `max_rects = 32`

Full redraw is suggested when:

- area% >= `damage_full_redraw_area_percent` (default 60%)
- or rect count >= `damage_full_redraw_rect_count` (default 64)

This is a suggestion; Core does not force it unless explicitly requested by
`RequestFullRedraw()` or other global triggers.

## Notes

- Damage rects are clamped to the viewport in `AddDamageRect()`.
- `ClearRenderRequests()` clears both the render request and the damage list.
- Debug logging under `RMLUI_DEBUG_DAMAGE` is available to validate behavior.
