# JS Table Editor – `font_lookup` Manager

An interactive, zero-dependency web interface designed to easily parse, view, and modify JavaScript object array tables—specifically tailored for editing the `font_lookup` table in `presets_lookups.js`.

---

## 🌟 Overview

Editing large configuration lookup arrays (like `font_lookup` in `presets_lookups.js`) directly in raw code can be error-prone and tedious. This tool provides a spreadsheet-like GUI directly in your browser while keeping your output formatted as valid JavaScript source code.

---

## ✨ Features

- **Automatic OS Day/Night Mode**: Automatically detects your operating system's color scheme preference (`prefers-color-scheme`) on load and updates dynamically if the system theme changes. Includes a manual dark/light mode toggle.
- **Continuous Local Auto-Save**: Saves changes to browser `localStorage` in real time. If you swipe back, swipe forward, or accidentally refresh the page, your work is fully restored automatically.
- **Active Row Highlighting**: Click anywhere on a row to highlight it across all cells (including sticky frozen columns) for better focus when working with wide tables.
- **Dedicated Field Controls**:
  - **Booleans**: Instant toggle buttons supporting `TRUE`, `FALSE`, and explicit `NULL` states.
  - **Numbers**: Numeric inputs with `+` / `-` increment buttons.
  - **Categories / Tags**: Interactive multi-select tag buttons for array properties (e.g., `['pebbleos', 'modern', 'tiny']`).
  - **Single-Select Dropdowns**: Predefined value options for repetitive string values.
  - **JS Line Comments**: Edits and preserves trailing `// inline comments` on each array object.
- **Table Customization**:
  - **Sticky Columns**: Pin key columns (like `id` or `label`) so they remain visible while scrolling horizontally.
  - **Column Resizing**: Drag column boundaries to adjust widths.
  - **Column Visibility**: Hide unneeded columns and unhide them anytime from pill indicators.

---

## 🚀 Requirements

- **Node.js** (v12.x or higher recommended)
- Any modern web browser

---

## 💻 Getting Started

### 1. Save Server File
Ensure your `server.js` file contains the server code.

### 2. Start the Server
Run the Node.js server from your terminal:

```bash
node server.js