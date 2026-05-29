LUX_TH: float = 600.0       # ADC threshold (0–1023) — don't turn lights on above this
ID_TIMEOUT: float = 20.0     # seconds to identify user after door opens
USER_TIMEOUT: float = 15.0          # seconds without heartbeat → mark user absent
USER_WATCHDOG_INTERVAL: float = 5.0 # how often the watchdog checks

MQTT_BROKER: str = "localhost"
MQTT_PORT: int = 1883

WEBAPP_HOST: str = "0.0.0.0"
WEBAPP_PORT: int = 8080

# MQTT topics
TOPIC_DOOR = "room/door"
TOPIC_PRESENCE = "room/presence"
TOPIC_LUX = "room/lux"
TOPIC_LIGHT_STATE = "room/light/state"
TOPIC_USER_DETECTED = "room/user/detected"
TOPIC_LIGHT_CMD = "room/light/cmd"
TOPIC_STATE = "room/state"
TOPIC_WELCOME = "room/welcome"
