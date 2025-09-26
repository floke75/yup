/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2025 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

(() => {
  "use strict";

  /**
   * RiveFrontend manages UI interactions for the temporary orchestration panel.
   * The class deliberately isolates DOM wiring so the eventual integration can
   * swap in framework-specific bindings without refactoring business logic.
   */
  class RiveFrontend {
    /**
     * @param {Document} rootDocument The document hosting the UI.
     */
    constructor(rootDocument) {
      this.doc = rootDocument;
      this.canvas = this.doc.getElementById("rivePreview");
      this.previewFallback = this.doc.getElementById("previewFallback");
      this.summaryElement = this.doc.getElementById("sessionSummary");
      this.artboardGroup = this.doc.getElementById("artboardGroup");
      this.artboardSelect = this.doc.getElementById("artboardSelect");
      this.animationSelect = this.doc.getElementById("animationSelect");
      this.copyButton = this.doc.getElementById("copySummaryButton");

      this.elements = {
        riveFileInput: this.doc.getElementById("riveFileInput"),
        widthInput: this.doc.getElementById("widthInput"),
        heightInput: this.doc.getElementById("heightInput"),
        frameRateInput: this.doc.getElementById("frameRateInput"),
        ndiNameInput: this.doc.getElementById("ndiNameInput"),
        ndiGroupInput: this.doc.getElementById("ndiGroupInput"),
        notesInput: this.doc.getElementById("notesInput")
      };

      this.state = {
        riveFile: null,
        riveFileInfo: null,
        artboards: [],
        selectedArtboard: null,
        animations: [],
        selectedAnimation: null,
        width: Number(this.elements.widthInput.value) || 1920,
        height: Number(this.elements.heightInput.value) || 1080,
        frameRate: Number(this.elements.frameRateInput.value) || 60,
        ndiName: this.elements.ndiNameInput.value.trim(),
        ndiGroups: this.elements.ndiGroupInput.value.trim(),
        notes: this.elements.notesInput.value.trim()
      };
    }

    /**
     * Sets up event listeners and renders the initial UI state.
     */
    init() {
      this.elements.riveFileInput.addEventListener("change", (event) => {
        const [file] = event.target.files ?? [];
        if (file) {
          this.handleRiveFileSelected(file).catch((error) => {
            console.error("Failed to inspect .riv file", error);
            this.setPreviewFallback(
              "Unable to load the .riv file. Check the browser console for details."
            );
            this.resetRiveMetadata();
            this.updateSummary();
          });
        } else {
          this.resetRiveMetadata();
          this.setPreviewFallback("Select a .riv file to initialise the preview.");
          this.updateSummary();
        }
      });

      const numericFields = [
        ["widthInput", "width"],
        ["heightInput", "height"],
        ["frameRateInput", "frameRate"]
      ];

      numericFields.forEach(([inputKey, stateKey]) => {
        this.elements[inputKey].addEventListener("input", (event) => {
          const value = Number(event.target.value);
          if (!Number.isFinite(value) || value <= 0) {
            return;
          }
          this.state[stateKey] = value;
          this.updateSummary();
        });
      });

      this.elements.ndiNameInput.addEventListener("input", (event) => {
        this.state.ndiName = event.target.value.trim();
        this.updateSummary();
      });

      this.elements.ndiGroupInput.addEventListener("input", (event) => {
        this.state.ndiGroups = event.target.value.trim();
        this.updateSummary();
      });

      this.elements.notesInput.addEventListener("input", (event) => {
        this.state.notes = event.target.value.trim();
        this.updateSummary();
      });

      this.artboardSelect.addEventListener("change", (event) => {
        this.state.selectedArtboard = event.target.value;
        this.refreshPreview();
        this.updateSummary();
      });

      this.animationSelect.addEventListener("change", (event) => {
        this.state.selectedAnimation = event.target.value;
        this.refreshPreview();
        this.updateSummary();
      });

      this.copyButton.addEventListener("click", () => {
        const summaryPayload = this.summaryElement.textContent;
        if (!summaryPayload) {
          return;
        }

        if (navigator.clipboard?.writeText) {
          navigator.clipboard.writeText(summaryPayload).catch((error) => {
            console.error("Clipboard copy failed", error);
          });
        } else {
          const helper = this.doc.createElement("textarea");
          helper.value = summaryPayload;
          helper.setAttribute("readonly", "readonly");
          helper.style.position = "absolute";
          helper.style.left = "-9999px";
          this.doc.body.appendChild(helper);
          helper.select();
          try {
            this.doc.execCommand("copy");
          } catch (error) {
            console.error("Fallback clipboard copy failed", error);
          } finally {
            helper.remove();
          }
        }
      });

      this.updateSummary();
      this.setPreviewFallback("Select a .riv file to initialise the preview.");
    }

    /**
     * Handles parsing and preview initialisation when the user selects a .riv file.
     * @param {File} file The uploaded .riv asset.
     */
    async handleRiveFileSelected(file) {
      this.state.riveFile = file;
      this.state.riveFileInfo = {
        name: file.name,
        sizeBytes: file.size,
        lastModified: file.lastModified
      };

      this.setPreviewFallback("Loading Rive runtime …");

      await this.populateRiveMetadata(file);
      this.updateSummary();
      await this.refreshPreview();
    }

    /**
     * Populates artboard and animation drop-downs when possible. Falls back to
     * minimal metadata when the runtime is unavailable (for example, while
     * working offline or when the CDN is blocked).
     * @param {File} file The uploaded .riv file.
     */
    async populateRiveMetadata(file) {
      if (typeof window.rive === "undefined" || !window.rive?.RiveFile) {
        this.resetRiveMetadata();
        this.setPreviewFallback(
          "Rive runtime unavailable. Preview is disabled, but settings can still be edited."
        );
        return;
      }

      const buffer = await file.arrayBuffer();
      const uint8Buffer = new Uint8Array(buffer);

      let riveFile;
      if (typeof window.rive.RiveFile === "function" && window.rive.RiveFile.fromRuntimeBuffer) {
        riveFile = await window.rive.RiveFile.fromRuntimeBuffer(uint8Buffer);
      } else if (typeof window.rive.load === "function") {
        // Older runtimes expose a convenience helper.
        riveFile = await window.rive.load({ buffer: uint8Buffer });
      } else {
        this.resetRiveMetadata();
        this.setPreviewFallback(
          "Rive runtime detected but unsupported. Preview disabled; metadata reset."
        );
        return;
      }

      const artboards = Array.isArray(riveFile?.artboardNames)
        ? riveFile.artboardNames
        : typeof riveFile?.artboardCount === "function"
        ? this.extractArtboardNames(riveFile)
        : [];

      this.state.artboards = artboards;
      this.state.selectedArtboard = artboards[0] ?? null;

      const animations = this.extractAnimationNames(riveFile, this.state.selectedArtboard);
      this.state.animations = animations;
      this.state.selectedAnimation = animations[0] ?? null;

      this.populateSelect(this.artboardSelect, artboards);
      this.populateSelect(this.animationSelect, animations);

      const shouldShowMetadata = artboards.length > 0 || animations.length > 0;
      this.artboardGroup.hidden = !shouldShowMetadata;

      if (!shouldShowMetadata) {
        this.setPreviewFallback(
          "File loaded, but artboard/animation metadata was not exposed by the runtime."
        );
      }
    }

    /**
     * Extracts artboard names when the runtime exposes only count/index APIs.
     * @param {object} riveFile The parsed Rive file instance.
     * @returns {string[]} Artboard names, if discoverable.
     */
    extractArtboardNames(riveFile) {
      if (typeof riveFile.artboardCount !== "function" || typeof riveFile.artboardByIndex !== "function") {
        return [];
      }
      const names = [];
      const count = riveFile.artboardCount();
      for (let index = 0; index < count; index += 1) {
        try {
          const artboard = riveFile.artboardByIndex(index);
          if (artboard?.name) {
            names.push(artboard.name);
          }
        } catch (error) {
          console.warn("Failed to resolve artboard name", error);
        }
      }
      return names;
    }

    /**
     * Extracts animation and state machine names from a parsed Rive file.
     * @param {object} riveFile The parsed file instance.
     * @param {string|null} artboardName The selected artboard name.
     * @returns {string[]} Available animation/state machine names.
     */
    extractAnimationNames(riveFile, artboardName) {
      if (!artboardName || typeof riveFile.artboardByName !== "function") {
        return [];
      }
      try {
        const artboard = riveFile.artboardByName(artboardName);
        if (!artboard) {
          return [];
        }

        const animationNames = [];
        if (typeof artboard.animationCount === "function" && typeof artboard.animationByIndex === "function") {
          const count = artboard.animationCount();
          for (let index = 0; index < count; index += 1) {
            const animation = artboard.animationByIndex(index);
            if (animation?.name) {
              animationNames.push(animation.name);
            }
          }
        }

        if (typeof artboard.stateMachineCount === "function" && typeof artboard.stateMachineByIndex === "function") {
          const count = artboard.stateMachineCount();
          for (let index = 0; index < count; index += 1) {
            const stateMachine = artboard.stateMachineByIndex(index);
            if (stateMachine?.name) {
              animationNames.push(stateMachine.name);
            }
          }
        }
        return animationNames;
      } catch (error) {
        console.warn("Failed to extract animation metadata", error);
        return [];
      }
    }

    /**
     * Populates a <select> element with the given entries.
     * @param {HTMLSelectElement} select The select element to update.
     * @param {string[]} entries The options to render.
     */
    populateSelect(select, entries) {
      select.innerHTML = "";
      entries.forEach((entry) => {
        const option = this.doc.createElement("option");
        option.value = entry;
        option.textContent = entry;
        select.appendChild(option);
      });
    }

    /**
     * Re-renders the JSON summary block to keep downstream systems aligned with
     * the current UI state.
     */
    updateSummary() {
      const payload = {
        ndi: {
          name: this.state.ndiName || "Unnamed Source",
          groups: this.state.ndiGroups ? this.state.ndiGroups.split(",").map((group) => group.trim()).filter(Boolean) : [],
          notes: this.state.notes || undefined
        },
        renderer: {
          width: this.state.width,
          height: this.state.height,
          frameRate: this.state.frameRate,
          artboard: this.state.selectedArtboard || undefined,
          animation: this.state.selectedAnimation || undefined
        },
        riveFile: this.state.riveFileInfo
      };

      this.summaryElement.textContent = JSON.stringify(payload, null, 2);
    }

    /**
     * Re-initialises the preview canvas if the runtime is available.
     */
    async refreshPreview() {
      if (!this.state.riveFile || typeof window.rive === "undefined" || !window.rive?.Rive) {
        this.setPreviewFallback(
          this.state.riveFile
            ? "Preview unavailable. The browser could not load the Rive runtime."
            : "Select a .riv file to initialise the preview."
        );
        return;
      }

      const arrayBuffer = await this.state.riveFile.arrayBuffer();
      const uint8Buffer = new Uint8Array(arrayBuffer);
      const artboard = this.state.selectedArtboard || undefined;
      const animation = this.state.selectedAnimation ? [this.state.selectedAnimation] : undefined;

      if (this.riveInstance) {
        this.riveInstance.cleanup();
        this.riveInstance = null;
      }

      try {
        this.riveInstance = new window.rive.Rive({
          buffer: uint8Buffer,
          canvas: this.canvas,
          autoplay: true,
          artboard,
          animations: animation,
          stateMachines: animation,
          fit: window.rive.Fit.CONTAIN,
          onLoad: () => {
            this.riveInstance.resizeDrawingSurfaceToCanvas();
            this.setPreviewFallback("");
          },
          onError: (error) => {
            console.error("Rive runtime reported an error", error);
            this.setPreviewFallback("Failed to render preview. Check the browser console for details.");
          }
        });
      } catch (error) {
        console.error("Unable to start Rive preview", error);
        this.setPreviewFallback("Failed to render preview. Check the browser console for details.");
      }
    }

    /**
     * Clears runtime-specific metadata when the user deselects a file.
     */
    resetRiveMetadata() {
      this.state.riveFile = null;
      this.state.riveFileInfo = null;
      this.state.artboards = [];
      this.state.selectedArtboard = null;
      this.state.animations = [];
      this.state.selectedAnimation = null;
      this.populateSelect(this.artboardSelect, []);
      this.populateSelect(this.animationSelect, []);
      this.artboardGroup.hidden = true;
      if (this.riveInstance) {
        this.riveInstance.cleanup();
        this.riveInstance = null;
      }
    }

    /**
     * Updates the preview fallback text and toggles visibility.
     * @param {string} message The fallback message to display.
     */
    setPreviewFallback(message) {
      if (!message) {
        this.previewFallback.hidden = true;
        this.previewFallback.textContent = "";
        return;
      }
      this.previewFallback.hidden = false;
      this.previewFallback.textContent = message;
    }
  }

  window.addEventListener("DOMContentLoaded", () => {
    const frontend = new RiveFrontend(document);
    frontend.init();
  });
})();
