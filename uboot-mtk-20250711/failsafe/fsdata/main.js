/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 */

// Project/author constants (centralized for reuse)
const AUTHOR_HANDLE = "Yuzhii0718";
const AUTHOR_DISPLAY = "💡Yuzhii";
const GITHUB_USER_URL = "https://github.com/Yuzhii0718/";
const PROJECT_REPO_URL = "https://github.com/Yuzhii0718/bl-mt798x-dhcpd";

function normalizeLang(input) {
    if (!input) return "en";
    const lowerCaseLanguage = String(input).toLowerCase();
    return lowerCaseLanguage.indexOf("zh") === 0 ? "zh-cn" : "en";
}

function detectLang() {
    let storedLang, navigatorLanguages;
    try {
        storedLang = localStorage.getItem("lang");
        if (storedLang) return normalizeLang(storedLang);
    } catch (error) { }
    navigatorLanguages = [];
    if (navigator.languages && navigator.languages.length) {
        navigatorLanguages = navigator.languages;
    } else if (navigator.language) {
        navigatorLanguages = [navigator.language];
    }
    return normalizeLang(navigatorLanguages[0]);
}

function detectTheme() {
    try {
        const storedTheme = localStorage.getItem("theme");
        if (storedTheme) return storedTheme;
    } catch (error) { }
    return "auto";
}

function normalizeThemeMode(input) {
    if (!input) return "auto";
    const normalizedMode = String(input).toLowerCase().trim();
    return normalizedMode === "light" || normalizedMode === "dark" || normalizedMode === "auto" ? normalizedMode : "auto";
}

function isI18nAvailable() {
    return typeof I18N !== "undefined" && I18N;
}

function isI18nEnabled() {
    return APP_STATE.i18nEnabled !== false;
}

function t(key, fallback) {
    const languageCode = APP_STATE.lang || "en";
    if (!isI18nEnabled() || !isI18nAvailable())
        return fallback !== undefined ? fallback : key;
    return I18N[languageCode] && I18N[languageCode][key] !== undefined ? I18N[languageCode][key] : I18N.en && I18N.en[key] !== undefined ? I18N.en[key] : (fallback !== undefined ? fallback : key);
}

function applyI18n(rootNode) {
    const scope = rootNode || document;
    const enabled = isI18nEnabled() && isI18nAvailable();
    const textNodes = scope.querySelectorAll("[data-i18n]");
    for (let textIndex = 0; textIndex < textNodes.length; textIndex++) {
        const textNode = textNodes[textIndex];
        const key = textNode.getAttribute("data-i18n");
        if (!textNode.hasAttribute("data-i18n-fallback"))
            textNode.setAttribute("data-i18n-fallback", textNode.textContent || "");
        const fallbackText = textNode.getAttribute("data-i18n-fallback") || "";
        textNode.textContent = enabled ? t(key, fallbackText) : fallbackText;
    }
    const htmlNodes = scope.querySelectorAll("[data-i18n-html]");
    for (let htmlIndex = 0; htmlIndex < htmlNodes.length; htmlIndex++) {
        const htmlNode = htmlNodes[htmlIndex];
        const htmlKey = htmlNode.getAttribute("data-i18n-html");
        if (!htmlNode.hasAttribute("data-i18n-html-fallback"))
            htmlNode.setAttribute("data-i18n-html-fallback", htmlNode.innerHTML || "");
        const fallbackHtml = htmlNode.getAttribute("data-i18n-html-fallback") || "";
        htmlNode.innerHTML = enabled ? t(htmlKey, fallbackHtml) : fallbackHtml;
    }
    const attributeNodes = scope.querySelectorAll("[data-i18n-attr]");
    for (let attrIndex = 0; attrIndex < attributeNodes.length; attrIndex++) {
        const attributeNode = attributeNodes[attrIndex];
        const attributeSpec = attributeNode.getAttribute("data-i18n-attr");
        if (!attributeSpec) continue;
        const attributeParts = attributeSpec.split(":");
        if (attributeParts.length < 2) continue;
        const attributeName = attributeParts[0];
        const translationKey = attributeParts.slice(1).join(":");
        const fallbackKey = "data-i18n-attr-fallback-" + attributeName;
        if (!attributeNode.hasAttribute(fallbackKey))
            attributeNode.setAttribute(fallbackKey, attributeNode.getAttribute(attributeName) || "");
        const fallbackAttribute = attributeNode.getAttribute(fallbackKey) || "";
        attributeNode.setAttribute(attributeName, enabled ? t(translationKey, fallbackAttribute) : fallbackAttribute);
    }
}

function setLang(language) {
    APP_STATE.lang = normalizeLang(language);
    try {
        localStorage.setItem("lang", APP_STATE.lang);
    } catch (error) { }
    applyI18n(document);
    typeof backupRefreshI18n == "function" && APP_STATE.page === "backup" && backupRefreshI18n();
    typeof flashRefreshI18n == "function" && APP_STATE.page === "flash" && flashRefreshI18n();
    typeof renderSysInfo == "function" && renderSysInfo();
    updateDocumentTitle();
}

function updateThemeSelect() {
    const themeSelect = document.getElementById("theme_select");
    if (!themeSelect) return;
    themeSelect.value = APP_STATE.theme || "auto";
}

