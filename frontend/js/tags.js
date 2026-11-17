
const WS_PATH = "/ws/tags";

let socket = null;
let reconnectTimer = null;
let currentSvg = null;
let onTagSelectedCallback = null;
let onTagsUpdatedCallback = null;

let tags = new Map();
let selectedTagId = null;

const SVG_NS = "http://www.w3.org/2000/svg";


/**
 * Подключение к WebSocket и получение обновлений меток.
 */
export function connectTags(svg, onTagSelected, onTagsUpdated) {
    currentSvg = svg;
    onTagSelectedCallback = onTagSelected;
    onTagsUpdatedCallback = onTagsUpdated;

    disconnectSocket();

    const protocol = window.location.protocol === "https:"
        ? "wss:"
        : "ws:";

    const wsUrl = `${protocol}//${window.location.host}${WS_PATH}`;

    socket = new WebSocket(wsUrl);

    socket.addEventListener("open", () => {
        console.log("WebSocket меток подключён");
    });

    socket.addEventListener("message", (event) => {
        handleMessage(event.data);
    });

    socket.addEventListener("error", (error) => {
        console.error("Ошибка WebSocket меток:", error);
    });

    socket.addEventListener("close", () => {
        console.warn("WebSocket меток отключён");

        reconnectTimer = setTimeout(() => {
            if (currentSvg) {
                connectTags(
                    currentSvg,
                    onTagSelectedCallback,
                    onTagsUpdatedCallback
                );
            }
        }, 3000);
    });
}


/**
 * Закрывает текущее WebSocket-соединение.
 */
function disconnectSocket() {
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }

    if (socket) {
        const oldSocket = socket;
        socket = null;
        oldSocket.close();
    }
}


/**
 * Обрабатывает сообщение от backend.
 */
function handleMessage(rawData) {
    let message;

    try {
        message = JSON.parse(rawData);
    } catch (error) {
        console.error("Не удалось разобрать сообщение WebSocket:", error);
        return;
    }

    if (message.type !== "tags_update") {
        return;
    }

    if (!Array.isArray(message.tags)) {
        console.warn("В сообщении отсутствует массив tags");
        return;
    }

    tags.clear();

    for (const tag of message.tags) {
        if (!tag || !tag.tag_id) {
            continue;
        }

        tags.set(tag.tag_id, tag);
    }

    // Если выбранная метка исчезла из обновления,
    // снимаем выделение.
    if (!tags.has(selectedTagId)) {
        selectedTagId = null;
    }

    renderTags();

    console.log(`Получено меток: ${tags.size}`);

    if (typeof onTagsUpdatedCallback === "function") {
        onTagsUpdatedCallback(
            getTags(),
            selectedTagId
        );
    }
}


/**
 * Обновляет плашки меток внутри соответствующих зон.
 */
function renderTags() {
    if (!currentSvg) {
        return;
    }

    // Удаляем предыдущие слои плашек.
    currentSvg.querySelectorAll(".zone-tags-layer").forEach(layer => {
        layer.remove();
    });

    // Группируем метки по зонам.
    const tagsByZone = new Map();

    for (const tag of tags.values()) {
        if (!tag.zone_id) {
            continue;
        }

        if (!tagsByZone.has(tag.zone_id)) {
            tagsByZone.set(tag.zone_id, []);
        }

        tagsByZone.get(tag.zone_id).push(tag);
    }

    // Создаём плашки внутри каждой зоны.
    for (const [zoneId, zoneTags] of tagsByZone) {
        const zoneGroup = currentSvg.querySelector(
            `.map-zone[data-zone-id="${CSS.escape(zoneId)}"]`
        );

        if (!zoneGroup) {
            console.warn(`Не найдена зона для меток: ${zoneId}`);
            continue;
        }

        const layer = document.createElementNS(SVG_NS, "g");
        layer.classList.add("zone-tags-layer");

        const rectangle = zoneGroup.querySelector(".zone-rectangle");

        if (!rectangle) {
            continue;
        }

        const zoneX = Number(rectangle.getAttribute("x"));
        const zoneY = Number(rectangle.getAttribute("y"));
        const zoneWidth = Number(rectangle.getAttribute("width"));
        const zoneHeight = Number(rectangle.getAttribute("height"));

        const badgeWidth = 1.55;
        const badgeHeight = 0.42;
        const badgeGap = 0.12;

        const totalWidth =
            zoneTags.length * badgeWidth +
            (zoneTags.length - 1) * badgeGap;

        const startX = zoneX + (zoneWidth - totalWidth) / 2;
        const badgeY = zoneY + zoneHeight - badgeHeight - 0.15;

        zoneTags.forEach((tag, index) => {
            const badge = createTagBadge(
                tag,
                startX + index * (badgeWidth + badgeGap),
                badgeY,
                badgeWidth,
                badgeHeight
            );

            layer.appendChild(badge);
        });

        zoneGroup.appendChild(layer);
    }
}


