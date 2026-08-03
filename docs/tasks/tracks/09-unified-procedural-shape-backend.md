# Task 09 - Unified Procedural Shape Backend

## Objective

Provide one backend-independent procedural shape API for TrackObj, DynTrackObj,
RulerObj, and future path-based objects. Callers should supply geometry plus a
template request and receive renderable meshes without knowing whether the
selected definition is a TSRE shape template or an Open Rails track profile.

## Motivation

The current callers repeat the same responsibilities:

- load and merge the TSRE and route-local ORTS catalogs;
- apply route-profile override precedence;
- resolve special values such as `DEFAULT`, `DISABLED`, and an absent request;
- choose `ProceduralShape` or `OrtsTrackProfileRenderer`;
- report diagnostics and generation failures;
- distinguish shared cached meshes from caller-owned meshes.

This makes it easy for UI selection and rendering support to diverge, as they
previously did for RulerObj.

## Proposed Design

Add a facade such as `ProceduralShapeBackend::getShape(...)` with overloads for
`QVector<TSection>`, `TrackShape`, and `ComplexLine`. Return a result object
which contains generated mesh pointers, ownership information, the resolved
template ID/backend, diagnostics, and an explicit fallback state.

The facade should own catalog precedence and template policy. World objects
should only invalidate the returned result and render it; they should not know
which parser or generator produced it.

## Acceptance Criteria

- TrackObj, DynTrackObj, and RulerObj use the same resolver and generator entry
  point.
- Route ORTS profiles override same-name global TSRE templates consistently.
- UI catalogs and render resolution use the same merged catalog.
- Cached TSRE meshes and newly allocated ORTS meshes have one explicit,
  testable lifetime contract.
- Existing static/hardcoded fallback behavior remains unchanged.
- Tests cover absent, default, disabled, valid, colliding, invalid, and missing
  template requests for every supported geometry input.

## Status

Proposal only. Keep separate backends in the current implementation until this
facade and its ownership contract are designed and tested.
