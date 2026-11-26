# -*- coding: utf-8 -*-
"""Plantilla de impresion a escala 1:1 de las dos caras de cobre de una placa.

Sirve para comprobar sobre papel, antes de pedir la fabricacion, que los
componentes reales caen donde dice el trazado: se imprime, se apoya la pieza
encima y se mira si los pines coinciden con los taladros. Asi se valido el
conector de diez posiciones de PCB-CONTROL.

La cara inferior sale espejada —vista desde abajo—, que es la postura en la que
se montan sus componentes. Cada pagina lleva una barra de 50 mm: si al medirla
con un calibre no da 50,0, la impresora ha escalado y la plantilla no vale.

Los pads rectangulares se dibujan rectangulares, no como un circulo equivalente:
el pad cuadrado es la marca del pin 1 y es lo que permite orientar la pieza.

El vertido de cobre no se dibuja a proposito: llenaria la hoja de negro y
taparia lo unico que interesa, que son los taladros y la serigrafia.

El PDF se escribe a mano, sin librerias, para que esto siga funcionando en
cualquier maquina sin instalar nada.

    python3 plantilla_impresion.py <directorio-gerber> <salida.pdf> [nombre-placa]

Sin el tercer argumento, el nombre de la placa que se rotula en la hoja se
deduce del nombre del PDF.

El directorio es el del Gerber descomprimido, tal como lo exporta EasyEDA.
"""
import sys, re, os

MM = 72/25.4                  # puntos PostScript por milimetro
A4 = (595.276, 841.890)
MARGEN = 40.0
ANCHO_VERTIDO = 0.2032        # mm: el trazo con que EasyEDA raya el relleno

# --- lectura del Gerber -------------------------------------------------------

def leer_gerber(path):
    """Devuelve (trazos, flashes). Las regiones G36/G37 —el vertido— se ignoran."""
    ap = {}
    segs, flashes = [], []
    cur = None; x = y = 0.0; region = False
    fmt = (4, 6)
    for ln in open(path, encoding='utf-8', errors='replace'):
        ln = ln.strip()
        m = re.match(r'^%FSLAX(\d)(\d)Y\d\d\*%', ln)
        if m: fmt = (int(m.group(1)), int(m.group(2))); continue
        m = re.match(r'^%ADD(\d+)([CROP]),([^*]*)\*%', ln)
        if m:
            ap[int(m.group(1))] = (m.group(2), [float(v) for v in m.group(3).split('X') if v])
            continue
        if ln.startswith('G36'): region = True; continue
        if ln.startswith('G37'): region = False; continue
        m = re.match(r'^(?:G54)?D(\d+)\*?$', ln)
        if m and int(m.group(1)) >= 10: cur = int(m.group(1)); continue
        m = re.match(r'^(?:G0?[123])?(?:X(-?\d+))?(?:Y(-?\d+))?(?:I(-?\d+))?(?:J(-?\d+))?D0?([123])\*$', ln)
        if not m: continue
        esc = 10 ** fmt[1]
        nx = float(m.group(1))/esc if m.group(1) else x
        ny = float(m.group(2))/esc if m.group(2) else y
        op = m.group(5)
        if not region and cur in ap:
            forma, dims = ap[cur]
            if op == '1':
                segs.append((x, y, nx, ny, (dims[0] if dims else 0.1)/2))
            elif op == '3':
                if forma in ('R', 'O') and len(dims) >= 2:
                    flashes.append((nx, ny, 'R', dims[0], dims[1]))
                else:
                    d = dims[0] if dims else 0.1
                    flashes.append((nx, ny, 'C', d, d))
        x, y = nx, ny
    return segs, flashes

def leer_contorno(path):
    pts = []
    for ln in open(path):
        m = re.match(r'^G0?1?X(-?\d+)Y(-?\d+)D0?([12])\*$', ln.strip())
        if m: pts.append((int(m.group(1))/1e6, int(m.group(2))/1e6, m.group(3)))
    segs = []; ant = None
    for x, y, op in pts:
        if op == '1' and ant: segs.append((ant[0], ant[1], x, y))
        ant = (x, y)
    return segs