/**
 * Создаёт одну плашку метки.
 */
function createTagBadge(tag, x, y, width, height) {
    const group = document.createElementNS(SVG_NS, "g");

    group.style.cursor = "pointer";
    group.setAttribute("pointer-events", "all");
    group.classList.add("tag-marker");
    group.dataset.tagId = tag.tag_id;

    group.classList.toggle(
        "selected",
        selectedTagId === tag.tag_id
    );

    group.classList.toggle(
        "lost",
        tag.status === "lost"
    );

    const background = document.createElementNS(SVG_NS, "rect");

    background.setAttribute("pointer-events", "all");
    background.setAttribute("x", String(x));
    background.setAttribute("y", String(y));
    background.setAttribute("width", String(width));
    background.setAttribute("height", String(height));
    background.setAttribute("rx", "0.08");

    background.setAttribute(
        "fill",
        tag.status === "lost" ? "#e5e7eb" : "#dbeafe"
    );

    background.setAttribute(
        "stroke",
        selectedTagId === tag.tag_id ? "#2563eb" : "#93c5fd"
    );

    background.setAttribute("stroke-width", "0.035");

    const label = document.createElementNS(SVG_NS, "text");

    label.setAttribute("pointer-events", "none");
    label.setAttribute("x", String(x + width / 2));
    label.setAttribute("y", String(y + height / 2));
    label.setAttribute("text-anchor", "middle");
    label.setAttribute("dominant-baseline", "middle");
    label.setAttribute("font-size", "0.22");
    label.setAttribute("font-weight", "600");
    label.setAttribute("fill", "#1e3a8a");

    label.textContent = tag.tag_id;

    group.appendChild(background);
    group.appendChild(label);

    group.addEventListener("click", event => {
        event.stopPropagation();
        selectTag(tag.tag_id);
    });

    return group;
}


/**
 * Выбирает метку.
 */
function selectTag(tagId) {
    const tag = tags.get(tagId);

    if (!tag) {
        return;
    }

    selectedTagId = tagId;

    renderTags();

    if (typeof onTagSelectedCallback === "function") {
        onTagSelectedCallback(tag);
    }

    if (typeof onTagsUpdatedCallback === "function") {
        onTagsUpdatedCallback(
            getTags(),
            selectedTagId
        );
    }
}

export function selectTagById(tagId) {
    selectTag(tagId);
}


/**
 * Снимает выделение с выбранной метки.
 */
export function clearTagSelection() {
    selectedTagId = null;
    renderTags();

    if (typeof onTagsUpdatedCallback === "function") {
        onTagsUpdatedCallback(
            getTags(),
            selectedTagId
        );
    }
}


/**
 * Возвращает текущий список меток.
 */
export function getTags() {
    return Array.from(tags.values());
}


/**
 * Возвращает одну метку по ID.
 */
export function getTagById(tagId) {
    return tags.get(tagId) ?? null;
}