function setTheme(themeMode, options) {
    const resolvedOptions = options || {};
    const persistLocal = resolvedOptions.persistLocal !== false;
    const persistEnv = resolvedOptions.persistEnv === true;
    const silent = resolvedOptions.silent === true;
    APP_STATE.theme = normalizeThemeMode(themeMode || "auto");
    try {
        persistLocal && localStorage.setItem("theme", APP_STATE.theme);
    } catch (error) { }
    const rootElement = document.documentElement;
    if (window.__failsafeThemeApplyMode) {
        window.__failsafeThemeApplyMode(APP_STATE.theme, { silent: silent });
    } else {
        APP_STATE.theme === "auto" ? rootElement.removeAttribute("data-theme") : rootElement.setAttribute("data-theme", APP_STATE.theme);
    }
    updateThemeSelect();
    persistEnv && saveThemeMode(APP_STATE.theme);
}

const THEME_COLOR_ENV_KEY = "failsafe_theme_color";
const THEME_COLOR_CACHE_KEY = "failsafe_theme_color_cache";
const ACCENT_PRESETS = ["#2563eb", "#0ea5e9", "#10b981", "#f59e0b", "#ef4444", "#a855f7"];
const THEME_MODE_ENV_KEY = "failsafe_theme_mode";

function normalizeHexColor(input) {
    if (!input) return null;
    let value = String(input).trim();
    if (value === "") return null;
    if (value[0] === "#") value = value.slice(1);
    if (!/^[0-9a-fA-F]{3}$/.test(value) && !/^[0-9a-fA-F]{6}$/.test(value)) return null;
    const hex = value.length === 3 ? `#${value[0]}${value[0]}${value[1]}${value[1]}${value[2]}${value[2]}` : `#${value}`;
    return hex.toLowerCase();
}

function hexToRgb(hex) {
    const normalizedHex = normalizeHexColor(hex);
    if (!normalizedHex) return null;
    return {
        r: parseInt(normalizedHex.slice(1, 3), 16),
        g: parseInt(normalizedHex.slice(3, 5), 16),
        b: parseInt(normalizedHex.slice(5, 7), 16)
    };
}

function applyAccentVars(color) {
    const normalizedColor = normalizeHexColor(color);
    let rgb;
    let lighter;
    if (!normalizedColor) return false;
    rgb = hexToRgb(normalizedColor);
    if (!rgb) return false;
    const rootElement = document.documentElement;
    rootElement.style.setProperty("--primary", normalizedColor);
    rootElement.style.setProperty("--primary-rgb", `${rgb.r}, ${rgb.g}, ${rgb.b}`);
    lighter = blendColor(normalizedColor, "#ffffff", 0.28);
    rootElement.style.setProperty("--primary-2", lighter);
    ensureThemeColorMeta(normalizedColor);
    return true;
}

function blendColor(sourceHex, targetHex, ratio) {
    const sourceRgb = hexToRgb(sourceHex);
    const targetRgb = hexToRgb(targetHex);
    if (!sourceRgb || !targetRgb) return sourceHex;
    const red = Math.round(sourceRgb.r + (targetRgb.r - sourceRgb.r) * ratio);
    const green = Math.round(sourceRgb.g + (targetRgb.g - sourceRgb.g) * ratio);
    const blue = Math.round(sourceRgb.b + (targetRgb.b - sourceRgb.b) * ratio);
    return `#${red.toString(16).padStart(2, "0")}${green.toString(16).padStart(2, "0")}${blue.toString(16).padStart(2, "0")}`;
}

function ensureThemeColorMeta(color) {
    if (!color) return;
    let meta = document.querySelector("meta[name='theme-color']");
    if (!meta) {
        meta = document.createElement("meta");
        meta.setAttribute("name", "theme-color");
        document.head && document.head.appendChild(meta);
    }
    meta.setAttribute("content", color);
}

function updateAccentControls(color) {
    const colorPicker = document.getElementById("accent_color_picker");
    const colorInput = document.getElementById("accent_color_input");
    const normalizedColor = normalizeHexColor(color);
    if (colorPicker && normalizedColor) colorPicker.value = normalizedColor;
    if (colorInput && normalizedColor) colorInput.value = normalizedColor;
    const swatches = document.querySelectorAll(".color-swatch");
    for (let swatchIndex = 0; swatchIndex < swatches.length; swatchIndex++) {
        const swatch = swatches[swatchIndex];
        if (!swatch || !swatch.dataset) continue;
        if (normalizedColor && String(swatch.dataset.color || "").toLowerCase() === normalizedColor)
            swatch.classList.add("active");
        else
            swatch.classList.remove("active");
    }
}

function applyAccentColor(color) {
    const isApplied = applyAccentVars(color);
    if (!isApplied) return false;
    updateAccentControls(color);
    return true;
}

(function applyAccentFromCache() {
    try {
        const cachedColor = localStorage.getItem(THEME_COLOR_CACHE_KEY);
        if (cachedColor) applyAccentVars(cachedColor);
    } catch (error) { }
})();

async function saveThemeColor(color) {
    const normalizedColor = normalizeHexColor(color);
    if (!normalizedColor) return;
    try {
        localStorage.setItem(THEME_COLOR_CACHE_KEY, normalizedColor);
    } catch (error) { }
    try {
        const formData = new FormData();
        formData.append("color", normalizedColor);
        await fetch("/theme/set", { method: "POST", body: formData });
    } catch (error) { }
}

async function saveThemeMode(theme) {
    const normalizedMode = normalizeThemeMode(theme);
    try {
        localStorage.setItem("theme", normalizedMode);
    } catch (error) { }
    try {
        const formData = new FormData();
        formData.append("theme", normalizedMode);
        await fetch("/theme/set", { method: "POST", body: formData });
    } catch (error) { }
}

