import {
    getMap,
    getMapSvg,
    getZones
} from "./api.js";

import {
    loadMap,
    initMapControls,
    initMapDragging
} from "./map.js";

import {
    renderZones,
    clearZoneSelection
} from "./zones.js";

import {
    updateSidebar,
    clearZoneSidebar
} from "./sidebar.js";

import {
    connectTags,
    clearTagSelection,
    selectTagById
} from "./tags.js";


// Управление боковой панелью
function initSidebarToggle() {
    const app = document.getElementById("app");
    const toggleButton = document.getElementById("sidebar-toggle");

    if (!app || !toggleButton) {
        console.error("Не найдены элементы боковой панели");
        return;
    }

    toggleButton.addEventListener("click", () => {
        const isCollapsed = app.classList.toggle("sidebar-collapsed");

        toggleButton.title = isCollapsed
            ? "Показать панель"
            : "Скрыть панель";

        toggleButton.textContent = isCollapsed ? "›" : "‹";
    });
}

let selectedZone = null;
let latestTags = [];
let selectedTagId = null;


/**
 * Обновляет содержимое боковой панели.
 */
function refreshSidebar() {
    updateSidebar({
        zone: selectedZone,
        tags: latestTags,
        selectedId: selectedTagId,
        onTagSelected: tag => {
            selectTagById(tag.tag_id);
        }
    });
}


// Инициализация приложения
async function init() {
    initSidebarToggle();

    try {
        const mapContainer = document.getElementById("map");

        if (!mapContainer) {
            throw new Error("Элемент #map не найден");
        }

        // Загружаем данные карты и зоны
        const [mapData, zones] = await Promise.all([
            getMap(),
            getZones()
        ]);

        // Загружаем SVG
        const svgText = await getMapSvg(mapData.file);

        const svg = await loadMap(
            mapContainer,
            svgText
        );

        // Подключаем масштабирование
        initMapControls();
        // Подключаем перемещение карты
        initMapDragging();

        // Отображаем зоны
        renderZones(
            svg,
            zones,
            zone => {
                selectedZone = zone;
                refreshSidebar();
            }
        );

        // Подключаем получение меток
        connectTags(
            svg,

            // Метка выбрана на карте.
            tag => {
                selectedTagId = tag.tag_id;
                refreshSidebar();

                console.log("Выбрана метка:", tag);
            },

            // Получен новый список меток от backend.
            (tags, selectedId) => {
                latestTags = tags;
                selectedTagId = selectedId;
                refreshSidebar();
            }
        );


        const mapViewport = document.querySelector(".map-viewport");

        mapViewport.addEventListener("click", event => {
            if (!event.target.closest(".map-zone")) {
                clearZoneSelection(svg);

                selectedZone = null;

                clearTagSelection();
                clearZoneSidebar();
            }
        });

        console.log("Карта и зоны успешно загружены");

    } catch (error) {
        console.error("Ошибка инициализации приложения:", error);

        const mapContainer = document.getElementById("map");

        if (mapContainer) {
            mapContainer.innerHTML = `
                <div style="
                    padding: 20px;
                    color: #c62828;
                    background: #ffebee;
                    border: 1px solid #ef9a9a;
                ">
                    Не удалось загрузить карту или зоны
                </div>
            `;
        }
    }
}


document.addEventListener("DOMContentLoaded", init);