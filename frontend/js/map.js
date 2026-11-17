let currentSvg = null;

let currentZoom = 1;

let panX = 0;
let panY = 0;

let isDragging = false;
let dragStartX = 0;
let dragStartY = 0;

let startPanX = 0;
let startPanY = 0;

let hasMoved = false;
let suppressClick = false;

const MIN_ZOOM = 0.5;
const MAX_ZOOM = 3;
const ZOOM_STEP = 0.2;
const DRAG_THRESHOLD = 4;


export async function loadMap(container, svgText) {
    container.innerHTML = svgText;

    currentSvg = container.querySelector("svg");

    if (!currentSvg) {
        throw new Error("В SVG не найден элемент <svg>");
    }

    currentZoom = 1;
    panX = 0;
    panY = 0;

    currentSvg.style.transformOrigin = "center center";

    updateTransform();

    return currentSvg;
}


// Обновление масштаба и положения карты
function updateTransform() {
    if (!currentSvg) {
        return;
    }

    currentSvg.style.transform =
        `translate(${panX}px, ${panY}px) scale(${currentZoom})`;
}


// Управление масштабированием
export function initMapControls() {
    const zoomInButton = document.getElementById("zoom-in");
    const zoomOutButton = document.getElementById("zoom-out");
    const zoomResetButton = document.getElementById("zoom-reset");

    if (!zoomInButton || !zoomOutButton || !zoomResetButton) {
        console.error("Не найдены кнопки управления масштабом");
        return;
    }

    zoomInButton.addEventListener("click", () => {
        currentZoom = Math.min(
            currentZoom + ZOOM_STEP,
            MAX_ZOOM
        );

        updateTransform();
    });

    zoomOutButton.addEventListener("click", () => {
        currentZoom = Math.max(
            currentZoom - ZOOM_STEP,
            MIN_ZOOM
        );

        updateTransform();
    });

    zoomResetButton.addEventListener("click", () => {
        currentZoom = 1;
        panX = 0;
        panY = 0;

        updateTransform();
    });
}


// Управление перемещением карты
export function initMapDragging() {
    const viewport = document.querySelector(".map-viewport");

    if (!viewport) {
        console.error("Не найдена область просмотра карты");
        return;
    }

    viewport.addEventListener("pointerdown", event => {
        // Используем только левую кнопку мыши
        if (event.button !== 0) {
            return;
        }

        // Не начинаем перемещение при нажатии на зону
        if (event.target.closest(".map-zone")) {
            return;
        }

        isDragging = true;
        hasMoved = false;

        dragStartX = event.clientX;
        dragStartY = event.clientY;

        startPanX = panX;
        startPanY = panY;

        viewport.classList.add("is-dragging");

        viewport.setPointerCapture(event.pointerId);
    });

    viewport.addEventListener("pointermove", event => {
        if (!isDragging) {
            return;
        }

        const deltaX = event.clientX - dragStartX;
        const deltaY = event.clientY - dragStartY;

        if (
            Math.abs(deltaX) > DRAG_THRESHOLD ||
            Math.abs(deltaY) > DRAG_THRESHOLD
        ) {
            hasMoved = true;
        }

        if (hasMoved) {
            panX = startPanX + deltaX;
            panY = startPanY + deltaY;

            updateTransform();
        }
    });

    function stopDragging(event) {
        if (!isDragging) {
            return;
        }

        isDragging = false;

        viewport.classList.remove("is-dragging");

        if (hasMoved) {
            suppressClick = true;
        }

        if (viewport.hasPointerCapture(event.pointerId)) {
            viewport.releasePointerCapture(event.pointerId);
        }
    }

    viewport.addEventListener("pointerup", stopDragging);
    viewport.addEventListener("pointercancel", stopDragging);

    // Не даём случайному клику после перемещения сбросить выбор зоны
    viewport.addEventListener("click", event => {
        if (suppressClick) {
            event.preventDefault();
            event.stopPropagation();
            suppressClick = false;
        }
    }, true);
}