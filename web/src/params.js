export const PARAM_SCHEMA_VERSION = 1;

export const PARAM_DESCRIPTORS = [
  { id: 0, stableId: 'inputGain', displayName: 'Input', unit: 'x', minValue: 0, maxValue: 4, step: 0.01, defaultValue: 1, role: 'primary' },
  { id: 1, stableId: 'outputGain', displayName: 'Output', unit: 'x', minValue: 0, maxValue: 4, step: 0.01, defaultValue: 1, role: 'primary' },
  { id: 2, stableId: 'dryGain', displayName: 'Dry', unit: 'x', minValue: 0, maxValue: 2, step: 0.01, defaultValue: 0.83, role: 'advanced' },
  { id: 3, stableId: 'wetDirectGain', displayName: 'Wet Direct', unit: 'x', minValue: 0, maxValue: 2, step: 0.01, defaultValue: 0.5, role: 'advanced' },
  { id: 4, stableId: 'wetCrossGain', displayName: 'Wet Cross', unit: 'x', minValue: 0, maxValue: 2, step: 0.01, defaultValue: 0.35, role: 'advanced' },
  { id: 5, stableId: 'baseDelayMs', displayName: 'Base Delay', unit: 'ms', minValue: 1, maxValue: 20, step: 0.01, defaultValue: 7, role: 'advanced' },
  { id: 6, stableId: 'depthMs', displayName: 'Depth', unit: 'ms', minValue: 0, maxValue: 6, step: 0.01, defaultValue: 0.9, role: 'advanced' },
  { id: 7, stableId: 'rateHz', displayName: 'Rate', unit: 'Hz', minValue: 0.01, maxValue: 4, step: 0.01, defaultValue: 0.25, role: 'advanced' },
  { id: 8, stableId: 'hpfHz', displayName: 'Low Focus', unit: 'Hz', minValue: 20, maxValue: 400, step: 1, defaultValue: 120, role: 'advanced' },
  { id: 9, stableId: 'lpfHz', displayName: 'Color', unit: 'Hz', minValue: 2000, maxValue: 12000, step: 1, defaultValue: 8000, role: 'primary' },
  { id: 10, stableId: 'analogAmount', displayName: 'Analog', unit: '', minValue: 0, maxValue: 1, step: 0.01, defaultValue: 0.35, role: 'primary' },
  { id: 11, stableId: 'companderAmount', displayName: 'Compander', unit: '', minValue: 0, maxValue: 1, step: 0.01, defaultValue: 0.35, role: 'advanced' },
  { id: 12, stableId: 'width', displayName: 'Width', unit: '', minValue: 0, maxValue: 2, step: 0.01, defaultValue: 1, role: 'primary' }
];

export const PARAM_IDS = Object.freeze(Object.fromEntries(
  PARAM_DESCRIPTORS.map((param) => [param.stableId, param.id])
));

export const PARAM_NAMES = PARAM_DESCRIPTORS.map((param) => param.stableId);

export const PARAM_DEFAULTS = PARAM_DESCRIPTORS.map((param) => param.defaultValue);

export function getParamDescriptor(stableId) {
  return PARAM_DESCRIPTORS.find((param) => param.stableId === stableId) ?? null;
}