def leer_taladros(path):
    """Excellon. Devuelve (x, y, diametro) en milimetros."""
    if not os.path.exists(path): return []
    hs = []; dia = {}; cur = None; metric = True
    for ln in open(path, errors='replace'):
        ln = ln.strip()
        m = re.match(r'^T(\d+)C([\d.]+)$', ln)
        if m: dia[m.group(1)] = float(m.group(2)); continue
        if ln.startswith('METRIC'): metric = True; continue
        if ln.startswith('INCH'):   metric = False; continue
        m = re.match(r'^T(\d+)$', ln)
        if m: cur = m.group(1); continue
        m = re.match(r'^X(-?[\d.]+)Y(-?[\d.]+)$', ln)
        if m and cur in dia:
            x, y = float(m.group(1)), float(m.group(2))
            if not metric: x, y = x*25.4, y*25.4
            hs.append((x, y, dia[cur]))
    return hs

# --- dibujo -------------------------------------------------------------------

def circulo(cx, cy, r):
    k = 0.5523*r
    return (f"{cx+r:.3f} {cy:.3f} m "
            f"{cx+r:.3f} {cy+k:.3f} {cx+k:.3f} {cy+r:.3f} {cx:.3f} {cy+r:.3f} c "
            f"{cx-k:.3f} {cy+r:.3f} {cx-r:.3f} {cy+k:.3f} {cx-r:.3f} {cy:.3f} c "
            f"{cx-r:.3f} {cy-k:.3f} {cx-k:.3f} {cy-r:.3f} {cx:.3f} {cy-r:.3f} c "
            f"{cx+k:.3f} {cy-r:.3f} {cx+r:.3f} {cy-k:.3f} {cx+r:.3f} {cy:.3f} c ")

