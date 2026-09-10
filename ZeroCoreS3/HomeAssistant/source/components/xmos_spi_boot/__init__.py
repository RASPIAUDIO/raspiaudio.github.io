import hashlib
from pathlib import Path
import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, spi, text_sensor
from esphome.const import CONF_ID, CONF_RAW_DATA_ID
from esphome.core import CORE, HexInt


DEPENDENCIES = ["i2c", "spi"]
AUTO_LOAD = ["text_sensor"]
CODEOWNERS = ["@RASPIAUDIO"]

xmos_spi_boot_ns = cg.esphome_ns.namespace("xmos_spi_boot")
XMOSSPIBoot = xmos_spi_boot_ns.class_(
    "XMOSSPIBoot", cg.Component, i2c.I2CDevice, spi.SPIDevice
)

CONF_IMAGE_FILE = "image_file"
CONF_SHA256 = "sha256"
CONF_STATUS_SENSOR = "status_sensor"
CONF_XMOS_ADDRESS = "xmos_address"
CONF_EXPECTED_VERSION = "expected_version"
CONF_TRANSFER_BLOCK_NUM = "transfer_block_num"
CONF_USB_RAM_TEST = "usb_ram_test"


def _validate_sha256(value):
    value = cv.string_strict(value)
    if re.fullmatch(r"[0-9a-fA-F]{64}", value) is None:
        raise cv.Invalid("SHA256 must contain exactly 64 hexadecimal characters")
    return value


def _resolve_image(config):
    image_path = Path(CORE.relative_config_path(config[CONF_IMAGE_FILE]))
    if not image_path.is_file():
        raise cv.Invalid(f"XMOS SPI boot image not found: {image_path}")
    image = image_path.read_bytes()
    if not image or len(image) % 4096 != 0:
        raise cv.Invalid(
            f"XMOS SPI boot image size must be a non-zero multiple of 4096, got {len(image)}"
        )
    actual = hashlib.sha256(image).hexdigest()
    if actual.lower() != config[CONF_SHA256].lower():
        raise cv.Invalid(
            f"XMOS SPI boot image SHA256 mismatch: expected {config[CONF_SHA256]}, got {actual}"
        )
    return config


def _validate_version(value):
    value = cv.string_strict(value)
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", value)
    if match is None or any(int(part) > 255 for part in match.groups()):
        raise cv.Invalid("expected_version must be MAJOR.MINOR.PATCH with values from 0 to 255")
    return value


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(XMOSSPIBoot),
            cv.GenerateID(CONF_RAW_DATA_ID): cv.declare_id(cg.uint8),
            cv.Required(CONF_IMAGE_FILE): cv.file_,
            cv.Required(CONF_SHA256): _validate_sha256,
            cv.Required(CONF_EXPECTED_VERSION): _validate_version,
            cv.Required(CONF_STATUS_SENSOR): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_XMOS_ADDRESS, default=0x2C): cv.i2c_address,
            cv.Optional(CONF_TRANSFER_BLOCK_NUM, default=106): cv.int_range(min=0),
            cv.Optional(CONF_USB_RAM_TEST, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x20))
    .extend(spi.spi_device_schema(cs_pin_required=True, default_data_rate="5MHz", default_mode="MODE0")),
    _resolve_image,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    await spi.register_spi_device(var, config)

    image_path = Path(CORE.relative_config_path(config[CONF_IMAGE_FILE]))
    image = image_path.read_bytes()
    image_array = cg.progmem_array(
        config[CONF_RAW_DATA_ID], tuple(HexInt(value) for value in image)
    )
    status = await cg.get_variable(config[CONF_STATUS_SENSOR])
    cg.add(var.set_image(image_array, len(image)))
    cg.add(var.set_expected_sha256(config[CONF_SHA256].lower()))
    version = [int(part) for part in config[CONF_EXPECTED_VERSION].split(".")]
    cg.add(var.set_expected_version(*version))
    cg.add(var.set_status_sensor(status))
    cg.add(var.set_xmos_address(config[CONF_XMOS_ADDRESS]))
    cg.add(var.set_transfer_block_num(config[CONF_TRANSFER_BLOCK_NUM]))
    cg.add(var.set_usb_ram_test(config[CONF_USB_RAM_TEST]))
