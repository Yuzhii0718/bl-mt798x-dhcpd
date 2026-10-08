
function ajax(opt) {
    var xmlhttp;

    if (window.XMLHttpRequest) {
        //  IE7+, Firefox, Chrome, Opera, Safari
        xmlhttp = new XMLHttpRequest();
    } else {
        // IE6, IE5
        xmlhttp = new ActiveXObject('Microsoft.XMLHTTP');
    }

    xmlhttp.upload.addEventListener('progress', function (e) {
        if (opt.progress)
            opt.progress(e)
    })

    xmlhttp.onreadystatechange = function() {
        if (xmlhttp.readyState == 4 && xmlhttp.status == 200) {
            if (opt.done)
                opt.done(xmlhttp.responseText);
        }
    }

    if (opt.timeout)
        xmlhttp.timeout = opt.timeout;

    var method = 'GET';

    if (opt.data)
        method = 'POST'

    xmlhttp.open(method, opt.url);
    xmlhttp.send(opt.data);
}

function startup(){
    getversion();
    getmtdlayoutlist();
}

function getmtdlayoutlist() {
    ajax({
        url: '/getmtdlayout',
        done: function(mtd_layout_list) {
            if (mtd_layout_list == "error")
                return;

            var mtd_layout = mtd_layout_list.split(';');

            var curEl = document.getElementById('current_mtd_layout');
            if (curEl) {
                var cur = mtd_layout[0];
                curEl.innerHTML = cur ? "Current mtd layout: " + cur : "";
            }

            var e = document.getElementById('mtd_layout_label');
            var hasOptions = false;

            for (var i=1; i<mtd_layout.length; i++) {
                if (mtd_layout[i].length > 0) {
                    e.options.add(new Option(mtd_layout[i], mtd_layout[i]));
                    hasOptions = true;
                }
            }
            /* Preselect the layout in use */
            for (var j = 0; j < e.options.length; j++) {
                if (e.options[j].value === mtd_layout[0])
                    e.selectedIndex = j;
            }
            document.getElementById('mtd_layout').style.display = hasOptions ? '' : 'none';
        }
    })
}

function getversion() {
    var versionElem = document.getElementById("version");
    ajax({
        url: '/version',
        done: function (version) {
            if (!versionElem) return;
            versionElem.innerHTML = version + ' 💡Yuzhii';
        }
    })
    if (versionElem) {
        var projectInfo = document.createElement("div");
        projectInfo.id = "project-info";
        projectInfo.innerHTML = 'You can find more infomation about this project: <a href="https://github.com/Yuzhii0718/bl-mt798x-dhcpd" target="_blank">Github</a>';
        versionElem.parentNode.insertBefore(projectInfo, versionElem.nextSibling);
    }
}

function upload(name) {
    var file = document.getElementById('file').files[0]
    if (!file)
        return

    document.getElementById('form').style.display = 'none';
    document.getElementById('hint').style.display = 'none';

    var form = new FormData();
    form.append(name, file);

    var mtd_layout_list = document.getElementById('mtd_layout_label');
    if (mtd_layout_list && mtd_layout_list.options.length > 0) {
        var mtd_idx = mtd_layout_list.selectedIndex;
        form.append("mtd_layout", mtd_layout_list.options[mtd_idx].value);
    }

    ajax({
        url: '/upload',
        data: form,
        done: function(resp) {
            if (String(resp).split('\n')[0].trim() == 'fail') {
                location = '/fail.html';
            } else {
                const info = resp.split(' ');

                document.getElementById('size').style.display = 'block';
                document.getElementById('size').innerHTML = 'Size: ' + info[0];

                document.getElementById('md5').style.display = 'block';
                document.getElementById('md5').innerHTML = 'MD5: ' + info[1];

                if (info[2]) {
                    document.getElementById('mtd').style.display = 'block';
                    document.getElementById('mtd').innerHTML = 'MTD layout: ' + info[2];
                }

                document.getElementById('upgrade').style.display = 'block';
            }
        },
        progress: function(e) {
            var percentage = parseInt(e.loaded / e.total * 100)
            document.getElementById('bar').setAttribute('style', '--percent: ' + percentage);
        }
    })
}

function bytesToHuman(n) {
    var t = Number(n);
    if (!isFinite(t) || t < 0) return "";
    if (t >= 1024 * 1024 * 1024) return (t / (1024 * 1024 * 1024)).toFixed(2) + " GiB";
    if (t >= 1024 * 1024) return (t / (1024 * 1024)).toFixed(2) + " MiB";
    if (t >= 1024) return (t / 1024).toFixed(2) + " KiB";
    return String(Math.floor(t)) + " B";
}