async function loadThemeColor() {
    let currentColor = null;
    let loadedFromEnv = false;
    try {
        const response = await fetch("/theme/get", { method: "GET" });
        if (response && response.ok) {
            const payload = await response.json();
            if (payload && payload.color) {
                currentColor = normalizeHexColor(payload.color);
                loadedFromEnv = !!currentColor;
            }
        }
    } catch (error) { }

    if (!currentColor) {
        try {
            currentColor = (getComputedStyle(document.documentElement).getPropertyValue("--primary") || "").trim();
            currentColor = normalizeHexColor(currentColor);
        } catch (error) { }
    }

    if (currentColor) {
        if (loadedFromEnv) applyAccentColor(currentColor);
        if (loadedFromEnv) {
            try {
                localStorage.setItem(THEME_COLOR_CACHE_KEY, currentColor);
            } catch (error) { }
        }
        updateAccentControls(currentColor);
    }
}

async function loadThemeMode() {
    let mode = null;
    try {
        const response = await fetch("/theme/get", { method: "GET" });
        if (response && response.ok) {
            const payload = await response.json();
            if (payload && payload.theme) mode = normalizeThemeMode(payload.theme);
        }
    } catch (error) { }

    if (mode) {
        setTheme(mode, { persistEnv: false, persistLocal: true, silent: true });
    }
}

function appendAccentControls(container) {
    if (!container) return;

    const row = document.createElement("div");
    row.className = "control-row control-row-color";

    const accentLabel = document.createElement("div");
    accentLabel.setAttribute("data-i18n", "control.accent");
    accentLabel.textContent = t("control.accent");
    row.appendChild(accentLabel);

    const picker = document.createElement("div");
    picker.className = "color-picker";

    const presets = document.createElement("div");
    presets.className = "color-presets";
    ACCENT_PRESETS.forEach((presetColor) => {
        const swatchButton = document.createElement("button");
        swatchButton.type = "button";
        swatchButton.className = "color-swatch";
        swatchButton.dataset.color = presetColor.toLowerCase();
        swatchButton.style.backgroundColor = presetColor;
        swatchButton.onclick = () => {
            applyAccentColor(presetColor);
            saveThemeColor(presetColor);
        };
        presets.appendChild(swatchButton);
    });

    const inputs = document.createElement("div");
    inputs.className = "color-inputs";

    const colorTextInput = document.createElement("input");
    colorTextInput.type = "text";
    colorTextInput.id = "accent_color_input";
    colorTextInput.setAttribute("data-i18n-attr", "placeholder:theme.color.placeholder");
    colorTextInput.placeholder = t("theme.color.placeholder");
    colorTextInput.addEventListener("change", () => {
        const normalizedColor = normalizeHexColor(colorTextInput.value);
        if (!normalizedColor) return;
        applyAccentColor(normalizedColor);
        saveThemeColor(normalizedColor);
    });

    const colorPicker = document.createElement("input");
    colorPicker.type = "color";
    colorPicker.id = "accent_color_picker";
    colorPicker.setAttribute("data-i18n-attr", "title:theme.color.custom");
    colorPicker.title = t("theme.color.custom");
    colorPicker.addEventListener("input", () => {
        applyAccentColor(colorPicker.value);
        saveThemeColor(colorPicker.value);
    });

    inputs.appendChild(colorTextInput);
    inputs.appendChild(colorPicker);

    picker.appendChild(presets);
    picker.appendChild(inputs);

    row.appendChild(picker);
    container.appendChild(row);
}

function ensureFavicon() {
    let link = document.querySelector("link[rel='icon']");
    if (!link) {
        link = document.createElement("link");
        link.setAttribute("rel", "icon");
        link.setAttribute("type", "image/svg+xml");
        link.setAttribute("href", "/favicon.svg");
        document.head && document.head.appendChild(link);
    } else {
        link.setAttribute("href", "/favicon.svg");
    }
}

function updateDocumentTitle() {
    if (!isI18nEnabled() || !isI18nAvailable())
        return;
    if (APP_STATE.page) {
        const titleKey = APP_STATE.page + ".title";
        if (I18N[APP_STATE.lang] && I18N[APP_STATE.lang][titleKey]) {
            document.title = t(titleKey);
            return;
        }
        APP_STATE.page === "flashing" ? document.title = t("flashing.title.in_progress") : APP_STATE.page === "booting" && (document.title = t("booting.title.in_progress"));
    }
}

function ensureBranding() {
    const versionNode = document.getElementById("version");
    if (!versionNode) return;

    // Remove an existing sibling brand node (if present)
    try {
        const nextSiblingNode = versionNode.nextElementSibling;
        if (nextSiblingNode && nextSiblingNode.classList && nextSiblingNode.classList.contains("brand") && nextSiblingNode.parentNode) {
            nextSiblingNode.parentNode.removeChild(nextSiblingNode);
        }
    } catch (e) { }

    // Ensure an inline brand label exists
    if (versionNode.querySelector && !versionNode.querySelector(".brand-inline")) {
        const brandNode = document.createElement("span");
        brandNode.className = "brand-inline";
        brandNode.textContent = AUTHOR_DISPLAY;
        versionNode.appendChild(document.createTextNode(" "));
        versionNode.appendChild(brandNode);
    }

    // Ensure project info block exists (don't duplicate)
    if (versionNode.querySelector && versionNode.querySelector("#project-info")) return;
    const projectInfo = document.createElement("div");
    projectInfo.id = "project-info";
    projectInfo.innerHTML = `You can find more infomation about this project: <a href="${PROJECT_REPO_URL}" target="_blank">Github</a>`;
    versionNode.appendChild(projectInfo);
}

