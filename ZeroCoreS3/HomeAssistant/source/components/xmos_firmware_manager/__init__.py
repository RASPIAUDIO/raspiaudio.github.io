import hashlib
from pathlib import Path
import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, i2c, sensor, spi, text_sensor
from esphome.const import CONF_ID, CONF_RAW_DATA_ID
from esphome.core import CORE, HexInt


DEPENDENCIES = ["i2c", "spi", "voice_dsp_plus"]
AUTO_LOAD = ["binary_sensor", "sensor", "sha256", "text_sensor"]
CODEOWNERS = ["@RASPIAUDIO"]

manager_ns = cg.esphome_ns.namespace("xmos_firmware_manager")
XMOSFirmwareManager = manager_ns.class_(
    "XMOSFirmwareManager", cg.Component, i2c.I2CDevice, spi.SPIDevice
)

CONF_CONTROLLER_ID = "controller_id"
CONF_UPGRADE_IMAGE = "upgrade_image"
CONF_UPGRADE_SHA256 = "upgrade_sha256"
CONF_SPI_BOOT_IMAGE = "spi_boot_image"
CONF_SPI_BOOT_SHA256 = "spi_boot_sha256"
CONF_SPI_TRANSFER_BLOCK_NUM = "spi_transfer_block_num"
CONF_QSPI_CAPACITY_BYTES = "qspi_capacity_bytes"
CONF_EXPECTED_VERSION = "expected_version"
CONF_STATUS_SENSOR = "status_sensor"
CONF_PROGRESS_SENSOR = "progress_sensor"
CONF_BUTTON_SENSOR = "button_sensor"
CONF_XMOS_ADDRESS = "xmos_address"
CONF_DIAGNOSTIC_PASSIVE = "diagnostic_passive"
CONF_UPGRADE_RAW_DATA_ID = "upgrade_raw_data_id"
CONF_SPI_RAW_DATA_ID = "spi_raw_data_id"


def _sha256(value):
    value = cv.string_strict(value).lower()
    if re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise cv.Invalid("SHA256 must contain exactly 64 hexadecimal characters")
    return value


def _version(value):
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", cv.string_strict(value))
    if match is None:
        raise cv.Invalid("expected_version must use MAJOR.MINOR.PATCH")
    parts = tuple(int(part) for part in match.groups())
    if any(part > 255 for part in parts):
        raise cv.Invalid("version components must fit in uint8")
    return parts


def _validate_images(config):
    for path_key, hash_key, name in (
        (CONF_UPGRADE_IMAGE, CONF_UPGRADE_SHA256, "upgrade"),
        (CONF_SPI_BOOT_IMAGE, CONF_SPI_BOOT_SHA256, "SPI boot"),
    ):
        path = Path(CORE.relative_config_path(config[path_key]))
        if not path.is_file():
            raise cv.Invalid(f"XMOS {name} image not found: {path}")
        data = path.read_bytes()
        if not data:
            raise cv.Invalid(f"XMOS {name} image is empty")
        if path_key == CONF_SPI_BOOT_IMAGE and len(data) % 4096:
            raise cv.Invalid("XMOS SPI boot image must be a multiple of 4096 bytes")
        actual = hashlib.sha256(data).hexdigest()
        if actual != config[hash_key]:
            raise cv.Invalid(
                f"XMOS {name} SHA256 mismatch: expected {config[hash_key]}, got {actual}"
            )
    upgrade_size = Path(CORE.relative_config_path(config[CONF_UPGRADE_IMAGE])).stat().st_size
    if config[CONF_QSPI_CAPACITY_BYTES] < upgrade_size:
        raise cv.Invalid(
            f"qspi_capacity_bytes={config[CONF_QSPI_CAPACITY_BYTES]} is smaller than "
            f"the {upgrade_size}-byte upgrade image"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(XMOSFirmwareManager),
            cv.GenerateID(CONF_UPGRADE_RAW_DATA_ID): cv.declare_id(cg.uint8),
            cv.GenerateID(CONF_SPI_RAW_DATA_ID): cv.declare_id(cg.uint8),
            cv.Required(CONF_CONTROLLER_ID): cv.use_id(
                cg.esphome_ns.namespace("voice_dsp_plus").class_("VoiceDSPPlus")
            ),
            cv.Required(CONF_UPGRADE_IMAGE): cv.file_,
            cv.Required(CONF_UPGRADE_SHA256): _sha256,
            cv.Required(CONF_SPI_BOOT_IMAGE): cv.file_,
            cv.Required(CONF_SPI_BOOT_SHA256): _sha256,
            cv.Required(CONF_SPI_TRANSFER_BLOCK_NUM): cv.int_range(min=1),
            cv.Required(CONF_QSPI_CAPACITY_BYTES): cv.int_range(min=1),
            cv.Required(CONF_EXPECTED_VERSION): _version,
            cv.Required(CONF_STATUS_SENSOR): cv.use_id(text_sensor.TextSensor),
            cv.Required(CONF_PROGRESS_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_BUTTON_SENSOR): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_XMOS_ADDRESS, default=0x2C): cv.i2c_address,
            cv.Optional(CONF_DIAGNOSTIC_PASSIVE, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x20))
    .extend(
        spi.spi_device_schema(
            cs_pin_required=True, default_data_rate="5MHz", default_mode="MODE0"
        )
    ),
    _validate_images,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    await spi.register_spi_device(var, config)

    controller = await cg.get_variable(config[CONF_CONTROLLER_ID])
    status = await cg.get_variable(config[CONF_STATUS_SENSOR])
    progress = await cg.get_variable(config[CONF_PROGRESS_SENSOR])
    button = await cg.get_variable(config[CONF_BUTTON_SENSOR])

    upgrade = Path(CORE.relative_config_path(config[CONF_UPGRADE_IMAGE])).read_bytes()
    spi_boot = Path(CORE.relative_config_path(config[CONF_SPI_BOOT_IMAGE])).read_bytes()
    upgrade_array = cg.progmem_array(
        config[CONF_UPGRADE_RAW_DATA_ID], tuple(HexInt(value) for value in upgrade)
    )
    spi_array = cg.progmem_array(
        config[CONF_SPI_RAW_DATA_ID], tuple(HexInt(value) for value in spi_boot)
    )

    cg.add(var.set_controller(controller))
    cg.add(var.set_upgrade_image(upgrade_array, len(upgrade)))
    cg.add(var.set_upgrade_sha256(config[CONF_UPGRADE_SHA256]))
    cg.add(var.set_spi_boot_image(spi_array, len(spi_boot)))
    cg.add(var.set_spi_boot_sha256(config[CONF_SPI_BOOT_SHA256]))
    cg.add(var.set_spi_transfer_block_num(config[CONF_SPI_TRANSFER_BLOCK_NUM]))
    cg.add(var.set_qspi_capacity_bytes(config[CONF_QSPI_CAPACITY_BYTES]))
    version = config[CONF_EXPECTED_VERSION]
    cg.add(var.set_expected_version(version[0], version[1], version[2]))
    cg.add(var.set_status_sensor(status))
    cg.add(var.set_progress_sensor(progress))
    cg.add(var.set_button_sensor(button))
    cg.add(var.set_xmos_address(config[CONF_XMOS_ADDRESS]))
    cg.add(var.set_diagnostic_passive(config[CONF_DIAGNOSTIC_PASSIVE]))