function parseFilenameFromDisposition(h) {
    var t, i;
    if (!h) return "";
    t = /filename\s*=\s*"([^"]+)"/i.exec(h);
    if (t && t[1]) return t[1];
    i = /filename\s*=\s*([^;\s]+)/i.exec(h);
    if (i && i[1]) return i[1].replace(/^"|"$/g, "");
    return "";
}

function setBackupStatusOld(msg) {
    var el = document.getElementById('backup_status');
    if (el) el.innerHTML = msg || "";
}

function setBackupProgressOld(pct) {
    var bar = document.getElementById('bar');
    if (!bar) return;
    var v = Math.max(0, Math.min(100, parseInt(pct || 0)));
    bar.style.display = 'block';
    bar.style.setProperty('--percent', v);
}

function backupInitOld() {
    getversion();

    var mode = document.getElementById('backup_mode');
    var range = document.getElementById('backup_range');
    var target = document.getElementById('backup_target');
    var info = document.getElementById('backup_info');

    if (mode && range) {
        mode.onchange = function () {
            range.style.display = (mode.value === 'range') ? '' : 'none';
        };
    }

    ajax({
        url: '/backup/info',
        done: function (txt) {
            var data;
            try { data = JSON.parse(txt); } catch (e) { setBackupStatusOld('backup info parse failed'); return; }

            if (info) {
                var mmc = data.mmc && data.mmc.present ? ('MMC: ' + (data.mmc.vendor || '') + ' ' + (data.mmc.product || '')) : 'MMC: not present';
                var mtd = data.mtd && data.mtd.present ? ('MTD: ' + (data.mtd.model || '')) : 'MTD: not present';
                info.innerHTML = mmc + ' | ' + mtd;
            }

            if (target) {
                target.options.length = 0;
                var opt = document.createElement('option');
                opt.value = '';
                opt.textContent = '-- select --';
                target.appendChild(opt);

                if (data.mmc && data.mmc.present) {
                    var raw = document.createElement('option');
                    raw.value = 'mmc:raw';
                    raw.textContent = '[MMC] raw';
                    target.appendChild(raw);

                    if (data.mmc.parts && data.mmc.parts.length) {
                        data.mmc.parts.forEach(function (p) {
                            if (!p || !p.name) return;
                            var o = document.createElement('option');
                            o.value = 'mmc:' + p.name;
                            o.textContent = '[MMC] ' + p.name + (p.size ? ' (' + bytesToHuman(p.size) + ')' : '');
                            target.appendChild(o);
                        });
                    }
                }

                if (data.mtd && data.mtd.present && data.mtd.parts && data.mtd.parts.length) {
                    data.mtd.parts.forEach(function (p) {
                        if (!p || !p.name) return;
                        var o = document.createElement('option');
                        o.value = 'mtd:' + p.name;
                        o.textContent = '[MTD] ' + p.name + (p.size ? ' (' + bytesToHuman(p.size) + ')' : '');
                        target.appendChild(o);
                    });
                }

                if (target.options.length > 1) target.selectedIndex = 1;
            }
        }
    });
}

async function startBackupOld() {
    var mode = document.getElementById('backup_mode');
    var target = document.getElementById('backup_target');
    if (!mode || !target || !target.value) {
        alert('Please select a target');
        return;
    }

    var fd = new FormData();
    fd.append('mode', mode.value);
    fd.append('storage', 'auto');
    fd.append('target', target.value);

    if (mode.value === 'range') {
        var start = document.getElementById('backup_start');
        var end = document.getElementById('backup_end');
        if (!start || !end || !start.value || !end.value) {
            alert('Please input valid start/end');
            return;
        }
        fd.append('start', start.value);
        fd.append('end', end.value);
    }

    setBackupProgressOld(0);
    setBackupStatusOld('Starting...');

    try {
        var resp = await fetch('/backup/main', { method: 'POST', body: fd });
        if (!resp.ok) {
            setBackupStatusOld('HTTP error: ' + resp.status);
            return;
        }

        var len = resp.headers.get('Content-Length');
        var total = len ? parseInt(len, 10) : 0;
        var name = parseFilenameFromDisposition(resp.headers.get('Content-Disposition')) || 'backup.bin';
        var received = 0;
        var chunks = [];

        if (resp.body && resp.body.getReader) {
            var reader = resp.body.getReader();
            while (true) {
                var r = await reader.read();
                if (r.done) break;
                chunks.push(r.value);
                received += r.value.length;
                total ? setBackupProgressOld(received / total * 100) : setBackupProgressOld(0);
                setBackupStatusOld('Downloading: ' + bytesToHuman(received) + (total ? ' / ' + bytesToHuman(total) : ''));
            }
        } else {
            var buf = await resp.arrayBuffer();
            chunks.push(new Uint8Array(buf));
        }

        var blob = new Blob(chunks, { type: 'application/octet-stream' });
        var a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = name;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        setBackupProgressOld(100);
        setBackupStatusOld('Done: ' + name);
    } catch (e) {
        setBackupStatusOld('Failed: ' + (e && e.message ? e.message : String(e)));
    }
}

