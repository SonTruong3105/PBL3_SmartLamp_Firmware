# My Custom Board

This board configuration targets ESP32-S3 and uses the GPIO assignments in
`config.h`.

## Audio wiring

The two INMP441 modules use standard stereo I2S slots, not TDM. Connect both
microphones to BCLK GPIO5 and WS GPIO4, strap one microphone's L/R pin low and
the other's high, and connect both SD outputs to GPIO6. The microphones drive
the shared SD wire in separate left/right slots. Do not connect the two SD
outputs together unless their L/R pins select opposite slots.

The MAX98357A uses BCLK GPIO15, LRC GPIO16, DIN GPIO17, and SD/enable GPIO18.
The firmware drives GPIO18 high to enable the amplifier. The current audio
driver reads both microphone slots as stereo input.

## Display and sensors

- The ST7789V 240x320 SPI display uses SCLK GPIO38, MOSI GPIO39, RST GPIO40,
  DC GPIO19, and CS GPIO20. The selected orientation is landscape. The top
  rows show local time and temperature/humidity, with chat messages below;
  the display uses the WeChat-style chat layout. The display module's
  backlight is assumed to be powered by the module itself.
- Time, temperature, and humidity refresh every five seconds. Time remains
  `--:--` until system time is synchronized, and unavailable sensor readings
  appear as `--`.
- GPIO19/20 are also the ESP32-S3 native USB D-/D+ pins. Assigning them to the
  display prevents native USB Serial/JTAG or USB OTG use while this display is
  connected; use UART for flashing and debug logging.
- AHT20 and BH1750 share I2C on SDA GPIO41 and SCL GPIO42. Their 7-bit
  addresses are 0x38 and 0x23; the bus runs at 100 kHz and logs which device
  or initialization command fails. A missing BH1750 does not prevent AHT20
  temperature/humidity readings; illuminance is reported as unavailable.
- GP2Y1010 uses the active-low LED control on GPIO9 and ADC1 channel 1
  (ESP32-S3 GPIO2). Keep the sensor's analog output within the ESP32-S3 ADC
  input range.
- LD2420 UART uses ESP TX GPIO47 and RX GPIO48 at 115200 baud. GPIO14 reads
  the radar's digital presence output. The UART distance parser expects the
  LD2420 energy-report frame format; configure the sensor accordingly.

## Controls and MCP tools

GPIO13 and GPIO12 adjust speaker volume; GPIO8 toggles chat state and enters
Wi-Fi setup mode when clicked while the device is starting. The controls use
active-low button inputs with the button component's internal pull-ups.

The device exposes the following board tools through MCP:

- `self.environment.get_readings` — AHT20 temperature/humidity and BH1750 lux.
- `self.environment.get_dust` — GP2Y1010 ADC raw value and calibrated
  millivolts when ADC calibration is available.
- `self.environment.get_presence` — LD2420 digital presence and most recent
  fresh UART distance reading.
- `self.lights.set_warm_cool` / `self.lights.get_state` — independent PWM
  brightness for the warm and cool LED channels.
