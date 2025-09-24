"""NDI orchestration helpers for YUP's Rive renderer bindings.

This package is tailored for Windows 11 automation hosts that drive the
Direct3D 11 offscreen renderer exposed via :mod:`yup_rive_renderer` and forward
frames to the official `cyndilib` Python bindings for NDI®. The modules here
focus on:

* Managing multiple concurrent renderer instances and mapping their BGRA frame
  buffers into NDI senders without unnecessary copies.
* Translating renderer timing into the 100 ns timestamp domain that the NDI SDK
  expects, ensuring downstream receivers observe stable clocks.
* Providing optional control hooks so higher level REST or OSC layers can route
  play/pause/scene changes while keeping rendering and transport logic in one
  place.

Runtime expectations:

* The host must run Windows 11 with the Microsoft Visual Studio 2022 toolchain
  and GPU drivers capable of Direct3D 11 offscreen rendering.
* The :mod:`cyndilib` package (>=0.0.8) must be installed to publish NDI video.
* The :mod:`yup_rive_renderer` extension must be built from this repository with
  `YUP_ENABLE_AUDIO_MODULES=OFF` as documented in ``docs/rive_ndi_overview.md``.

The top-level API re-exports :class:`~yup_ndi.orchestrator.NDIOrchestrator`,
:class:`~yup_ndi.orchestrator.NDIStreamConfig`, and
:class:`~yup_ndi.orchestrator.NDIStreamMetrics` for convenience.
"""

from .orchestrator import NDIOrchestrator, NDIStreamConfig, NDIStreamMetrics

__all__ = ["NDIOrchestrator", "NDIStreamConfig", "NDIStreamMetrics"]
