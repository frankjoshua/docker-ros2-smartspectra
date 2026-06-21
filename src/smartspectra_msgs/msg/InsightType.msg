# Insight response category (mirrors SmartSpectra InsightType).
uint8 value

uint8 VITALS = 0     # periodic auto-fired vitals snapshot
uint8 SPEECH = 1     # on-demand, prompt-only request
uint8 COMBINED = 2   # on-demand, prompt plus metrics buffer