var consoleStateOld = {
    running: false,
    pollTimer: null,
    history: [],
    histPos: -1
};

function consoleInitOld() {
    getversion();

    var out = document.getElementById('console_out');
    var cmd = document.getElementById('console_cmd');
    var status = document.getElementById('console_status');
    var token = document.getElementById('console_token');

    function setStatus(t) {
        if (status) status.innerHTML = t || '';
    }

    function appendText(t) {
        if (!out || !t) return;
        out.textContent += t;
        out.scrollTop = out.scrollHeight;
    }

    async function pollOnce() {
        if (!consoleStateOld.running) return;
        try {
            var fd = new FormData();
            if (token && token.value) fd.append('token', token.value);
            var r = await fetch('/console/poll', { method: 'POST', body: fd });
            if (!r.ok) { setStatus('HTTP error: ' + r.status); return; }
            var txt = await r.text();
            var j;
            try { j = JSON.parse(txt); } catch (e) { setStatus('Parse error'); return; }
            if (j && j.data) appendText(j.data);
        } catch (e) {
            setStatus('Error: ' + (e && e.message ? e.message : String(e)));
        }
    }

    function schedulePoll() {
        if (consoleStateOld.pollTimer) clearTimeout(consoleStateOld.pollTimer);
        consoleStateOld.pollTimer = setTimeout(async function () {
            await pollOnce();
            schedulePoll();
        }, 300);
    }

    window.consoleSendOld = async function () {
        if (!cmd || !cmd.value) return;
        var line = String(cmd.value);
        cmd.value = '';
        consoleStateOld.history.unshift(line);
        if (consoleStateOld.history.length > 50) consoleStateOld.history.length = 50;
        consoleStateOld.histPos = -1;
        try {
            var fd = new FormData();
            fd.append('cmd', line);
            if (token && token.value) fd.append('token', token.value);
            setStatus('Running...');
            var r = await fetch('/console/exec', { method: 'POST', body: fd });
            var txt = await r.text();
            if (!r.ok) { setStatus('HTTP error: ' + r.status + (txt ? ': ' + txt : '')); return; }
            try {
                var j = JSON.parse(txt);
                setStatus('Return: ' + (j && typeof j.ret !== 'undefined' ? j.ret : '?'));
            } catch (e) {
                setStatus('Done.');
            }
        } catch (e) {
            setStatus('Error: ' + (e && e.message ? e.message : String(e)));
        }
    };

    window.consoleClearOld = async function () {
        try {
            var fd = new FormData();
            if (token && token.value) fd.append('token', token.value);
            var r = await fetch('/console/clear', { method: 'POST', body: fd });
            if (r.ok) {
                if (out) out.textContent = '';
                setStatus('Cleared.');
            } else {
                setStatus('HTTP error: ' + r.status);
            }
        } catch (e) {
            setStatus('Error: ' + (e && e.message ? e.message : String(e)));
        }
    };

    if (cmd) {
        cmd.addEventListener('keydown', function (e) {
            if (e.key === 'Enter') {
                e.preventDefault();
                window.consoleSendOld();
                return;
            }
            if (e.key === 'ArrowUp') {
                var h = consoleStateOld.history;
                if (!h || !h.length) return;
                consoleStateOld.histPos = Math.min(h.length - 1, consoleStateOld.histPos + 1);
                cmd.value = h[consoleStateOld.histPos] || '';
                e.preventDefault();
                return;
            }
            if (e.key === 'ArrowDown') {
                var h2 = consoleStateOld.history;
                if (!h2 || !h2.length) return;
                consoleStateOld.histPos = Math.max(-1, consoleStateOld.histPos - 1);
                cmd.value = consoleStateOld.histPos >= 0 ? (h2[consoleStateOld.histPos] || '') : '';
                e.preventDefault();
            }
        });
    }

    consoleStateOld.running = true;
    setStatus('Ready.');
    schedulePoll();
}