function ensureSidebar() {
    const createNavLink = (path, i18nKey, navId) => {
        const link = document.createElement("a");
        link.className = "nav-link";
        link.href = path;
        link.setAttribute("data-nav-id", navId);

        const iconSpan = document.createElement("span");
        iconSpan.className = "dot";
        link.appendChild(iconSpan);

        const labelSpan = document.createElement("span");
        labelSpan.setAttribute("data-i18n", i18nKey);
        labelSpan.textContent = t(i18nKey);
        link.appendChild(labelSpan);

        // Normalize and check active
        let normalizedPath = path;
        if (normalizedPath !== "/" && normalizedPath.charAt(0) !== "/") normalizedPath = "/" + normalizedPath;
        const isActive = normalizedPath === currentPath || (normalizedPath === "/" && (currentPath === "/" || currentPath === "/index.html"));
        if (isActive) link.classList.add("active");
        return link;
    };

    const sidebar = document.getElementById("sidebar");
    if (!sidebar) return;

    // Avoid re-rendering
    if (sidebar.getAttribute("data-rendered") === "1") return;
    sidebar.setAttribute("data-rendered", "1");

    // Prepare current path
    let currentPath = (location && location.pathname) ? location.pathname : "";
    if (currentPath === "") currentPath = "/";

    // Clear existing content
    sidebar.innerHTML = "";

    // Branding
    const brandContainer = document.createElement("div");
    brandContainer.className = "sidebar-brand";
    const brandTitle = document.createElement("div");
    brandTitle.className = "title";
    brandTitle.setAttribute("data-i18n", "app.name");
    brandTitle.textContent = t("app.name");
    brandContainer.appendChild(brandTitle);
    sidebar.appendChild(brandContainer);

    // Controls (language, theme, accent)
    const controlsContainer = document.createElement("div");
    controlsContainer.className = "sidebar-controls";

    const languageRow = document.createElement("div");
    languageRow.className = "control-row";
    const languageLabel = document.createElement("div");
    languageLabel.setAttribute("data-i18n", "control.language");
    languageLabel.textContent = t("control.language");
    languageRow.appendChild(languageLabel);

    const languageSelect = document.createElement("select");
    languageSelect.id = "lang_select";
    languageSelect.innerHTML = '<option value="en">English</option><option value="zh-cn">简体中文</option>';
    languageSelect.value = APP_STATE.lang;
    languageSelect.onchange = function () { setLang(this.value); };
    languageRow.appendChild(languageSelect);
    controlsContainer.appendChild(languageRow);

    const themeRow = document.createElement("div");
    themeRow.className = "control-row";
    const themeLabel = document.createElement("div");
    themeLabel.setAttribute("data-i18n", "control.theme");
    themeLabel.textContent = t("control.theme");
    themeRow.appendChild(themeLabel);

    const themeSelect = document.createElement("select");
    themeSelect.id = "theme_select";
    const autoOption = document.createElement("option");
    autoOption.value = "auto";
    autoOption.setAttribute("data-i18n", "theme.auto");
    autoOption.textContent = t("theme.auto");
    const lightOption = document.createElement("option");
    lightOption.value = "light";
    lightOption.setAttribute("data-i18n", "theme.light");
    lightOption.textContent = t("theme.light");
    const darkOption = document.createElement("option");
    darkOption.value = "dark";
    darkOption.setAttribute("data-i18n", "theme.dark");
    darkOption.textContent = t("theme.dark");
    themeSelect.appendChild(autoOption);
    themeSelect.appendChild(lightOption);
    themeSelect.appendChild(darkOption);
    themeSelect.value = APP_STATE.theme;
    themeSelect.onchange = function () { setTheme(this.value, { persistEnv: true, persistLocal: true }); };
    themeRow.appendChild(themeSelect);
    controlsContainer.appendChild(themeRow);

    appendAccentControls(controlsContainer);
    sidebar.appendChild(controlsContainer);

    // Navigation
    const navContainer = document.createElement("div");
    navContainer.className = "nav";

    // Basic section
    const basicSection = document.createElement("div");
    basicSection.className = "nav-section";
    const basicTitle = document.createElement("div");
    basicTitle.className = "nav-section-title";
    basicTitle.setAttribute("data-i18n", "nav.basic");
    basicTitle.textContent = t("nav.basic");
    basicSection.appendChild(basicTitle);
    basicSection.appendChild(createNavLink("/", "nav.firmware", "firmware"));
    basicSection.appendChild(createNavLink("/uboot.html", "nav.uboot", "uboot"));
    navContainer.appendChild(basicSection);

    // Advanced section
    const advancedSection = document.createElement("div");
    advancedSection.className = "nav-section";
    const advancedTitle = document.createElement("div");
    advancedTitle.className = "nav-section-title";
    advancedTitle.setAttribute("data-i18n", "nav.advanced");
    advancedTitle.textContent = t("nav.advanced");
    advancedSection.appendChild(advancedTitle);
    advancedSection.appendChild(createNavLink("/bl2.html", "nav.bl2", "bl2"));
    const gptLink = createNavLink("/gpt.html", "nav.gpt", "gpt");
    gptLink.style.display = "none";
    advancedSection.appendChild(gptLink);
    const simgLink = createNavLink("/simg.html", "nav.simg", "simg");
    simgLink.style.display = "none";
    advancedSection.appendChild(simgLink);
    advancedSection.appendChild(createNavLink("/factory.html", "nav.factory", "factory"));
    advancedSection.appendChild(createNavLink("/initramfs.html", "nav.initramfs", "initramfs"));
    navContainer.appendChild(advancedSection);

    // System section
    const systemSection = document.createElement("div");
    systemSection.className = "nav-section";
    const systemTitle = document.createElement("div");
    systemTitle.className = "nav-section-title";
    systemTitle.setAttribute("data-i18n", "nav.system");
    systemTitle.textContent = t("nav.system");
    systemSection.appendChild(systemTitle);
    systemSection.appendChild(createNavLink("/backup.html", "nav.backup", "backup"));
    systemSection.appendChild(createNavLink("/flash.html", "nav.flash", "flash"));
    systemSection.appendChild(createNavLink("/env.html", "nav.env", "env"));
    systemSection.appendChild(createNavLink("/console.html", "nav.console", "console"));
    systemSection.appendChild(createNavLink("/reboot.html", "nav.reboot", "reboot"));
    navContainer.appendChild(systemSection);

    sidebar.appendChild(navContainer);

    applyI18n(sidebar);
    updateGptNavVisibility();
    updateSimgNavVisibility();
}

