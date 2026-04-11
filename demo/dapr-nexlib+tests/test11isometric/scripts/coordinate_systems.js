
const THREE = require('./three.cjs');

const V2 = THREE.Vector2;

const log = console.log;
const abs = Math.abs;

// (u, v) grid coordinate system
// (x, y) view coordinate system - draws isometric view

// unitary vectors unitU, unitV, expressed in x,y coordinate system

const KX = 16;
const KY = 8

const unitU = new V2( KX, KY);
const unitV = new V2(-KX, KY);

log('xy_for_uv');

const xy_for_uv = (uv) => {
    const u = uv.x;
    const v = uv.y;
    const x = KX * (u - v);
    const y = KY * (u + v);
    return new V2(x, y);
}

for (let v = -2; v <= 2; v++) {
    for (let u = -2; u <= 2; u++) {
        const sum = abs(u) + abs(v);
        if (sum > 2) continue;
        const uv = new V2(u, v);
        const xy = xy_for_uv(uv);
        log(uv, '->', xy);
    }
}

log('-------------------------------------------------------')

log('xy_for_uv_alt');

const xy_for_uv_alt = (uv) => {
    const res = new V2();
    return res.addScaledVector(unitU, uv.x).addScaledVector(unitV, uv.y);
}

for (let v = -2; v <= 2; v++) {
    for (let u = -2; u <= 2; u++) {
        const sum = abs(u) + abs(v);
        if (sum > 2) continue;
        const uv = new V2(u, v);
        const xy = xy_for_uv_alt(uv);
        log(uv, '->', xy);
    }
}

log('-------------------------------------------------------')

let difcnt = 0;
for (let v = -2; v <= 2; v++) {
    for (let u = -2; u <= 2; u++) {
        const uv = new V2(u, v);
        const xy1 = xy_for_uv(uv);
        const xy2 = xy_for_uv(uv);
        if (!xy1.equals(xy2)) difcnt++;
    }
}
if (difcnt == 0) log("check: xy_for_uv EQUIVALENT to xy_for_uv_alt");
else             log("check: xy_for_uv NOT equivalent to xy_for_uv_alt");

log('-------------------------------------------------------')

const uv_for_xy = (xy) => {
    const x = xy.x;
    const y = xy.y;
    const u =  (x / (2 * KX)) + (y / (2 * KY));
    const v = -(x / (2 * KX)) + (y / (2 * KY));
    return new V2(u, v);
}

for (let v = -2; v <= 2; v++) {
    for (let u = -2; u <= 2; u++) {
        const sum = abs(u) + abs(v);
        if (sum > 2) continue;
        const uv = new V2(u, v);
        const xy = xy_for_uv(uv);
        const uv2 = uv_for_xy(xy);
        log(uv, '->', xy, '->', uv2);
    }
}