function envInitOld() {
    getversion();
    envRefreshOld();
}

function envRefreshOld() {
    var list = document.getElementById('env_list');
    var count = document.getElementById('env_count');
    var status = document.getElementById('env_status');

    function setStatus(t) { if (status) status.innerHTML = t || ''; }

    fetch('/env/list', { method: 'GET' })
        .then(function (r) { return r.text().then(function (t) { return { ok: r.ok, status: r.status, text: t }; }); })
        .then(function (res) {
            if (!res.ok) { setStatus('HTTP error: ' + res.status); return; }
            if (list) list.textContent = res.text || '';
            if (count) {
                var lines = res.text.split('\n');
                var c = 0;
                for (var i = 0; i < lines.length; i++) if (lines[i] && lines[i].indexOf('=') > 0) c++;
                count.innerHTML = 'Count: ' + c;
            }
            setStatus('Ready.');
        })
        .catch(function (e) {
            setStatus('Error: ' + (e && e.message ? e.message : String(e)));
        });
}

function envSetOld() {
    var name = document.getElementById('env_name');
    var value = document.getElementById('env_value');
    var status = document.getElementById('env_status');
    function setStatus(t) { if (status) status.innerHTML = t || ''; }

    if (!name || !name.value) { alert('Please input a variable name'); return; }
    var fd = new FormData();
    fd.append('name', name.value);
    fd.append('value', value ? value.value : '');
    fetch('/env/set', { method: 'POST', body: fd })
        .then(function (r) { return r.text().then(function (t) { return { ok: r.ok, status: r.status, text: t }; }); })
        .then(function (res) {
            if (!res.ok) { setStatus('Error: ' + (res.text || res.status)); return; }
            setStatus('Saved.');
            envRefreshOld();
        })
        .catch(function (e) { setStatus('Error: ' + (e && e.message ? e.message : String(e))); });
}

function envUnsetOld() {
    var name = document.getElementById('env_name');
    var status = document.getElementById('env_status');
    function setStatus(t) { if (status) status.innerHTML = t || ''; }

    if (!name || !name.value) { alert('Please input a variable name'); return; }
    if (!confirm('Delete variable ' + name.value + ' ?')) return;
    var fd = new FormData();
    fd.append('name', name.value);
    fetch('/env/unset', { method: 'POST', body: fd })
        .then(function (r) { return r.text().then(function (t) { return { ok: r.ok, status: r.status, text: t }; }); })
        .then(function (res) {
            if (!res.ok) { setStatus('Error: ' + (res.text || res.status)); return; }
            setStatus('Deleted.');
            envRefreshOld();
        })
        .catch(function (e) { setStatus('Error: ' + (e && e.message ? e.message : String(e))); });
}

function envResetOld() {
    var status = document.getElementById('env_status');
    function setStatus(t) { if (status) status.innerHTML = t || ''; }

    if (!confirm('Reset environment to defaults?')) return;
    fetch('/env/reset', { method: 'POST' })
        .then(function (r) { return r.text().then(function (t) { return { ok: r.ok, status: r.status, text: t }; }); })
        .then(function (res) {
            if (!res.ok) { setStatus('Error: ' + (res.text || res.status)); return; }
            setStatus('Reset done.');
            envRefreshOld();
        })
        .catch(function (e) { setStatus('Error: ' + (e && e.message ? e.message : String(e))); });
}

function envRestoreOld() {
    var file = document.getElementById('env_file');
    var status = document.getElementById('env_status');
    function setStatus(t) { if (status) status.innerHTML = t || ''; }

    if (!file || !file.files || !file.files.length) { alert('Please select an env file'); return; }
    if (!confirm('Restore environment from file?')) return;
    var fd = new FormData();
    fd.append('envfile', file.files[0]);
    fetch('/env/restore', { method: 'POST', body: fd })
        .then(function (r) { return r.text().then(function (t) { return { ok: r.ok, status: r.status, text: t }; }); })
        .then(function (res) {
            if (!res.ok) { setStatus('Error: ' + (res.text || res.status)); return; }
            setStatus('Restored.');
            envRefreshOld();
        })
        .catch(function (e) { setStatus('Error: ' + (e && e.message ? e.message : String(e))); });
}