function ajax(request) {
    let xhr;
    let method;
    xhr = window.XMLHttpRequest ? new XMLHttpRequest : new ActiveXObject("Microsoft.XMLHTTP");
    xhr.upload.addEventListener("progress", function (event) {
        request.progress && request.progress(event);
    });
    xhr.onreadystatechange = function () {
        xhr.readyState == 4 && xhr.status == 200 && request.done && request.done(xhr.responseText);
    };
    request.timeout && (xhr.timeout = request.timeout);
    method = "GET";
    request.data && (method = "POST");
    xhr.open(method, request.url);
    method === "POST" && xhr.setRequestHeader("X-Requested-With", "XMLHttpRequest");
    xhr.send(request.data);
}

/* consoleInit moved to console_js.js */

/* envInit moved to env_js.js */

function appInit(pageName) {
    APP_STATE.page = pageName || "";
    APP_STATE.i18nEnabled = isI18nAvailable();
    APP_STATE.lang = detectLang();
    APP_STATE.theme = detectTheme();
    setTheme(APP_STATE.theme, { persistEnv: false, persistLocal: true, silent: true });
    setLang(APP_STATE.lang);
    ensureSidebar();
    ensureBranding();
    ensureFavicon();
    applyI18n(document);
    updateDocumentTitle();
    loadThemeColor();
    loadThemeMode();
    setTimeout(function () {
        document.body.classList.add("ready")
    }, 0);
    getversion();
    // Fetch system info and storage/partition info for display
    getSysInfo();
    getStorageInfoForSysinfo();
    // getCurrentMtdLayout();
    (pageName === "index" || pageName === "initramfs") && getmtdlayoutlist();
    pageName === "backup" && typeof backupInit === "function" && backupInit();
    pageName === "flash" && typeof flashInit === "function" && flashInit();
    pageName === "console" && typeof consoleInit === "function" && consoleInit();
    pageName === "env" && typeof envInit === "function" && envInit()

    const Yuzhii_VERSION = 'UBOOT-MTK-20250711';
    const Yuzhii_LINK = 'https://github.com/Yuzhii0718/';
    console.log('\n%c Yuzhii0718 ' + Yuzhii_VERSION + ' %c ' + Yuzhii_LINK + ' ', 'color: #fadfa3; background: #030307; padding:5px 0;', 'background: #fadfa3; padding:5px 0;');
}

function updateGptNavVisibility() {
    // Hide GPT update entry when no MMC is present (runtime detection).
    // If backupinfo is unavailable, keep it visible (fallback behavior).
    const gptNavLink = document.querySelector("#sidebar [data-nav-id='gpt']");
    if (!gptNavLink) return;
    const backupInfo = APP_STATE.backupinfo;
    if (!backupInfo || !backupInfo.mmc || typeof backupInfo.mmc.present === "undefined") {
        gptNavLink.style.display = "none";
        return;
    }
    gptNavLink.style.display = backupInfo.mmc.present === false ? "none" : "";
    console.warn("GPT nav visibility updated based on MMC presence:", backupInfo.mmc.present);
}

function updateSimgNavVisibility() {
    // Hide Single Image entry unless the page is actually served.
    const simgNavLink = document.querySelector("#sidebar [data-nav-id='simg']");
    if (!simgNavLink) return;
    simgNavLink.style.display = "none";

    // Avoid repeated probes.
    if (APP_STATE._simg_probe_done) return;
    APP_STATE._simg_probe_done = true;

    try {
        fetch("/simg.html?_probe=1", { method: "GET", cache: "no-store" })
            .then(function (response) {
                if (response && response.ok) {
                    simgNavLink.style.display = "";
                    return;
                }
                console.warn("SIMG probe HTTP status:", response ? response.status : "unknown");
                console.info("If SIMG feature is not enabled, this warning is expected.");
            })
            .catch(function () { });
    } catch (error) {
        console.warn("Unexpected error during SIMG probe:", error);
    }
}