def pagina(cara, espejo, geo, placa):
    cobre, silk, out, hs, (X0, X1, Y0, Y1) = geo
    ancho, alto = X1-X0, Y1-Y0
    ox = (A4[0]-ancho*MM)/2
    oy = (A4[1]-alto*MM)/2 - 20
    def P(x, y):
        u = (X1-x) if espejo else (x-X0)
        return ox + u*MM, oy + (y-Y0)*MM
    c = ['q 1 J 1 j']
    segs, flashes = cobre[cara]
    c.append('0.45 0.45 0.45 RG')                               # pistas
    for x0, y0, x1, y1, r in segs:
        if abs(2*r - ANCHO_VERTIDO) < 0.002: continue           # el relleno, fuera
        c.append(f'{2*r*MM:.3f} w')
        a = P(x0, y0); b = P(x1, y1)
        c.append(f'{a[0]:.3f} {a[1]:.3f} m {b[0]:.3f} {b[1]:.3f} l S')
    c.append('0 0 0 rg')                                        # pads
    for x, y, forma, dx, dy in flashes:
        p = P(x, y)
        if forma == 'R':
            c.append(f'{p[0]-dx/2*MM:.3f} {p[1]-dy/2*MM:.3f} {dx*MM:.3f} {dy*MM:.3f} re f')
        else:
            c.append(circulo(p[0], p[1], dx/2*MM) + 'f')
    ss, _ = silk[cara]                                          # serigrafia
    c.append('0.55 0.55 0.55 RG')
    for x0, y0, x1, y1, r in ss:
        c.append(f'{max(2*r*MM, 0.3):.3f} w')
        a = P(x0, y0); b = P(x1, y1)
        c.append(f'{a[0]:.3f} {a[1]:.3f} m {b[0]:.3f} {b[1]:.3f} l S')
    c.append('1 1 1 rg 0 0 0 RG 0.3 w')                         # taladros
    for x, y, d in hs:
        p = P(x, y); c.append(circulo(p[0], p[1], d/2*MM) + 'B')
    c.append('0 0 0 RG 0.8 w')                                  # contorno
    for x0, y0, x1, y1 in out:
        a = P(x0, y0); b = P(x1, y1)
        c.append(f'{a[0]:.3f} {a[1]:.3f} m {b[0]:.3f} {b[1]:.3f} l S')
    bx, by = MARGEN, A4[1]-MARGEN-14                            # barra de calibracion
    c.append('0 0 0 RG 1.2 w')
    c.append(f'{bx:.2f} {by:.2f} m {bx+50*MM:.2f} {by:.2f} l S')
    for t in (0, 50):
        c.append(f'{bx+t*MM:.2f} {by-4:.2f} m {bx+t*MM:.2f} {by+4:.2f} l S')
    rotulo = ('SUPERIOR (vista desde arriba)' if cara == 'top'
              else 'INFERIOR (espejada: vista desde abajo)')
    c.append(f'BT /F1 9 Tf 0 0 0 rg {bx:.2f} {by+10:.2f} Td ({placa}  -  cara {rotulo}) Tj ET')
    c.append(f'BT /F1 7 Tf {bx:.2f} {by-14:.2f} Td (Esta barra debe medir 50,0 mm. '
             'Si no, la impresion NO esta a escala: imprimir al 100 %, sin ajustar a pagina.) Tj ET')
    c.append(f'BT /F1 7 Tf {bx:.2f} {by-24:.2f} Td (Placa {ancho:.1f} x {alto:.1f} mm. '
             'Sin vertido de cobre, para que se vean los taladros.) Tj ET')
    c.append('Q')
    return '\n'.join(c)

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    G = sys.argv[1].rstrip('/') + '/'
    salida = sys.argv[2]
    # el rotulo distingue las hojas cuando se imprimen varias placas a la vez
    placa = sys.argv[3] if len(sys.argv) > 3 else (
        os.path.basename(salida).rsplit('.', 1)[0].replace('-plantilla-impresion', ''))
    cobre = {n: leer_gerber(G+f) for n, f in
             (('top', 'Gerber_TopLayer.GTL'), ('bottom', 'Gerber_BottomLayer.GBL'))}
    silk = {}
    for n, f in (('top', 'Gerber_TopSilkscreenLayer.GTO'),
                 ('bottom', 'Gerber_BottomSilkscreenLayer.GBO')):
        silk[n] = leer_gerber(G+f) if os.path.exists(G+f) else ([], [])
    out = leer_contorno(G+'Gerber_BoardOutlineLayer.GKO')
    # todos los ficheros de taladro, no una lista fija de nombres: segun la placa
    # aparecen PTH, vias y NPTH —los agujeros de montaje suelen ser NPTH y son
    # justo lo que hay que ver en una plantilla—
    hs = []
    for f in sorted(os.listdir(G)):
        if f.upper().endswith('.DRL'): hs += leer_taladros(G+f)
    xs = [p for s in out for p in (s[0], s[2])]
    ys = [p for s in out for p in (s[1], s[3])]
    geo = (cobre, silk, out, hs, (min(xs), max(xs), min(ys), max(ys)))

    objs = {1: '<< /Type /Catalog /Pages 2 0 R >>',
            2: '<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>',
            7: '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'}
    for cara, esp, po, co in (('top', False, 3, 4), ('bottom', True, 5, 6)):
        s = pagina(cara, esp, geo, placa).encode('latin-1', 'replace')
        objs[po] = (f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {A4[0]:.3f} {A4[1]:.3f}] '
                    f'/Contents {co} 0 R /Resources << /Font << /F1 7 0 R >> >> >>')
        objs[co] = (f'<< /Length {len(s)} >>\nstream\n', s, '\nendstream')
    buf = bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n')
    pos = {}
    for n in sorted(objs):
        pos[n] = len(buf)
        buf += f'{n} 0 obj\n'.encode()
        v = objs[n]
        buf += (v[0].encode()+v[1]+v[2].encode()) if isinstance(v, tuple) else v.encode()
        buf += b'\nendobj\n'
    xref = len(buf)
    buf += f'xref\n0 {max(objs)+1}\n'.encode() + b'0000000000 65535 f \n'
    for n in range(1, max(objs)+1):
        buf += (f'{pos[n]:010d} 00000 n \n').encode() if n in pos else b'0000000000 65535 f \n'
    buf += f'trailer\n<< /Size {max(objs)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
    open(salida, 'wb').write(buf)
    ancho, alto = geo[4][1]-geo[4][0], geo[4][3]-geo[4][2]
    print(f'{salida}: placa {ancho:.2f} x {alto:.2f} mm, {len(hs)} taladros')
    return 0

if __name__ == '__main__':
    sys.exit(main())
