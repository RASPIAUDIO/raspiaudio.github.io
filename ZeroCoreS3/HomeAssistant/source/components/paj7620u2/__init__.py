import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import i2c, text_sensor
from esphome.const import CONF_ID, CONF_TRIGGER_ID


DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["text_sensor"]
CODEOWNERS = ["@RASPIAUDIO"]

paj7620u2_ns = cg.esphome_ns.namespace("paj7620u2")
PAJ7620U2 = paj7620u2_ns.class_(
    "PAJ7620U2", cg.PollingComponent, i2c.I2CDevice
)

RightGestureTrigger = paj7620u2_ns.class_("RightGestureTrigger", automation.Trigger)
LeftGestureTrigger = paj7620u2_ns.class_("LeftGestureTrigger", automation.Trigger)
UpGestureTrigger = paj7620u2_ns.class_("UpGestureTrigger", automation.Trigger)
DownGestureTrigger = paj7620u2_ns.class_("DownGestureTrigger", automation.Trigger)
ForwardGestureTrigger = paj7620u2_ns.class_("ForwardGestureTrigger", automation.Trigger)
BackwardGestureTrigger = paj7620u2_ns.class_("BackwardGestureTrigger", automation.Trigger)

CONF_LAST_GESTURE = "last_gesture"
CONF_ON_RIGHT = "on_right"
CONF_ON_LEFT = "on_left"
CONF_ON_UP = "on_up"
CONF_ON_DOWN = "on_down"
CONF_ON_FORWARD = "on_forward"
CONF_ON_BACKWARD = "on_backward"

TRIGGERS = {
    CONF_ON_RIGHT: RightGestureTrigger,
    CONF_ON_LEFT: LeftGestureTrigger,
    CONF_ON_UP: UpGestureTrigger,
    CONF_ON_DOWN: DownGestureTrigger,
    CONF_ON_FORWARD: ForwardGestureTrigger,
    CONF_ON_BACKWARD: BackwardGestureTrigger,
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PAJ7620U2),
            cv.Optional(CONF_LAST_GESTURE): cv.use_id(text_sensor.TextSensor),
            **{
                cv.Optional(key): automation.validate_automation(
                    {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(trigger_type)}
                )
                for key, trigger_type in TRIGGERS.items()
            },
        }
    )
    .extend(cv.polling_component_schema("20ms"))
    .extend(i2c.i2c_device_schema(0x73))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    if CONF_LAST_GESTURE in config:
        last_gesture = await cg.get_variable(config[CONF_LAST_GESTURE])
        cg.add(var.set_last_gesture_sensor(last_gesture))

    for key, trigger_type in TRIGGERS.items():
        for conf in config.get(key, []):
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
            await automation.build_automation(trigger, [], conf)
