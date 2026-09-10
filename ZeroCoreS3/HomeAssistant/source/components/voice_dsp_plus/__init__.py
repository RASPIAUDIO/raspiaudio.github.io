import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, i2c, sensor, text_sensor
from esphome.const import CONF_ID


DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]
CODEOWNERS = ["@RASPIAUDIO"]

voice_dsp_plus_ns = cg.esphome_ns.namespace("voice_dsp_plus")
VoiceDSPPlus = voice_dsp_plus_ns.class_(
    "VoiceDSPPlus", cg.PollingComponent, i2c.I2CDevice
)

CONF_JACK_SENSOR = "jack_sensor"
CONF_SQUARE_SENSOR = "square_sensor"
CONF_XMOS_STATUS = "xmos_status"
CONF_DSP_STATUS = "dsp_status"
CONF_AEC_STATUS = "aec_status"
CONF_DOA_SENSOR = "doa_sensor"
CONF_DOA_ENERGY_SENSOR = "doa_energy_sensor"
CONF_MIC_D0_LEVEL_SENSOR = "mic_d0_level_sensor"
CONF_MIC_D1_LEVEL_SENSOR = "mic_d1_level_sensor"
CONF_MIC_D2_LEVEL_SENSOR = "mic_d2_level_sensor"
CONF_MIC_D3_LEVEL_SENSOR = "mic_d3_level_sensor"
CONF_MIC_DIAGNOSTIC_STATUS = "mic_diagnostic_status"
CONF_XMOS_ADDRESS = "xmos_address"
CONF_DAC_ADDRESS = "dac_address"


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(VoiceDSPPlus),
            cv.Required(CONF_JACK_SENSOR): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_SQUARE_SENSOR): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_XMOS_STATUS): cv.use_id(text_sensor.TextSensor),
            cv.Required(CONF_DSP_STATUS): cv.use_id(text_sensor.TextSensor),
            cv.Required(CONF_AEC_STATUS): cv.use_id(text_sensor.TextSensor),
            cv.Required(CONF_DOA_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_DOA_ENERGY_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_MIC_D0_LEVEL_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_MIC_D1_LEVEL_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_MIC_D2_LEVEL_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_MIC_D3_LEVEL_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_MIC_DIAGNOSTIC_STATUS): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_XMOS_ADDRESS, default=0x2C): cv.i2c_address,
            cv.Optional(CONF_DAC_ADDRESS, default=0x18): cv.i2c_address,
        }
    )
    .extend(cv.polling_component_schema("200ms"))
    .extend(i2c.i2c_device_schema(0x20))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    jack = await cg.get_variable(config[CONF_JACK_SENSOR])
    square = await cg.get_variable(config[CONF_SQUARE_SENSOR])
    status = await cg.get_variable(config[CONF_XMOS_STATUS])
    dsp_status = await cg.get_variable(config[CONF_DSP_STATUS])
    aec_status = await cg.get_variable(config[CONF_AEC_STATUS])
    doa_sensor = await cg.get_variable(config[CONF_DOA_SENSOR])
    doa_energy_sensor = await cg.get_variable(config[CONF_DOA_ENERGY_SENSOR])
    mic_d0_level_sensor = await cg.get_variable(config[CONF_MIC_D0_LEVEL_SENSOR])
    mic_d1_level_sensor = await cg.get_variable(config[CONF_MIC_D1_LEVEL_SENSOR])
    mic_d2_level_sensor = await cg.get_variable(config[CONF_MIC_D2_LEVEL_SENSOR])
    mic_d3_level_sensor = await cg.get_variable(config[CONF_MIC_D3_LEVEL_SENSOR])
    mic_diagnostic_status = await cg.get_variable(config[CONF_MIC_DIAGNOSTIC_STATUS])
    cg.add(var.set_jack_sensor(jack))
    cg.add(var.set_square_sensor(square))
    cg.add(var.set_xmos_status_sensor(status))
    cg.add(var.set_dsp_status_sensor(dsp_status))
    cg.add(var.set_aec_status_sensor(aec_status))
    cg.add(var.set_doa_sensor(doa_sensor))
    cg.add(var.set_doa_energy_sensor(doa_energy_sensor))
    cg.add(var.set_mic_level_sensor(0, mic_d0_level_sensor))
    cg.add(var.set_mic_level_sensor(1, mic_d1_level_sensor))
    cg.add(var.set_mic_level_sensor(2, mic_d2_level_sensor))
    cg.add(var.set_mic_level_sensor(3, mic_d3_level_sensor))
    cg.add(var.set_mic_diagnostic_status_sensor(mic_diagnostic_status))
    cg.add(var.set_xmos_address(config[CONF_XMOS_ADDRESS]))
    cg.add(var.set_dac_address(config[CONF_DAC_ADDRESS]))