function renderSysInfo() {
    const sysinfoContainer = document.getElementById("sysinfo");
    let sysinfoData;
    let boardInfo;
    let ramInfo;
    let mtdSummary;
    if (!sysinfoContainer) return;
    sysinfoData = APP_STATE.sysinfo;
    if (!sysinfoData) {
        sysinfoContainer.textContent = t("sysinfo.loading");
        return
    }
    boardInfo = sysinfoData.board || {};
    ramInfo = sysinfoData.ram || {};

    while (sysinfoContainer.firstChild) sysinfoContainer.removeChild(sysinfoContainer.firstChild);
    sysinfoContainer.classList.remove("sysinfo-expanded");

    const summary = document.createElement("div");
    summary.className = "sysinfo-summary";

    const boardLine = document.createElement("div");
    boardLine.className = "sysinfo-line";
    boardLine.textContent = t("sysinfo.board") + " " + (boardInfo.model || t("sysinfo.unknown"));
    summary.appendChild(boardLine);

    const ramLine = document.createElement("div");
    ramLine.className = "sysinfo-line";
    ramLine.textContent = t("sysinfo.ram") + " " + (ramInfo.size !== undefined && ramInfo.size !== null && ramInfo.size !== 0 ? bytesToHuman(ramInfo.size) : t("sysinfo.unknown"));
    summary.appendChild(ramLine);

    if (sysinfoData.storage && sysinfoData.storage.mtd_layout) {
        mtdSummary = sysinfoData.storage.mtd_layout || {};
        if (mtdSummary.current) {
            const curLayoutLine = document.createElement("div");
            curLayoutLine.className = "sysinfo-line";
            curLayoutLine.textContent = t("sysinfo.mtd.current", "MTD layout") + " " + mtdSummary.current;
            summary.appendChild(curLayoutLine);
        }
    }

    sysinfoContainer.appendChild(summary);

    const details = document.createElement("details");
    details.className = "sysinfo-details";

    const summaryNode = document.createElement("summary");
    summaryNode.textContent = t("sysinfo.more", "More info");
    details.appendChild(summaryNode);

    const extra = document.createElement("div");
    extra.className = "sysinfo-extra";

    if (sysinfoData.storage && sysinfoData.storage.mtd_layout) {
        if (mtdSummary.current_parts) {
            const curPartsLine = document.createElement("div");
            curPartsLine.className = "sysinfo-line sysinfo-mtdparts";
            curPartsLine.textContent = t("sysinfo.mtd.parts", "MTD parts") + " " + mtdSummary.current_parts;
            extra.appendChild(curPartsLine);
        }
    }

    if (sysinfoData.build_variant) {
        const variantLine = document.createElement("div");
        variantLine.className = "sysinfo-line";
        variantLine.textContent = t("sysinfo.variant", "Variant") + " " + sysinfoData.build_variant;
        extra.appendChild(variantLine);
    }

    if (boardInfo.compatible) {
        const compatLine = document.createElement("div");
        compatLine.className = "sysinfo-line";
        compatLine.textContent = t("sysinfo.compat", "Compatible") + " " + boardInfo.compatible;
        extra.appendChild(compatLine);
    }

    if (sysinfoData.storage && sysinfoData.storage.mtd_layout) {
        const mtdLayoutInfo = sysinfoData.storage.mtd_layout || {};
        const layouts = mtdLayoutInfo.layouts || [];
        if (layouts && layouts.length) {
            const layoutTitle = document.createElement("div");
            layoutTitle.className = "sysinfo-line sysinfo-section";
            layoutTitle.textContent = t("sysinfo.mtd.layouts", "MTD layouts");
            extra.appendChild(layoutTitle);

            const layoutList = document.createElement("ul");
            layoutList.className = "sysinfo-list";
            for (let layoutIndex = 0; layoutIndex < layouts.length; layoutIndex++) {
                const item = layouts[layoutIndex] || {};
                const entry = document.createElement("li");
                const parts = item.parts ? " " + item.parts : "";
                entry.textContent = (item.label || "-") + ":" + parts;
                layoutList.appendChild(entry);
            }
            extra.appendChild(layoutList);
        }
    }

    if (sysinfoData.storage && sysinfoData.storage.mmc && sysinfoData.storage.mmc.present) {
        const mmcInfo = sysinfoData.storage.mmc;
        const mmcTitle = document.createElement("div");
        mmcTitle.className = "sysinfo-line sysinfo-section";
        mmcTitle.textContent = t("sysinfo.mmc", "MMC partitions");
        extra.appendChild(mmcTitle);

        if (mmcInfo.parts && mmcInfo.parts.length) {
            const list = document.createElement("ul");
            list.className = "sysinfo-list";
            for (let partitionIndex = 0; partitionIndex < mmcInfo.parts.length; partitionIndex++) {
                const partition = mmcInfo.parts[partitionIndex];
                const listItem = document.createElement("li");
                const sizeText = partition.size ? bytesToHuman(partition.size) : t("sysinfo.unknown");
                listItem.textContent = (partition.name || "-") + " (" + sizeText + ")";
                list.appendChild(listItem);
            }
            extra.appendChild(list);
        } else {
            const empty = document.createElement("div");
            empty.className = "sysinfo-line";
            empty.textContent = t("sysinfo.mmc.none", "No partitions");
            extra.appendChild(empty);
        }
    }

    if (extra.childNodes.length) {
        details.appendChild(extra);
        sysinfoContainer.appendChild(details);

        const toggleExpanded = () => {
            details.open ? sysinfoContainer.classList.add("sysinfo-expanded") : sysinfoContainer.classList.remove("sysinfo-expanded");
        };
        details.addEventListener("toggle", toggleExpanded);
        toggleExpanded();
    }
}

