export async function getMap() {
    const response = await fetch("/api/map");

    if (!response.ok) {
        throw new Error(`Не удалось загрузить карту: ${response.status}`);
    }

    return response.json();
}


export async function getMapSvg(file) {
    const response = await fetch(file);

    if (!response.ok) {
        throw new Error(`Не удалось загрузить SVG: ${response.status}`);
    }

    return response.text();
}


export async function getZones() {
    const response = await fetch("/api/zones");

    if (!response.ok) {
        throw new Error(`Не удалось загрузить зоны: ${response.status}`);
    }

    return response.json();
}