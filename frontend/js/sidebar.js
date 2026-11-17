const sidebarContent = document.getElementById("sidebar-content");

let currentZone = null;
let currentTags = [];
let selectedTagId = null;
let onTagSelectedCallback = null;


/**
 * Обновляет данные боковой панели.
 */
export function updateSidebar({
    zone = currentZone,
    tags = currentTags,
    selectedId = selectedTagId,
    onTagSelected = onTagSelectedCallback
} = {}) {
    currentZone = zone;
    currentTags = tags;
    selectedTagId = selectedId;
    onTagSelectedCallback = onTagSelected;

    renderSidebar();
}


/**
 * Очищает выбранную зону, но сохраняет список меток.
 */
export function clearZoneSidebar() {
    currentZone = null;
    renderSidebar();
}


/**
 * Совместимость со старым вызовом.
 * Теперь очищается только информация о зоне.
 */
export function clearSidebar() {
    clearZoneSidebar();
}


/**
 * Старый интерфейс выбора зоны.
 */
export function renderZoneSidebar(zone) {
    currentZone = zone;
    renderSidebar();
}


/**
 * Полная отрисовка боковой панели.
 */
function renderSidebar() {
    if (!sidebarContent) {
        return;
    }

    sidebarContent.innerHTML = "";

    renderZoneSection();
    renderTagsSection();
}


/**
 * Информация о выбранной зоне.
 */
function renderZoneSection() {
    const section = document.createElement("section");
    section.classList.add("sidebar-section", "zone-sidebar-section");

    const heading = document.createElement("h3");
    heading.classList.add("sidebar-section-title");
    heading.textContent = "Зона";

    section.appendChild(heading);

    if (!currentZone) {
        const empty = document.createElement("p");
        empty.classList.add("sidebar-empty");
        empty.textContent = "Выберите зону на карте";
        section.appendChild(empty);
        sidebarContent.appendChild(section);
        return;
    }

    const title = document.createElement("h2");
    title.classList.add("sidebar-entity-title");
    title.textContent = currentZone.name;

    section.appendChild(title);

    const shape = currentZone.shape || {};

    addInfoRow(section, "ID", currentZone.id);
    addInfoRow(section, "Тип", shape.type);
    addInfoRow(section, "X", shape.x);
    addInfoRow(section, "Y", shape.y);
    addInfoRow(section, "Ширина", shape.width);
    addInfoRow(section, "Высота", shape.height);

    sidebarContent.appendChild(section);
}


/**
 * Постоянный список обнаруженных меток.
 */
function renderTagsSection() {
    const section = document.createElement("section");
    section.classList.add("sidebar-section", "tags-sidebar-section");

    const heading = document.createElement("h3");
    heading.classList.add("sidebar-section-title");
    heading.textContent = `Метки (${currentTags.length})`;

    section.appendChild(heading);

    if (currentTags.length === 0) {
        const empty = document.createElement("p");
        empty.classList.add("sidebar-empty");
        empty.textContent = "Метки пока не обнаружены";
        section.appendChild(empty);
        sidebarContent.appendChild(section);
        return;
    }

    const list = document.createElement("div");
    list.classList.add("sidebar-tags-list");

    for (const tag of currentTags) {
        list.appendChild(createTagItem(tag));
    }

    section.appendChild(list);
    sidebarContent.appendChild(section);
}


/**
 * Создаёт элемент списка одной метки.
 */
function createTagItem(tag) {
    const item = document.createElement("details");
    item.classList.add("sidebar-tag-item");
    item.open = selectedTagId === tag.tag_id;

    if (selectedTagId === tag.tag_id) {
        item.classList.add("selected");
    }

    const summary = document.createElement("summary");
    summary.classList.add("sidebar-tag-summary");

    const name = document.createElement("span");
    name.classList.add("sidebar-tag-id");
    name.textContent = tag.tag_id;

    const status = document.createElement("span");
    status.classList.add("sidebar-tag-status");
    status.textContent = tag.status || "неизвестен";

    summary.appendChild(name);
    summary.appendChild(status);

    summary.addEventListener("click", event => {
        event.preventDefault();

        item.open = true;

        if (typeof onTagSelectedCallback === "function") {
            onTagSelectedCallback(tag);
        }
    });

    item.appendChild(summary);

    const details = document.createElement("div");
    details.classList.add("sidebar-tag-details");

    addInfoRow(details, "ID", tag.tag_id);
    addInfoRow(details, "Зона", tag.zone_name || tag.zone_id || "Не определена");
    addInfoRow(details, "Статус", tag.status || "Неизвестен");

    item.appendChild(details);

    return item;
}


/**
 * Добавляет строку с названием и значением.
 */
function addInfoRow(container, label, value) {
    if (value === undefined || value === null || value === "") {
        return;
    }

    const row = document.createElement("div");
    row.classList.add("info-row");

    const name = document.createElement("span");
    name.textContent = label;

    const content = document.createElement("strong");
    content.textContent = String(value);

    row.appendChild(name);
    row.appendChild(content);

    container.appendChild(row);
}