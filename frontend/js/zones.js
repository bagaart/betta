const SVG_NS = "http://www.w3.org/2000/svg";

let selectedZone = null;


export function renderZones(svg, zones, onZoneSelected) {
    // Удаляем старый слой зон, если он уже существует
    const oldLayer = svg.querySelector(".zones-layer");

    if (oldLayer) {
        oldLayer.remove();
    }

    const layer = document.createElementNS(SVG_NS, "g");
    layer.classList.add("zones-layer");

    zones.forEach(zone => {
        if (zone.shape.type !== "rectangle") {
            console.warn("Неподдерживаемая форма зоны:", zone);
            return;
        }

        const group = document.createElementNS(SVG_NS, "g");
        group.classList.add("map-zone");
        group.dataset.zoneId = zone.id;

        // Прямоугольник зоны
        const rectangle = document.createElementNS(SVG_NS, "rect");

        rectangle.classList.add("zone-rectangle");

        rectangle.setAttribute("x", zone.shape.x);
        rectangle.setAttribute("y", zone.shape.y);
        rectangle.setAttribute("width", zone.shape.width);
        rectangle.setAttribute("height", zone.shape.height);

        // Название зоны
        const label = document.createElementNS(SVG_NS, "text");

        label.classList.add("zone-label");

        label.setAttribute(
            "x",
            zone.shape.x + zone.shape.width / 2
        );

        label.setAttribute(
            "y",
            zone.shape.y + zone.shape.height / 2
        );

        label.setAttribute("text-anchor", "middle");
        label.setAttribute("dominant-baseline", "middle");

        label.textContent = zone.name;

        group.appendChild(rectangle);
        group.appendChild(label);

        // Выбор зоны
        group.addEventListener("click", event => {
            event.stopPropagation();

            selectZone(svg, zone);

            if (onZoneSelected) {
                onZoneSelected(zone);
            }
        });

        layer.appendChild(group);
    });

    svg.appendChild(layer);
}


function selectZone(svg, zone) {
    clearZoneSelection(svg);

    selectedZone = zone;

    const group = svg.querySelector(
        `.map-zone[data-zone-id="${zone.id}"]`
    );

    if (group) {
        group.classList.add("selected");
    }
}


export function clearZoneSelection(svg) {
    selectedZone = null;

    svg.querySelectorAll(".map-zone").forEach(group => {
        group.classList.remove("selected");
    });
}