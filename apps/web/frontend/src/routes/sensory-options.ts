export const sensoryDescriptorGroups = [
  { label: 'Frutales', options: [{ value: 'Citrus', label: 'Cítrico' }, { value: 'RedFruit', label: 'Frutos rojos' }, { value: 'TropicalFruit', label: 'Frutas tropicales' }, { value: 'RipeFruit', label: 'Fruta madura' }, { value: 'DriedFruit', label: 'Fruta seca' }] },
  { label: 'Dulces', options: [{ value: 'Caramel', label: 'Caramelo' }, { value: 'Honey', label: 'Miel' }, { value: 'Panela', label: 'Panela' }, { value: 'Chocolate', label: 'Chocolate' }, { value: 'Vanilla', label: 'Vainilla' }, { value: 'BrownSugar', label: 'Azúcar morena' }] },
  { label: 'Florales', options: [{ value: 'GenericFloral', label: 'Floral' }, { value: 'Jasmine', label: 'Jazmín' }, { value: 'Rose', label: 'Rosa' }] },
  { label: 'Otros', options: [{ value: 'Nuts', label: 'Nueces' }, { value: 'Spices', label: 'Especias' }, { value: 'Cereal', label: 'Cereal' }, { value: 'Herbal', label: 'Herbal' }, { value: 'FermentedDescriptor', label: 'Fermentado' }, { value: 'Cocoa', label: 'Cacao' }, { value: 'Tobacco', label: 'Tabaco' }, { value: 'Earthy', label: 'Terroso' }, { value: 'Woody', label: 'Leñoso' }] },
] as const

// En el orden de la ficha CVA de la SCA (EKO 3.5.0): sus siete atributos
// primero y después los que la CVA no trae y la catación de espresso sí usa.
// El agrado global va aparte, como el «Overall» de la CVA.
export const sensoryAttributes = [
  { value: 'Fragrance', label: 'Fragancia' },
  { value: 'Aroma', label: 'Aroma' },
  { value: 'Flavor', label: 'Sabor' },
  { value: 'Aftertaste', label: 'Retrogusto' },
  { value: 'Acidity', label: 'Acidez' },
  { value: 'Sweetness', label: 'Dulzor' },
  { value: 'Mouthfeel', label: 'Sensación en boca' },
  { value: 'Bitterness', label: 'Amargor' },
  { value: 'Body', label: 'Cuerpo' },
  { value: 'Astringency', label: 'Astringencia' },
  { value: 'Balance', label: 'Balance' },
] as const

// Lo que la ficha CVA marca aparte, hasta dos de cada uno (EKO 3.5.0).
export const basicTastes = [
  { value: 'Salty', label: 'Salado' },
  { value: 'Bitter', label: 'Amargo' },
  { value: 'Sour', label: 'Ácido' },
  { value: 'Umami', label: 'Umami' },
  { value: 'Sweet', label: 'Dulce' },
] as const

export const mouthfeelTextures = [
  { value: 'Rough', label: 'Áspera' },
  { value: 'Smooth', label: 'Suave' },
  { value: 'Oily', label: 'Aceitosa' },
  { value: 'Metallic', label: 'Metálica' },
  { value: 'MouthDrying', label: 'Secante' },
] as const

export const sensoryDirections = [
  { value: 'Increase', label: 'Aumentar' },
  { value: 'Decrease', label: 'Disminuir' },
  { value: 'Improve', label: 'Mejorar' },
] as const

export const sensoryDefects = [
  { value: 'Burnt', label: 'Quemado' },
  { value: 'ExcessiveAstringency', label: 'Astringencia excesiva' },
  { value: 'ExcessiveBitterness', label: 'Amargor excesivo' },
  { value: 'ExcessiveAcidity', label: 'Acidez excesiva' },
  { value: 'InsufficientAcidity', label: 'Acidez insuficiente' },
  { value: 'FermentedDefect', label: 'Fermentado como defecto' },
  { value: 'Vegetal', label: 'Vegetal' },
  { value: 'Ashy', label: 'Ceniza' },
  { value: 'Metallic', label: 'Metálico' },
  { value: 'PersistentUnpleasantTaste', label: 'Sabor desagradable persistente' },
  { value: 'Moldy', label: 'Moho' },
  { value: 'Phenolic', label: 'Fenólico' },
  { value: 'Potato', label: 'Papa' },
] as const

export const sensoryDescriptorValues: ReadonlySet<string> = new Set(
  sensoryDescriptorGroups.flatMap((group) => group.options.map((option) => option.value)),
)

export function sensoryDescriptorLabel(value: string) {
  for (const group of sensoryDescriptorGroups) {
    const option = group.options.find((item) => item.value === value || item.label.toLocaleLowerCase() === value.toLocaleLowerCase())
    if (option) return option.label
  }
  return value
}

export function sensoryDescriptorValue(value: string) {
  for (const group of sensoryDescriptorGroups) {
    const option = group.options.find((item) => item.value === value || item.label.toLocaleLowerCase() === value.toLocaleLowerCase())
    if (option) return option.value
  }
  return value
}

export function sensoryAttributeLabel(value: string) {
  const aliases: Record<string, string> = {
    fragrance: 'Fragancia',
    aroma: 'Aroma',
    mouthfeel: 'Sensación en boca',
    OverallLiking: 'Agrado global',
    flavor: 'Sabor',
    acidity: 'Acidez',
    sweetness: 'Dulzor',
    bitterness: 'Amargor',
    body: 'Cuerpo',
    aftertaste: 'Retrogusto',
    astringency: 'Astringencia',
    balance: 'Balance',
    'overall-liking': 'Agrado general',
  }
  return sensoryAttributes.find((item) => item.value === value)?.label ?? aliases[value] ?? value
}

export function sensoryDirectionLabel(value: string) {
  return sensoryDirections.find((item) => item.value === value)?.label ?? value
}

export function sensoryDefectLabel(value: string) {
  return sensoryDefects.find((item) => item.value === value)?.label ?? value
}

/* El protocolo con que se llena la ficha (protocolo de catación § 6: «referencia
   a este protocolo en cada ficha»). El documento no lleva número de versión: lo
   que lo identifica es su línea de estado, «borrador para aprobar
   (2026-10-19)». Se cita así y se cambia aquí cuando se apruebe; antes la ficha
   decía «Protocolo sensorial piloto v0.2», que no es ningún documento del
   repositorio. */
export const sensoryProtocolReference = 'device/docs/engineering/protocolo-catacion.md · borrador para aprobar (2026-10-19)'
