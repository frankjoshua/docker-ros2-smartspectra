# Response to an insight request (mirrors SmartSpectra Insight).
# In the SDK `result` is a oneof: exactly one of analysis/error is populated.
int32 request_id      # client-generated correlation ID
string processed_at   # ISO timestamp
string analysis       # set on success (empty otherwise)
string error          # set on failure (empty otherwise)
InsightType type