function parseUserLenOld(n) {
    var t, i, r;
    if (!n) return null;
    if (n = String(n).trim(), n === "") return null;
    t = /^\s*(0x[0-9a-fA-F]+|\d+)\s*([a-zA-Z]*)\s*$/.exec(n);
    if (!t) return null;
    i = t[1].toLowerCase().indexOf("0x") === 0 ? parseInt(t[1], 16) : parseInt(t[1], 10);
    if (!isFinite(i) || i < 0) return null;
    r = (t[2] || "").toLowerCase();
    return r === "" ? i : r === "k" || r === "kb" || r === "kib" ? i * 1024 : null
}

function flashExtractBytesOld(text) {
    var bytes = [];
    if (!text) return bytes;
    var m = text.match(/[0-9a-fA-F]{2}/g);
    if (!m) return bytes;
    for (var i = 0; i < m.length; i++) bytes.push(parseInt(m[i], 16));
    return bytes;
}

function flashFormatHexLinesOld(bytes) {
    var out = [];
    for (var i = 0; i < bytes.length; i++) {
        if (i && i % 16 === 0) out.push("\n");
        out.push((bytes[i] < 16 ? "0" : "") + bytes[i].toString(16).toUpperCase());
        if (i % 16 !== 15 && i !== bytes.length - 1) out.push(" ");
    }
    return out.join("");
}

function flashFormatDataOld() {
    var data = document.getElementById('flash_data');
    if (!data) return;
    if (!confirm('Format hex data now? This will reflow and trim invalid characters.')) return;
    var bytes = flashExtractBytesOld(data.value || "");
    data.value = flashFormatHexLinesOld(bytes);
    setFlashStatusOld('Formatted.');
}

function flashFindLastBeforeOld(str, sub, limit) {
    var idx = -1, cur = str.indexOf(sub);
    while (cur !== -1 && cur < limit) {
        idx = cur;
        cur = str.indexOf(sub, cur + 1)
    }
    return idx
}

function flashParseBackupFilenameOld(name) {
    if (!name) return null;
    var rangeIdx = name.indexOf("_0x"), dashIdx, startStr, endStr, start, end;
    if (rangeIdx < 0) return null;
    dashIdx = name.indexOf("-0x", rangeIdx);
    if (dashIdx < 0) return null;
    startStr = name.slice(rangeIdx + 1, dashIdx);
    endStr = name.slice(dashIdx + 1);
    start = /^0x[0-9a-fA-F]+/.exec(startStr);
    end = /^0x[0-9a-fA-F]+/.exec(endStr);
    if (!start || !end) return null;
    start = parseInt(start[0], 16);
    end = parseInt(end[0], 16);
    if (!isFinite(start) || !isFinite(end) || end <= start) return null;
    var mtdIdx = flashFindLastBeforeOld(name, "_mtd_", rangeIdx);
    var mmcIdx = flashFindLastBeforeOld(name, "_mmc_", rangeIdx);
    var stypeIdx = mtdIdx >= 0 && mmcIdx >= 0 ? (mtdIdx > mmcIdx ? mtdIdx : mmcIdx) : (mtdIdx >= 0 ? mtdIdx : mmcIdx);
    if (stypeIdx < 0) return null;
    var storage = stypeIdx === mtdIdx ? "mtd" : "mmc";
    var seg = name.slice(stypeIdx + 5, rangeIdx);
    if (!seg) return null;
    var parts = seg.split("_");
    var target = parts[parts.length - 1];
    if (!target) return null;
    return { storage: storage, target: target, start: start, end: end }
}

function flashSelectTargetOld(val) {
    var sel = document.getElementById('flash_target'), i;
    if (!sel) return false;
    for (i = 0; i < sel.options.length; i++) if (sel.options[i].value === val) {
        sel.selectedIndex = i;
        return true;
    }
    return false;
}

function flashGetDeviceNameOld(storage) {
    var bi = window.APP_STATE && window.APP_STATE.backupinfo ? window.APP_STATE.backupinfo : null;
    var mmcName = '';
    var mtdName = '';
    if (bi && bi.mmc && bi.mmc.present) {
        mmcName = [bi.mmc.vendor || '', bi.mmc.product || ''].join(' ').trim();
        if (!mmcName) mmcName = 'MMC';
    }
    if (bi && bi.mtd && bi.mtd.present) {
        mtdName = (bi.mtd.model || '').trim();
        if (!mtdName) mtdName = 'MTD';
    }
    if (storage === 'mtd') return mtdName || 'MTD';
    if (storage === 'mmc') return mmcName || 'MMC';
    return mtdName || mmcName || 'device';
}

