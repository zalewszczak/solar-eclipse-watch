const http = require('http');

const PORT = 3000;

const HTML_PAGE = `<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
  <meta charset="UTF-8">
  <title>JS Source Table Editor</title>
  <style>
    :root[data-theme="dark"] {
      --bg: #1e1e2e;
      --card-bg: #282a36;
      --border: #44475a;
      --text: #f8f8f2;
      --accent: #bd93f9;
      --accent-hover: #ff79c6;
      --btn-green: #2ea043;
      --btn-green-hover: #3fb950;
      --btn-red: #da3633;
      --btn-red-hover: #f85149;
      --muted: #6272a4;
      --input-bg: #181825;
    }

    :root[data-theme="light"] {
      --bg: #f4f5f9;
      --card-bg: #ffffff;
      --border: #dcdfe6;
      --text: #2c3e50;
      --accent: #6272a4;
      --accent-hover: #44475a;
      --btn-green: #2ecc71;
      --btn-green-hover: #27ae60;
      --btn-red: #e74c3c;
      --btn-red-hover: #c0392b;
      --muted: #95a5a6;
      --input-bg: #ffffff;
    }

    * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, monospace; }
    body { background-color: var(--bg); color: var(--text); margin: 0; padding: 20px; transition: background-color 0.2s, color 0.2s; }
    
    .top-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }
    h1 { margin: 0; font-size: 1.5rem; color: var(--accent); }
    
    .container { display: flex; flex-direction: column; gap: 20px; max-width: 100%; }

    .panel {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 16px;
    }

    .panel-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 10px;
      flex-wrap: wrap;
      gap: 12px;
    }

    .panel-content {
      transition: all 0.2s ease;
    }
    .panel-content.collapsed {
      display: none;
    }

    textarea.src-input {
      width: 100%;
      height: 180px;
      background: var(--input-bg);
      color: var(--text);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 10px;
      font-family: monospace;
      font-size: 13px;
      resize: vertical;
    }

    .btn {
      background: var(--accent);
      color: #fff;
      border: none;
      padding: 6px 14px;
      border-radius: 4px;
      font-weight: bold;
      cursor: pointer;
      transition: background 0.2s;
    }
    .btn:hover { background: var(--accent-hover); }
    .btn-danger { background: var(--btn-red); color: #fff; }
    .btn-sm { padding: 4px 8px; font-size: 12px; }
    .btn-outline { background: transparent; border: 1px solid var(--border); color: var(--text); }
    .btn-outline:hover { background: var(--border); }

    .table-wrapper { overflow-x: auto; max-height: 75vh; position: relative; }

    table {
      width: 100%;
      border-collapse: separate;
      border-spacing: 0;
      font-size: 13px;
      text-align: left;
    }

    th, td {
      border-right: 1px solid var(--border);
      border-bottom: 1px solid var(--border);
      padding: 8px;
      vertical-align: top;
      white-space: normal;
      word-break: break-word;
      background: var(--card-bg);
    }

    th {
      background: var(--bg);
      position: sticky;
      top: 0;
      z-index: 10;
      border-top: 1px solid var(--border);
    }

    .resizer {
      position: absolute;
      right: 0;
      top: 0;
      bottom: 0;
      width: 8px;
      cursor: col-resize;
      user-select: none;
      z-index: 40;
    }
    .resizer:hover, .resizer.resizing {
      background: var(--accent);
      opacity: 0.7;
    }

    .col-header-box {
      display: flex;
      flex-direction: column;
      gap: 4px;
      width: 100%;
      overflow: hidden;
    }

    .col-header-top {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 4px;
    }

    .btn-hide {
      background: transparent;
      border: none;
      color: var(--muted);
      cursor: pointer;
      padding: 2px 4px;
      font-size: 12px;
      border-radius: 3px;
      line-height: 1;
    }
    .btn-hide:hover {
      background: var(--border);
      color: var(--text);
    }

    .hidden-cols-container {
      display: flex;
      align-items: center;
      gap: 6px;
      flex-wrap: wrap;
    }
    .hidden-col-pill {
      background: var(--input-bg);
      color: var(--text);
      border: 1px dashed var(--border);
      padding: 3px 8px;
      border-radius: 12px;
      font-size: 11px;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      gap: 4px;
      transition: all 0.15s;
    }
    .hidden-col-pill:hover {
      border-color: var(--accent);
      color: var(--accent);
      background: var(--card-bg);
    }

    .col-header-box input, .col-header-box select, select.cell-select {
      background: var(--input-bg);
      color: var(--text);
      border: 1px solid var(--border);
      padding: 6px;
      border-radius: 4px;
      width: 100%;
    }

    input[type="text"], input[type="number"] {
      background: var(--input-bg);
      color: var(--text);
      border: 1px solid var(--border);
      padding: 6px;
      border-radius: 4px;
      width: 100%;
    }

    textarea.cell-textarea {
      width: 100%;
      min-height: 48px;
      background: var(--input-bg);
      color: var(--text);
      border: 1px solid var(--border);
      border-radius: 4px;
      padding: 4px 6px;
      font-family: inherit;
      font-size: 12px;
      resize: vertical;
      white-space: pre-wrap;
      word-break: break-word;
    }

    .num-control { display: flex; align-items: center; gap: 4px; }
    .num-control button {
      background: var(--border);
      color: var(--text);
      border: none;
      width: 26px;
      height: 28px;
      border-radius: 4px;
      cursor: pointer;
      font-weight: bold;
    }
    .num-control button:hover { background: var(--accent); color: #fff; }
    .num-control input { text-align: center; }

    .bool-btn-group {
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .bool-main-btn {
      border: none;
      border-radius: 4px;
      padding: 5px 10px;
      font-size: 11px;
      font-weight: bold;
      cursor: pointer;
      color: #fff;
      flex: 1;
      text-align: center;
    }
    .bool-main-btn.true-state { background: var(--btn-green); }
    .bool-main-btn.true-state:hover { background: var(--btn-green-hover); }
    .bool-main-btn.false-state { background: var(--btn-red); }
    .bool-main-btn.false-state:hover { background: var(--btn-red-hover); }
    .bool-main-btn.null-state { background: var(--border); color: var(--muted); cursor: not-allowed; }

    .null-btn {
      border: none;
      border-radius: 3px;
      width: 24px;
      height: 24px;
      font-size: 11px;
      font-weight: bold;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .null-btn.null-active { background: var(--btn-red); color: #fff; }
    .null-btn.null-inactive { background: var(--border); color: var(--text); }
    .null-btn.null-inactive:hover { background: var(--muted); }

    .cat-btn-group {
      display: flex;
      flex-wrap: wrap;
      gap: 4px;
    }
    .cat-btn {
      border: none;
      border-radius: 4px;
      padding: 3px 8px;
      font-size: 11px;
      font-weight: bold;
      cursor: pointer;
      color: #fff;
      transition: opacity 0.15s;
    }
    .cat-btn.active { background: var(--btn-green); }
    .cat-btn.active:hover { background: var(--btn-green-hover); }
    .cat-btn.inactive { background: var(--btn-red); opacity: 0.7; }
    .cat-btn.inactive:hover { opacity: 1; }
  </style>
</head>
<body>

  <div class="top-header">
    <h1>JS Source Code Table Editor</h1>
    <button class="btn" id="themeBtn" onclick="toggleTheme()">🌙 Night Mode</button>
  </div>

  <div class="container">
    <div class="panel">
      <div class="panel-header">
        <strong>JS Table Source Code</strong>
        <div>
          <button class="btn btn-sm btn-outline" id="collapseBtn" onclick="toggleSourcePanel()">[-] Collapse Code</button>
          <button class="btn btn-sm" onclick="parseTextToTable()">Parse / Reload Table</button>
        </div>
      </div>
      <div class="panel-content" id="srcPanelContent">
        <textarea id="srcInput" class="src-input" spellcheck="false"></textarea>
      </div>
    </div>

    <div class="panel">
      <div class="panel-header">
        <div style="display: flex; align-items: center; gap: 16px; flex-wrap: wrap;">
          <strong>Data Table</strong>
          <label style="font-size: 12px; display: flex; align-items: center; gap: 6px; font-weight: normal;">
            Sticky Columns:
            <input type="number" id="stickyColInput" min="0" value="1" style="width: 55px; padding: 2px 6px; font-size: 12px;" onchange="updateStickyCols(this.value)">
          </label>
          <div class="hidden-cols-container" id="hiddenColsList"></div>
        </div>
        <button class="btn btn-sm" onclick="addRow()">+ Add Row</button>
      </div>
      <div class="table-wrapper">
        <table id="dataTable">
          <thead id="tableHead"></thead>
          <tbody id="tableBody"></tbody>
        </table>
      </div>
    </div>
  </div>

  <script>
    let rowsData = [];
    let columnsConfig = [];
    let stickyColCount = 1;
    let currentResizingCol = null;
    let startX = 0;
    let startWidth = 0;

    document.getElementById('srcInput').value = 's';

    // --- SWIPE BACK & NAVIGATION PROTECTION ---
    // Push dummy state to trap swipe back / history back gestures
    history.pushState({ page: 'editor' }, '', location.href);

    window.addEventListener('popstate', (e) => {
      const src = document.getElementById('srcInput').value;
      if (src && src.trim().length > 0) {
        const confirmLeave = confirm("You have unsaved changes in your table editor. Are you sure you want to leave?");
        if (!confirmLeave) {
          // Re-push state to keep trapping back/swipe actions
          history.pushState({ page: 'editor' }, '', location.href);
        } else {
          // Allow leaving
          history.back();
        }
      }
    });

    // Native Tab Close / Refresh protection
    window.addEventListener('beforeunload', (e) => {
      const src = document.getElementById('srcInput').value;
      if (src && src.trim().length > 0) {
        e.preventDefault();
        e.returnValue = '';
      }
    });

    function toggleTheme() {
      const current = document.documentElement.getAttribute('data-theme');
      const next = current === 'light' ? 'dark' : 'light';
      document.documentElement.setAttribute('data-theme', next);
      document.getElementById('themeBtn').innerText = next === 'light' ? '🌙 Night Mode' : '☀️ Day Mode';
    }

    function toggleSourcePanel() {
      const content = document.getElementById('srcPanelContent');
      const btn = document.getElementById('collapseBtn');
      if (content.classList.contains('collapsed')) {
        content.classList.remove('collapsed');
        btn.innerText = '[-] Collapse Code';
      } else {
        content.classList.add('collapsed');
        btn.innerText = '[+] Expand Code';
      }
    }

    function updateStickyCols(val) {
      stickyColCount = Math.max(0, parseInt(val, 10) || 0);
      renderTable();
    }

    function toggleColumnVisibility(cIdx) {
      columnsConfig[cIdx].hidden = !columnsConfig[cIdx].hidden;
      renderTable();
    }

    function parseTextToTable() {
      const src = document.getElementById('srcInput').value;
      const lines = src.split('\\n');
      rowsData = [];
      const keysSet = new Set();
      
      const existingColMap = new Map(columnsConfig.map(c => [c.key, c]));

      for (let line of lines) {
        let trimmed = line.trim();
        if (!trimmed || trimmed.startsWith('[') || trimmed === ']') continue;

        let comment = '';
        const commentIdx = line.indexOf('//');
        if (commentIdx !== -1) {
          comment = line.substring(commentIdx + 2).trim();
          line = line.substring(0, commentIdx);
        }

        line = line.trim();
        if (line.endsWith(',')) line = line.slice(0, -1).trim();

        if (line.startsWith('{') && line.endsWith('}')) {
          try {
            const obj = new Function('return ' + line)();
            obj._comment = comment;
            rowsData.push(obj);

            Object.keys(obj).forEach(k => {
              if (k !== '_comment') keysSet.add(k);
            });
          } catch (e) {
            console.error('Line parse error:', line, e);
          }
        }
      }

      columnsConfig = Array.from(keysSet).map(key => {
        const existing = existingColMap.get(key);
        let detectedType = existing ? existing.type : 'string';
        let width = existing ? existing.width : 160;
        let hidden = existing ? !!existing.hidden : false;

        if (!existing) {
          for (let r of rowsData) {
            const val = r[key];
            if (val === null || val === undefined) continue;
            if (Array.isArray(val)) { detectedType = 'categories'; break; }
            if (typeof val === 'number') { detectedType = 'number'; break; }
            if (typeof val === 'boolean') { detectedType = 'boolean'; break; }
          }
        }

        const catSet = new Set(existing ? existing.categoryOptions : []);
        rowsData.forEach(r => {
          const val = r[key];
          if (Array.isArray(val)) {
            val.forEach(c => catSet.add(String(c)));
          } else if (val !== null && val !== undefined && typeof val !== 'boolean') {
            catSet.add(String(val));
          }
        });

        return {
          key: key,
          type: detectedType,
          categoryOptions: Array.from(catSet),
          width: width,
          hidden: hidden
        };
      });

      columnsConfig.push({
        key: '_comment',
        type: 'comment',
        categoryOptions: [],
        width: 220,
        hidden: existingColMap.get('_comment')?.hidden || false
      });

      renderTable();
    }

    function renderTable() {
      const thead = document.getElementById('tableHead');
      const tbody = document.getElementById('tableBody');
      const hiddenContainer = document.getElementById('hiddenColsList');

      let hiddenHTML = '';
      columnsConfig.forEach((col, cIdx) => {
        if (col.hidden) {
          const name = col.key === '_comment' ? 'Comment' : col.key;
          hiddenHTML += \`<button class="hidden-col-pill" onclick="toggleColumnVisibility(\${cIdx})" title="Click to restore column">\${name} ✕</button>\`;
        }
      });
      hiddenContainer.innerHTML = hiddenHTML;

      const stickyInput = document.getElementById('stickyColInput');
      const visibleColsCount = columnsConfig.filter(c => !c.hidden).length;
      if (stickyInput) {
        stickyInput.max = visibleColsCount;
      }

      let leftOffsets = [];
      let currentLeft = 0;
      columnsConfig.forEach(col => {
        if (col.hidden) {
          leftOffsets.push(0);
          return;
        }
        leftOffsets.push(currentLeft);
        currentLeft += (col.width || 160);
      });

      let headHTML = '<tr>';
      let visibleIdx = 0;
      columnsConfig.forEach((col, cIdx) => {
        if (col.hidden) return;

        const showOptionsInput = col.type === 'categories' || col.type === 'predefined';
        const colWidth = col.width || 160;
        const isSticky = visibleIdx < stickyColCount;
        const leftPos = leftOffsets[cIdx];

        let stickyThStyle = '';
        if (isSticky) {
          stickyThStyle = \`position: sticky; left: \${leftPos}px; z-index: 35; background: var(--bg);\`;
          if (visibleIdx === 0) stickyThStyle += ' border-left: 1px solid var(--border);';
        }

        headHTML += \`
          <th style="width: \${colWidth}px; min-width: \${colWidth}px; \${stickyThStyle}">
            <div class="col-header-box">
              <div class="col-header-top">
                <strong>\${col.key === '_comment' ? 'Comment' : col.key}</strong>
                <button class="btn-hide" onclick="toggleColumnVisibility(\${cIdx})" title="Hide Column">👁️</button>
              </div>
              <select onchange="updateColType(\${cIdx}, this.value)">
                <option value="string" \${col.type === 'string' ? 'selected' : ''}>String</option>
                <option value="number" \${col.type === 'number' ? 'selected' : ''}>Number</option>
                <option value="boolean" \${col.type === 'boolean' ? 'selected' : ''}>Boolean</option>
                <option value="predefined" \${col.type === 'predefined' ? 'selected' : ''}>Predefined (Single-Select)</option>
                <option value="categories" \${col.type === 'categories' ? 'selected' : ''}>Categories (Multi-Select)</option>
                <option value="comment" \${col.type === 'comment' ? 'selected' : ''}>Comment</option>
              </select>
              \${showOptionsInput ? \`
                <input type="text" 
                       placeholder="Options (comma sep)" 
                       value="\${(col.categoryOptions || []).join(', ')}" 
                       onchange="updateCategoryOptions(\${cIdx}, this.value)" />
              \` : ''}
            </div>
            <div class="resizer" onmousedown="initResize(event, \${cIdx})"></div>
          </th>\`;
        
        visibleIdx++;
      });
      headHTML += '<th style="width: 70px;">Actions</th></tr>';
      thead.innerHTML = headHTML;

      let bodyHTML = '';
      rowsData.forEach((row, rIdx) => {
        bodyHTML += '<tr>';
        let rowVisibleIdx = 0;

        columnsConfig.forEach((col, cIdx) => {
          if (col.hidden) return;

          const val = row[col.key];
          const colWidth = col.width || 160;
          const isSticky = rowVisibleIdx < stickyColCount;
          const leftPos = leftOffsets[cIdx];

          let stickyTdStyle = '';
          if (isSticky) {
            stickyTdStyle = \`position: sticky; left: \${leftPos}px; z-index: 15; background: var(--card-bg);\`;
            if (rowVisibleIdx === 0) stickyTdStyle += ' border-left: 1px solid var(--border);';
          }

          bodyHTML += \`<td style="width: \${colWidth}px; min-width: \${colWidth}px; \${stickyTdStyle}">\`;

          if (col.type === 'number') {
            const numVal = (val !== undefined && val !== null) ? val : 0;
            bodyHTML += \`
              <div class="num-control">
                <button onclick="stepNumber(\${rIdx}, '\${col.key}', -1)">-</button>
                <input type="number" value="\${numVal}" oninput="updateCell(\${rIdx}, '\${col.key}', parseFloat(this.value) || 0)">
                <button onclick="stepNumber(\${rIdx}, '\${col.key}', 1)">+</button>
              </div>\`;
          } else if (col.type === 'boolean') {
            const isNull = (val === null || val === undefined);
            const isTrue = val === true;

            let label = 'NULL';
            let btnClass = 'null-state';
            if (!isNull) {
              label = isTrue ? 'TRUE' : 'FALSE';
              btnClass = isTrue ? 'true-state' : 'false-state';
            }

            bodyHTML += \`
              <div class="bool-btn-group">
                <button type="button" class="bool-main-btn \${btnClass}" 
                        \${isNull ? 'disabled' : ''} 
                        onclick="toggleBoolValue(\${rIdx}, '\${col.key}')">
                  \${label}
                </button>
                <button type="button" class="null-btn \${isNull ? 'null-active' : 'null-inactive'}" 
                        title="\${isNull ? 'Value is NULL (Click to enable boolean)' : 'Set value to NULL'}" 
                        onclick="toggleBoolNull(\${rIdx}, '\${col.key}')">✕</button>
              </div>\`;
          } else if (col.type === 'predefined') {
            const currentStr = (val === null || val === undefined) ? '' : String(val);
            const opts = col.categoryOptions || [];
            const allOpts = Array.from(new Set([...opts, ...(currentStr ? [currentStr] : [])]));
            let optionsHTML = '<option value="">-- None / Null --</option>';
            
            allOpts.forEach(opt => {
              const isSelected = currentStr === opt;
              optionsHTML += \`<option value="\${opt.replace(/"/g, '&quot;')}" \${isSelected ? 'selected' : ''}>\${opt}</option>\`;
            });

            bodyHTML += \`
              <select class="cell-select" onchange="updateCell(\${rIdx}, '\${col.key}', this.value === '' ? null : this.value)">
                \${optionsHTML}
              </select>\`;
          } else if (col.type === 'categories') {
            const selectedArr = Array.isArray(val) ? val : [];
            let buttonsHTML = (col.categoryOptions || []).map(opt => {
              const isActive = selectedArr.includes(opt);
              const safeOpt = opt.replace(/'/g, "\\\\'");
              return \`
                <button type="button" class="cat-btn \${isActive ? 'active' : 'inactive'}"
                        onclick="toggleCategory(\${rIdx}, '\${col.key}', '\${safeOpt}')">
                  \${opt}
                </button>\`;
            }).join('');

            bodyHTML += \`<div class="cat-btn-group">\${buttonsHTML}</div>\`;
          } else if (col.type === 'comment') {
            bodyHTML += \`<textarea class="cell-textarea" oninput="updateCell(\${rIdx}, '_comment', this.value)">\${row._comment || ''}</textarea>\`;
          } else {
            const strVal = (val === null || val === undefined) ? '' : val;
            bodyHTML += \`<textarea class="cell-textarea" oninput="updateCell(\${rIdx}, '\${col.key}', this.value)">\${strVal}</textarea>\`;
          }

          bodyHTML += '</td>';
          rowVisibleIdx++;
        });

        bodyHTML += \`<td><button class="btn btn-danger btn-sm" onclick="deleteRow(\${rIdx})">✕</button></td></tr>\`;
      });

      tbody.innerHTML = bodyHTML;
      updateSourceText();
    }

    function initResize(e, cIdx) {
      e.preventDefault();
      e.stopPropagation();
      currentResizingCol = cIdx;
      startX = e.clientX;
      startWidth = columnsConfig[cIdx].width || 160;
      
      document.addEventListener('mousemove', handleMouseMove);
      document.addEventListener('mouseup', handleMouseUp);
      e.target.classList.add('resizing');
    }

    function handleMouseMove(e) {
      if (currentResizingCol === null) return;
      const diff = e.clientX - startX;
      const newWidth = Math.max(90, startWidth + diff);
      columnsConfig[currentResizingCol].width = newWidth;
      renderTable();
    }

    function handleMouseUp() {
      if (currentResizingCol !== null) {
        document.querySelectorAll('.resizer').forEach(r => r.classList.remove('resizing'));
        currentResizingCol = null;
        document.removeEventListener('mousemove', handleMouseMove);
        document.removeEventListener('mouseup', handleMouseUp);
      }
    }

    function updateCell(rIdx, key, value) {
      rowsData[rIdx][key] = value;
      updateSourceText();
    }

    function toggleBoolValue(rIdx, key) {
      const current = rowsData[rIdx][key];
      if (current === true) {
        rowsData[rIdx][key] = false;
      } else {
        rowsData[rIdx][key] = true;
      }
      renderTable();
    }

    function toggleBoolNull(rIdx, key) {
      const current = rowsData[rIdx][key];
      if (current === null || current === undefined) {
        rowsData[rIdx][key] = false;
      } else {
        rowsData[rIdx][key] = null;
      }
      renderTable();
    }

    function stepNumber(rIdx, key, delta) {
      const current = parseFloat(rowsData[rIdx][key]) || 0;
      rowsData[rIdx][key] = current + delta;
      renderTable();
    }

    function toggleCategory(rIdx, key, option) {
      if (!Array.isArray(rowsData[rIdx][key])) {
        rowsData[rIdx][key] = [];
      }
      const arr = rowsData[rIdx][key];
      const idx = arr.indexOf(option);
      if (idx > -1) {
        arr.splice(idx, 1);
      } else {
        arr.push(option);
      }
      renderTable();
    }

    function updateColType(cIdx, newType) {
      const col = columnsConfig[cIdx];
      col.type = newType;

      if (newType === 'categories' || newType === 'predefined') {
        const extracted = new Set(col.categoryOptions || []);
        rowsData.forEach(r => {
          const v = r[col.key];
          if (Array.isArray(v)) {
            v.forEach(x => { if (x !== null && x !== undefined) extracted.add(String(x)); });
          } else if (typeof v === 'string' && v.trim() !== '') {
            if (newType === 'categories') {
              v.split(',').map(s => s.trim()).filter(Boolean).forEach(x => extracted.add(x));
            } else {
              extracted.add(v.trim());
            }
          } else if (v !== null && v !== undefined && typeof v !== 'boolean') {
            extracted.add(String(v));
          }
        });
        col.categoryOptions = Array.from(extracted);
      }
      renderTable();
    }

    function updateCategoryOptions(cIdx, val) {
      const opts = val.split(',').map(s => s.trim()).filter(Boolean);
      columnsConfig[cIdx].categoryOptions = opts;
      renderTable();
    }

    function addRow() {
      const newRow = {};
      columnsConfig.forEach(c => {
        if (c.type === 'categories') newRow[c.key] = [];
        else if (c.type === 'number') newRow[c.key] = 0;
        else if (c.type === 'boolean') newRow[c.key] = false;
        else if (c.type === 'comment') newRow._comment = '';
        else newRow[c.key] = '';
      });
      rowsData.push(newRow);
      renderTable();
    }

    function deleteRow(rIdx) {
      rowsData.splice(rIdx, 1);
      renderTable();
    }

    function updateSourceText() {
      const lines = rowsData.map((row) => {
        const parts = [];

        columnsConfig.forEach(col => {
          if (col.type === 'comment') return;
          const val = row[col.key];
          if (val === undefined) return;

          let valFormatted;
          if (val === null) {
            valFormatted = 'null';
          } else if (typeof val === 'string') {
            valFormatted = \`'\${val.replace(/'/g, "\\\\'")}'\`;
          } else if (Array.isArray(val)) {
            valFormatted = \`[\${val.map(v => \`'\${v}'\`).join(', ')}]\`;
          } else {
            valFormatted = String(val);
          }

          parts.push(\`\${col.key}: \${valFormatted}\`);
        });

        let lineStr = \`  { \${parts.join(', ')} }\`;
        lineStr += ',';

        if (row._comment) {
          lineStr += \` // \${row._comment}\`;
        }

        return lineStr;
      });

      document.getElementById('srcInput').value = \`[\n\${lines.join('\\n')}\n]\`;
    }

    parseTextToTable();
  </script>
</body>
</html>`;

const server = http.createServer((req, res) => {
  res.writeHead(200, { 'Content-Type': 'text/html' });
  res.end(HTML_PAGE);
});

server.listen(PORT, () => {
  console.log(`\n==================================================`);
  console.log(`🚀 JS Table Editor running at: http://localhost:${PORT}`);
  console.log(`==================================================\n`);
});
