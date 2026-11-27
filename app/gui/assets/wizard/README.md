# Wizard illustrations (ADR-0076)

Illustration slots of the wizards (plan/features/0007 § Illustration slots; 0008–0010 add their own). Each slot is a
file `<slot-id>.svg` (or `.png` at 2×) of **480 × 270 logical px**, with an optional dark variant
`<slot-id>-dark.svg`; a slot without a dark variant is shown on the `surface` background. Images contain **no text**:
the caption is a separate translatable text. The files are stored with **Git LFS** (`.gitattributes`:
`app/gui/assets/wizard/*.svg`, `*.png`) and compiled into the QML module `HardwarioMonitor`
(`qrc:/qt/qml/HardwarioMonitor/assets/wizard/`). Replacing a file needs no code change.

`jlink-usb`, `chester-lrw-cable`, `stm32-swd-cable`, `chester-power` and `placeholder` are **placeholders**: a dashed outline frame and the Material Symbol of
the topic (paths from the icons in `../icons`, Apache License 2.0, see `../icons/README.md`), the slot id only in the
SVG `<title>`. `chester-app-cable` and `chester-lte-cable` (light and `-dark`) are **schematic line drawings** (plan
0051, ADR-0380): the CHESTER board outline with both 10-pin programming connectors (APP left, LTE right), the J-Link
flat cable on the connector of the step in the accent colour, the other connector muted. **The connector positions
are schematic and must be checked against a real CHESTER board.** Real photos or art remain a content task for
HARDWARIO (ADR-0076, open question 22); they replace the files without code changes.

| Slot | Shows | Caption source text | Placeholder symbol |
|---|---|---|---|
| `jlink-usb` | J-Link connected to a computer by USB | "Connect the J-Link to your computer with a USB cable." | `probe` |
| `chester-app-cable` | flat cable to the application MCU's programming connector | from the profile guide (`TargetProfile::connectionGuide`, ADR-0077) | schematic (ADR-0380) |
| `chester-lte-cable` | flat cable to the LTE MCU's programming connector | from the profile guide | schematic (ADR-0380) |
| `chester-lrw-cable` | J-Link cable to the programming connector of the CHESTER LoRaWAN module (plan 0040) | from the profile guide | `device` |
| `stm32-swd-cable` | J-Link SWD wires (SWDIO, SWCLK, GND, VTref) to an STM32 chip (plan 0039) | from the profile guide | `device` |
| `chester-power` | CHESTER with batteries / USB-C power | from the profile power hint (`DeviceProfile::powerHintSourceText`) | `flash` |
| `setup-welcome` | CHESTER, a J-Link and a laptop (setup wizard Welcome, plan 0008) | — (decorative) | `placeholder` |
| `placeholder` | neutral frame (fallback for an unknown slot) | — | `placeholder` |

A Qt Quick Test (`tst_wizard.qml`) and a GTest (`WizardIllustrations`) check that every slot of this table and every
illustration slot of a device profile's connection guide resolves to a file.