function flashBuildErasePlanOld() {
    var target = document.getElementById('flash_target');
    var startEl = document.getElementById('flash_start');
    var endEl = document.getElementById('flash_end');
    var v, seg, storage, tname, isRaw, startStr, endStr, hasStart, hasEnd, start, end;
    var detail, targetLabel;

    if (!target || !target.value)
        return { error: 'Please select a target' };

    v = String(target.value);
    seg = v.split(':');
    storage = seg.length > 1 ? seg[0] : 'auto';
    tname = seg.length > 1 ? seg.slice(1).join(':') : v;
    isRaw = tname === 'raw';

    startStr = startEl && startEl.value ? String(startEl.value).trim() : '';
    endStr = endEl && endEl.value ? String(endEl.value).trim() : '';
    hasStart = !!startStr;
    hasEnd = !!endStr;

    if (hasStart !== hasEnd)
        return { error: 'Please input valid start/end' };

    if (hasStart && hasEnd) {
        start = parseUserLenOld(startStr);
        end = parseUserLenOld(endStr);
        if (start === null || end === null || end <= start)
            return { error: 'Please input valid start/end' };
    }

    if (isRaw && !hasStart)
        return { error: 'Please input valid start/end (raw target requires range)' };

    targetLabel = isRaw ? '' : (tname + ' partition');
    if (hasStart)
        detail = isRaw ? ('0x' + start.toString(16) + '~0x' + end.toString(16)) :
            (targetLabel + ' is 0x' + start.toString(16) + '~0x' + end.toString(16));
    else
        detail = targetLabel;

    return {
        storage: storage,
        target: v,
        hasRange: hasStart,
        start: hasStart ? start : null,
        end: hasStart ? end : null,
        detail: detail,
        deviceName: flashGetDeviceNameOld(storage)
    };
}

function flashInitOld() {
    getversion();

    var target = document.getElementById('flash_target');
    var info = document.getElementById('flash_info');
    var backup = document.getElementById('flash_backup');
    var restoreInfo = document.getElementById('flash_restore_info');

    if (backup) {
        backup.onchange = function () {
            var f = backup.files && backup.files.length ? backup.files[0] : null;
            var d = f ? flashParseBackupFilenameOld(f.name) : null;
            if (!d) {
                if (restoreInfo) restoreInfo.innerHTML = 'No valid backup filename detected';
                return;
            }
            if (restoreInfo) restoreInfo.innerHTML = d.storage + ':' + d.target + ' 0x' + d.start.toString(16) + '-0x' + d.end.toString(16);
            flashSelectTargetOld(d.storage + ':' + d.target);
            var s = document.getElementById('flash_start');
            var e = document.getElementById('flash_end');
            s && (s.value = '0x' + d.start.toString(16));
            e && (e.value = '0x' + d.end.toString(16));
        };
    }

    ajax({
        url: '/backup/info',
        done: function (txt) {
            var data;
            try { data = JSON.parse(txt); } catch (e) { setFlashStatusOld('backup info parse failed'); return; }

            if (info) {
                var mmc = data.mmc && data.mmc.present ? ('MMC: ' + (data.mmc.vendor || '') + ' ' + (data.mmc.product || '')) : 'MMC: not present';
                var mtd = data.mtd && data.mtd.present ? ('MTD: ' + (data.mtd.model || '')) : 'MTD: not present';
                info.innerHTML = mmc + ' | ' + mtd;
            }

            if (target) {
                target.options.length = 0;
                var opt = document.createElement('option');
                opt.value = '';
                opt.textContent = '-- select --';
                target.appendChild(opt);

                if (data.mmc && data.mmc.present) {
                    var raw = document.createElement('option');
                    raw.value = 'mmc:raw';
                    raw.textContent = '[MMC] raw';
                    target.appendChild(raw);

                    if (data.mmc.parts && data.mmc.parts.length) {
                        data.mmc.parts.forEach(function (p) {
                            if (!p || !p.name) return;
                            var o = document.createElement('option');
                            o.value = 'mmc:' + p.name;
                            o.textContent = '[MMC] ' + p.name + (p.size ? ' (' + bytesToHuman(p.size) + ')' : '');
                            target.appendChild(o);
                        });
                    }
                }

                if (data.mtd && data.mtd.present && data.mtd.parts && data.mtd.parts.length) {
                    data.mtd.parts.forEach(function (p) {
                        if (!p || !p.name) return;
                        var o = document.createElement('option');
                        o.value = 'mtd:' + p.name;
                        o.textContent = '[MTD] ' + p.name + (p.size ? ' (' + bytesToHuman(p.size) + ')' : '');
                        target.appendChild(o);
                    });
                }

                if (target.options.length > 1) target.selectedIndex = 1;
            }
        }
    });
}