function getSysInfo() {
    // Always fetch sysinfo into APP_STATE (used by features like backup filename),
    // but only render when the sysinfo element exists on current page.
    const sysinfoElement = document.getElementById("sysinfo");
    sysinfoElement && renderSysInfo();
    ajax({
        url: "/sysinfo",
        done: function (responseText) {
            try {
                APP_STATE.sysinfo = JSON.parse(responseText)
            } catch (error) {
                return
            }
            sysinfoElement && renderSysInfo()
        }
    })
}

async function ensureSysInfoLoaded() {
    // On pages without #sysinfo (e.g. backup.html), we still need board model.
    if (APP_STATE.sysinfo && APP_STATE.sysinfo.board && APP_STATE.sysinfo.board.model)
        return APP_STATE.sysinfo;

    if (APP_STATE._sysinfo_promise)
        return await APP_STATE._sysinfo_promise;

    APP_STATE._sysinfo_promise = (async function () {
        try {
            const response = await fetch("/sysinfo", { method: "GET" });
            if (!response || !response.ok) return null;
            const payload = await response.json();
            payload && (APP_STATE.sysinfo = payload);
            return payload;
        } catch (error) {
            return null;
        } finally {
            // allow retry later
            APP_STATE._sysinfo_promise = null;
        }
    })();

    return await APP_STATE._sysinfo_promise;
}

function getStorageInfoForSysinfo() {
    // Pull /backup/info to render current partition table in the sysinfo box
    if (APP_STATE.backupinfo) {
        updateGptNavVisibility();
        return;
    }
    ajax({
        url: "/backup/info",
        done: function (responseText) {
            try {
                APP_STATE.backupinfo = JSON.parse(responseText);
            } catch (error) { return; }
            updateGptNavVisibility();
            renderSysInfo();
        }
    });
}

function getCurrentMtdLayout() {
    // Get current mtd layout label if multi-layout is enabled
    ajax({
        url: "/getmtdlayout",
        done: function (resp) {
            if (!resp || resp === "error") return;
            const parts = resp.split(";");
            if (parts.length > 0 && parts[0]) {
                APP_STATE.mtd_layout_current = parts[0];
                renderSysInfo();
            }
        }
    });
}

function startup() {
    appInit("index")
}

function getmtdlayoutlist() {
    ajax({
        url: "/getmtdlayout",
        done: function (responseText) {
            let layoutNames, currentLayoutElement, chooseLayoutElement, layoutSelect, layoutIndex, layoutContainer;
            if (responseText != "error" && (layoutNames = responseText.split(";"), currentLayoutElement = document.getElementById("current_mtd_layout"), currentLayoutElement && (currentLayoutElement.innerHTML = t("label.current_mtd") + layoutNames[0]), chooseLayoutElement = document.getElementById("choose_mtd_layout"), chooseLayoutElement && (chooseLayoutElement.textContent = t("label.choose_mtd")), layoutSelect = document.getElementById("mtd_layout_label"), layoutSelect)) {
                for (layoutSelect.options.length = 0, layoutIndex = 1; layoutIndex < layoutNames.length; layoutIndex++) layoutNames[layoutIndex].length > 0 && layoutSelect.options.add(new Option(layoutNames[layoutIndex], layoutNames[layoutIndex]));
                layoutContainer = document.getElementById("mtd_layout");
                layoutContainer && (layoutContainer.style.display = "")
            }
        }
    })
}

function getversion() {
    ajax({
        url: "/version",
        done: function (versionText) {
            const versionElement = document.getElementById("version");
            versionElement && (versionElement.innerHTML = versionText);
            ensureBranding()
        }
    })
}

function upload(formFieldName) {
    const selectedFile = document.getElementById("file").files[0];
    let formElement, hintElement, progressBarElement, formData, layoutSelect, layoutIndex, selectedLayoutName;
    selectedFile && (selectedLayoutName = selectedFile.name || "", formElement = document.getElementById("form"), formElement && (formElement.style.display = "none"), hintElement = document.getElementById("hint"), hintElement && (hintElement.style.display = "none"), progressBarElement = document.getElementById("bar"), progressBarElement && (progressBarElement.style.display = "block"), formData = new FormData, formData.append(formFieldName, selectedFile), layoutSelect = document.getElementById("mtd_layout_label"), layoutSelect && layoutSelect.options.length > 0 && (layoutIndex = layoutSelect.selectedIndex, formData.append("mtd_layout", layoutSelect.options[layoutIndex].value)), ajax({
                url: "/upload",
                data: formData,
                done: function (responseText) {
                    let responseParts, sizeElement, md5Element, mtdElement, upgradeElement, filenameElement, md5InName, md5Hint, md5Ok, md5Match, md5Class;
                    responseText == "fail" ? location = "/fail.html" : (responseParts = responseText.split(" "), filenameElement = document.getElementById("filename"), filenameElement && selectedLayoutName && (filenameElement.style.display = "block", filenameElement.innerHTML = `<span class="filename-label">${t("label.file")}</span><span class="filename-value">${selectedLayoutName}</span>`), sizeElement = document.getElementById("size"), sizeElement && (sizeElement.style.display = "block", sizeElement.innerHTML = `${t("label.size")}${responseParts[0]}`), md5Element = document.getElementById("md5"), md5Match = selectedLayoutName ? /(?:^|[._-])md5-([0-9a-fA-F]{32})(?:$|[._-])/.exec(selectedLayoutName) : null, md5InName = md5Match && md5Match[1] ? md5Match[1] : "", md5Element && (md5Element.style.display = "block", md5Ok = responseParts[1] && md5InName && String(responseParts[1]).toLowerCase() === String(md5InName).toLowerCase(), md5Hint = md5InName ? (md5Ok ? t("md5.match") : t("md5.mismatch")) : "", md5Class = md5InName ? (md5Ok ? "md5-ok" : "md5-bad") : "", md5Element.innerHTML = `${t("label.md5")}${responseParts[1]}${md5Hint ? ` <span class="md5-status ${md5Class}">${md5Hint}</span>` : ""}`), mtdElement = document.getElementById("mtd"), mtdElement && responseParts[2] && (mtdElement.style.display = "block", mtdElement.innerHTML = `${t("label.mtd")}${responseParts[2]}`), upgradeElement = document.getElementById("upgrade"), upgradeElement && (upgradeElement.style.display = "block"))
        },
        progress: function (progressEvent) {
            if (progressEvent.total) {
                const percent = parseInt(progressEvent.loaded / progressEvent.total * 100);
                const progressElement = document.getElementById("bar");
                progressElement && (progressElement.style.display = "block", progressElement.style.setProperty("--percent", percent))
            }
        }
    }))
}

