# Rive → NDI Pipeline Guide (Preview)

This guide is being prepared to document the workflow for streaming Rive animations to NDI using the YUP toolchain.

> [!NOTE]
> When configuring with `-DYUP_ENABLE_AUDIO_MODULES=OFF`, YUP automatically skips the audio-dependent console, app, graphics, and plugin samples as well as the CTest suite. This keeps the slimmed-down Rive→NDI workflow free from audio build requirements.

> [!TIP]
> Python wheels honour the same toggle. Set `YUP_ENABLE_AUDIO_MODULES=0` in your environment before running `python -m build python` (or `pip wheel`) to publish artifacts that exclude the audio stack. Switch it back to `1` if a consumer explicitly needs the legacy audio APIs.

Additional sections covering renderer setup, Python bindings, and NDI orchestration will be added soon.