function setFlashStatusOld(msg) {
    var el = document.getElementById('flash_status');
    if (el) el.innerHTML = msg || '';
}

async function flashReadOld() {
    var target = document.getElementById('flash_target');
    var start = document.getElementById('flash_start');
    var end = document.getElementById('flash_end');
    var data = document.getElementById('flash_data');
    if (!target || !target.value) { alert('Please select a target'); return; }
    if (!start || !end || !start.value || !end.value) { alert('Please input valid start/end'); return; }
    try {
        setFlashStatusOld('Reading...');
        var fd = new FormData();
        fd.append('op', 'read');
        fd.append('storage', 'auto');
        fd.append('target', target.value);
        fd.append('start', start.value);
        fd.append('end', end.value);
        var r = await fetch('/flash/read', { method: 'POST', body: fd });
        var txt = await r.text();
        if (!r.ok) { setFlashStatusOld('HTTP error: ' + r.status + (txt ? ': ' + txt : '')); return; }
        var j;
        try { j = JSON.parse(txt); } catch (e) { setFlashStatusOld('Parse error'); return; }
        if (!j || !j.ok) { setFlashStatusOld('Error: ' + (j && j.error ? j.error : '')); return; }
        if (data) data.value = j.data || '';
        setFlashStatusOld('Done.');
    } catch (e) {
        setFlashStatusOld('Error: ' + (e && e.message ? e.message : String(e)));
    }
}

async function flashWriteOld() {
    var target = document.getElementById('flash_target');
    var start = document.getElementById('flash_start');
    var data = document.getElementById('flash_data');
    if (!target || !target.value) { alert('Please select a target'); return; }
    if (!start || !start.value) { alert('Please input valid start'); return; }
    if (!data || !data.value || !data.value.trim()) { alert('Please input hex data'); return; }
    if (!confirm('Write data to flash now?')) return;
    try {
        setFlashStatusOld('Writing...');
        var fd = new FormData();
        fd.append('op', 'write');
        fd.append('storage', 'auto');
        fd.append('target', target.value);
        fd.append('start', start.value);
        fd.append('data', data.value);
        var r = await fetch('/flash/write', { method: 'POST', body: fd });
        var txt = await r.text();
        if (!r.ok) { setFlashStatusOld('HTTP error: ' + r.status + (txt ? ': ' + txt : '')); return; }
        var j;
        try { j = JSON.parse(txt); } catch (e) { setFlashStatusOld('Parse error'); return; }
        if (!j || !j.ok) { setFlashStatusOld('Error: ' + (j && j.error ? j.error : '')); return; }
        setFlashStatusOld('Done.');
    } catch (e) {
        setFlashStatusOld('Error: ' + (e && e.message ? e.message : String(e)));
    }
}

async function flashEraseOld() {
    var plan = flashBuildErasePlanOld();
    if (plan.error) {
        alert(plan.error);
        return;
    }

    if (!confirm('Erase flash data now? This is dangerous.'))
        return;

    if (!confirm('Will erase [' + plan.deviceName + '] ' + plan.detail + ' confirm again?'))
        return;

    try {
        setFlashStatusOld('Erasing...');
        var fd = new FormData();
        fd.append('op', 'erase');
        fd.append('storage', 'auto');
        fd.append('target', plan.target);
        if (plan.hasRange) {
            fd.append('start', '0x' + plan.start.toString(16));
            fd.append('end', '0x' + plan.end.toString(16));
        }

        var r = await fetch('/flash/erase', { method: 'POST', body: fd });
        var txt = await r.text();
        if (!r.ok) {
            setFlashStatusOld('HTTP error: ' + r.status + (txt ? ': ' + txt : ''));
            return;
        }

        var j;
        try { j = JSON.parse(txt); } catch (e) {
            setFlashStatusOld('Parse error');
            return;
        }

        if (!j || !j.ok) {
            setFlashStatusOld('Error: ' + (j && j.error ? j.error : ''));
            return;
        }

        setFlashStatusOld('Done.');
    } catch (e) {
        setFlashStatusOld('Error: ' + (e && e.message ? e.message : String(e)));
    }
}

