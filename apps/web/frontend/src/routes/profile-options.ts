export const profileProperties = [
  { value: 'coffee-dose', label: 'Dosis de café', unit: 'g' },
  { value: 'beverage-mass', label: 'Masa de bebida', unit: 'g' },
  { value: 'brew-ratio', label: 'Relación de preparación', unit: ':1' },
  { value: 'grind-setting', label: 'Posición del molino', unit: 'posición' },
  { value: 'extraction-time', label: 'Tiempo de extracción', unit: 's' },
  { value: 'preinfusion-time', label: 'Preinfusión', unit: 's' },
  { value: 'water-temperature', label: 'Temperatura del agua', unit: '°C' },
  { value: 'pressure', label: 'Presión', unit: 'bar' },
  { value: 'ambient-temperature', label: 'Temperatura ambiente', unit: '°C' },
  { value: 'relative-humidity', label: 'Humedad relativa', unit: '%' },
  { value: 'tds', label: 'TDS', unit: '%' },
  { value: 'brix', label: 'Brix', unit: '°Bx' },
  { value: 'extraction-yield', label: 'Rendimiento', unit: '%' },
  { value: 'thermal-stability', label: 'Estabilidad térmica', unit: '°C' },
]

// El perfil pide el agua sobre el café y la placa la convierte a caldera; solo
// acepta el agua que su caldera medida alcanza (acta v12). Son los mismos números
// que `espresso_profiles/water_range.py`, que el servidor vuelve a comprobar.
export const WATER_TEMPERATURE_RANGE_C = { minimum: 85.72, maximum: 92.97 }

// La placa y Preparar café guardan la presión como `extraction-pressure`; el
// formulario de perfiles, como `pressure`. Las dos se leen igual en pantalla:
// sin esto el código interno salía escrito tal cual.
const propertyAliases: Record<string, string> = { 'extraction-pressure': 'pressure' }

export function profilePropertyLabel(value: string) {
  const known = propertyAliases[value] ?? value
  return profileProperties.find((item) => item.value === known)?.label ?? value
}