function bytesToHuman(bytes) {
    if (bytes === null || bytes === undefined) return "";
    const numericBytes = Number(bytes);
    if (!isFinite(numericBytes) || numericBytes < 0) return "";
    if (numericBytes >= 1024 * 1024 * 1024) return (numericBytes / (1024 * 1024 * 1024)).toFixed(2) + " GiB";
    if (numericBytes >= 1024 * 1024) return (numericBytes / (1024 * 1024)).toFixed(2) + " MiB";
    if (numericBytes >= 1024) return (numericBytes / 1024).toFixed(2) + " KiB";
    return String(Math.floor(numericBytes)) + " B";
}

function parseFilenameFromDisposition(dispositionHeader) {
    if (!dispositionHeader) return "";
    const quotedMatch = /filename\s*=\s*"([^"]+)"/i.exec(dispositionHeader);
    if (quotedMatch && quotedMatch[1]) return quotedMatch[1];
    const unquotedMatch = /filename\s*=\s*([^;\s]+)/i.exec(dispositionHeader);
    if (unquotedMatch && unquotedMatch[1]) return unquotedMatch[1].replace(/^"|"$/g, "");
    return "";
}

function sanitizeFilenameComponent(value) {
    return value ? String(value).replace(/[^a-zA-Z0-9._-]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 48) : ""
}

function getNowYYYYMMDD() {
    const now = new Date();
    const year = now.getFullYear();
    const month = now.getMonth() + 1;
    const day = now.getDate();
    return String(year) + String(month).padStart(2, "0") + String(day).padStart(2, "0")
}

function makeBackupDownloadName(originalName) {
    const boardModel = (APP_STATE.sysinfo && APP_STATE.sysinfo.board && APP_STATE.sysinfo.board.model) ? APP_STATE.sysinfo.board.model : "";
    const boardComponent = sanitizeFilenameComponent(boardModel) || "board";
    const dateStamp = getNowYYYYMMDD();
    let downloadName = String(originalName || "backup.bin");

    // Ensure it starts with backup_
    downloadName.indexOf("backup_") === 0 || (downloadName = "backup_" + downloadName.replace(/^_+/, ""));

    // Insert board right after backup_ if not already
    downloadName.indexOf("backup_" + boardComponent + "_") === 0 || (downloadName = downloadName.replace(/^backup_/, "backup_" + boardComponent + "_"));

    // Ensure .bin extension
    /\.[A-Za-z0-9]+$/.test(downloadName) || (downloadName = downloadName + ".bin");

    // Append date before extension if not already present
    /_\d{8}\.[A-Za-z0-9]+$/.test(downloadName) || (downloadName = downloadName.replace(/(\.[A-Za-z0-9]+)$/, "_" + dateStamp + "$1"));

    return downloadName
}

function parseUserLen(input) {
    if (!input) return null;
    input = String(input).trim();
    if (input === "") return null;
    const match = /^\s*(0x[0-9a-fA-F]+|\d+)\s*([a-zA-Z]*)\s*$/.exec(input);
    if (!match) return null;
    const rawNumber = match[1];
    const suffix = (match[2] || "").toLowerCase();
    const numericValue = rawNumber.toLowerCase().indexOf("0x") === 0 ? parseInt(rawNumber, 16) : parseInt(rawNumber, 10);
    if (!isFinite(numericValue) || numericValue < 0) return null;
    if (!suffix) return numericValue;
    if (suffix === "k" || suffix === "kb" || suffix === "kib") return Math.floor(numericValue * 1024);
    if (suffix === "m" || suffix === "mb" || suffix === "mib") return Math.floor(numericValue * 1024 * 1024);
    if (suffix === "g" || suffix === "gb" || suffix === "gib") return Math.floor(numericValue * 1024 * 1024 * 1024);
    return null;
}

/* flash logic moved to flash_js.js */

/* backup logic moved to backup_js.js */

APP_STATE = {
    lang: "en",
    theme: "auto",
    page: ""
}