async function flashRestoreOld() {
    var target = document.getElementById('flash_target');
    var start = document.getElementById('flash_start');
    var end = document.getElementById('flash_end');
    var backup = document.getElementById('flash_backup');
    if (!backup || !backup.files || !backup.files.length) { alert('Please select a backup file'); return; }
    if (!confirm('Restore backup to flash now?')) return;
    try {
        var file = backup.files[0];
        var totalSize = file ? file.size : 0;
        var baseStart = start ? parseUserLenOld(start.value) : null;
        var baseEnd = end ? parseUserLenOld(end.value) : null;

        if ((baseStart === null || baseEnd === null) && file && file.name) {
            var parsed = flashParseBackupFilenameOld(file.name);
            if (parsed) {
                baseStart = parsed.start;
                baseEnd = parsed.end;
                if (target && !target.value && parsed.storage && parsed.target)
                    flashSelectTargetOld(parsed.storage + ':' + parsed.target);
                start && (start.value = '0x' + baseStart.toString(16));
                end && (end.value = '0x' + baseEnd.toString(16));
            }
        }

        if (target && !target.value) { alert('Please select a target'); return; }
        if (baseStart === null || baseEnd === null || baseEnd <= baseStart) {
            setFlashStatusOld('Please input valid start/end');
            return;
        }
        if ((baseEnd - baseStart) !== totalSize) {
            setFlashStatusOld('Please input valid start/end');
            return;
        }

        var chunkSize = 4 * 1024 * 1024;
        var useChunked = totalSize > chunkSize;

        function toHex(n) { return '0x' + n.toString(16); }

        function sendChunk(blob, chunkOffset, chunkEnd, totalSize, baseStart) {
            return new Promise(function (resolve, reject) {
                var fd = new FormData();
                fd.append('op', 'restore');
                fd.append('backup', blob, 'restore_chunk.bin');
                target && target.value && fd.append('target', target.value);
                fd.append('start', toHex(baseStart + chunkOffset));
                fd.append('end', toHex(baseStart + chunkEnd));
                fd.append('storage', 'auto');

                var xhr = new XMLHttpRequest();
                xhr.upload.onprogress = function (evt) {
                    if (!evt || !evt.lengthComputable) return;
                    var pct = Math.floor((chunkOffset + evt.loaded) / totalSize * 100);
                    setFlashStatusOld('Uploading... ' + pct + '%');
                };
                xhr.upload.onload = function () {
                    setFlashStatusOld('Restoring...');
                };
                xhr.onreadystatechange = function () {
                    if (xhr.readyState !== 4) return;
                    if (xhr.status !== 200) {
                        setFlashStatusOld('HTTP error: ' + xhr.status + (xhr.responseText ? ': ' + xhr.responseText : ''));
                        reject(new Error('http'));
                        return;
                    }
                    var j;
                    try { j = JSON.parse(xhr.responseText); } catch (e) {
                        setFlashStatusOld('Parse error');
                        reject(e);
                        return;
                    }
                    if (!j || !j.ok) {
                        setFlashStatusOld('Error: ' + (j && j.error ? j.error : ''));
                        reject(new Error('bad'));
                        return;
                    }
                    if (j.alert)
                        window.__flash_restore_alert = j.alert;
                    resolve();
                };
                xhr.open('POST', '/flash/restore');
                xhr.send(fd);
            });
        }

        setFlashStatusOld('Uploading... 0%');

        if (!useChunked) {
            await sendChunk(file, 0, totalSize, totalSize, baseStart);
        } else {
            var offset = 0;
            while (offset < totalSize) {
                var next = Math.min(offset + chunkSize, totalSize);
                var blob = file.slice(offset, next);
                await sendChunk(blob, offset, next, totalSize, baseStart);
                offset = next;
            }
        }

        setFlashStatusOld('Done.');
        alert(window.__flash_restore_alert || 'Backup restore completed.');
        window.__flash_restore_alert = '';
    } catch (e) {
        setFlashStatusOld('Error: ' + (e && e.message ? e.message : String(e)));
    }
}
