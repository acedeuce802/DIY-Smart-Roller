# DIY-Smart-Roller
Converting a standard cycling roller to a smart roller via FTMS protocol, strong magnets, and a servo

This project uses a sled of magnets, mounted to cycling rollers via linear bearings and controlled by an ESP32 and RC servo motor, in order to increase or decrease resistive power.  As of the inital commit, this project only works in ERG mode, where the cycling app is following a workout (aka power versus time), with hope to add simulation funcionality in the future.  All of my testing and use has been with TrainerRoad thus far.

Neodymium magnets have been used to increase the resistance in cycling rollers for quite some time, however I've only seen them used on a fixed mount, turning them into a "mag resistance trainer" where power increases versus speed.  I stumbled across several projects of attaching steppers to spin bike resistance knobs, making button presses for spin bikes with digital buttons for resistance, etc, which made me think that it must not be too difficult to adapt this to the magnet theory for rollers.  Ever since I got my set of rollers (for $20!), I've been researching off-the-shelf smart rollers, but it quickly comes to a halt when I remember that the "cheap" ones are ~$900, and the expensive ones are $1400-2100.

The general flow of the control is as follows:
  1. ESP32 initializes FTMS BLE service and user selects ESP32 as a controllable smart trainer
  2. User deselects ESP32 as a power meter and chooses their own power meter, which is necessary as the cycling rollers have no power measurement (this can be done at any time prior to cycling)
  3. ESP32 and cycling app perform handshake of BLE messages
  4. User loads a workout on the cycling app and the app sends 0x05 message to ESP32
  5. Every 0.5s, ESP32 receives target power from the cycling app, and then sends that target power right back as "actual power" (Cycling apps won't compensate for power offsets if power isn't spammed back, since this PID function is meant to adjust for the differential between smart trainer reported power and physical power meter reported power)
  6. A hall sensor reads a magnet on the rollers very quickly to measure RPM
  7. An array bilinearly interpolates roller speed and target power to output a desired servo position
  8. The servo position is set and the bikes physical power meter will be read into the cycling app
  9. If there is an offset between physical power and target power, the cycling app will send an updated target power to ESP32, despite target power in the app showing constant

Note: Rollers inherently have more resistance than a spin bike or trainer, so there is a "power floor" which increase as speed increases.  Conversely, there's a "power ceiling" as the magnets can only provide so much resistance.  This ends up giving a tuning band at each speed.  I'll include a picture later but to give an idea, at 10mph the power range is from 90w to 130w, at 150mph the power range is from 140w to 250w, at 20mph the power range is from 190w to 400w, and at 25mph the power range is from 250w to 510w.  This mean you need to be in some gear that'll let the servo work, from what I've found there's about 2-3 gears (on a widely spaced MTB cassette) at a given power that will give enough servo resolution.  For workouts like a sweet spot that is stepping through powers around high end of Z2 and low end of Z3, no shifting is needed.  But for a 30/30 workout where it's 30s above FTP and 30s at 50% FTP, I find that I need 3 upshifts.  I do these upshifts a few seconds prior to target power changing and the power ramp is seamless.

The initial values table (Z) needs characterized, a characterization file will be provided at a later date.  This file will step through the full range of servo positions while measuring RPM and reporting RPM and servo position to the Serial Monitor.  If power input over BLE is added, data analysis becomes easier, but for now I averaged power for each step on Strava.  To log speed and servo position, [PuTTY was used to log over Serial](https://www.eye4software.com/hydromagic/documentation/articles-and-howtos/serial-port-logging/).  There is some math to be done, since the calibration table has speed and target power with nicely spread out inputs, however the data collected has servo position and speed as the inputs.  I didn't document this, however I used some curve fitting in Excel to find the trends, then calculate the numbers to enter into the calibration table.  To fine tune this table, a custom workout file can be created in the cycling app to target powers from the calibration table, spin the pedals to achieve different speeds from the table, then note/log the servo position that allowed actual power to achieve target power after the cycling app adjusts via PID.

This PCB houses the Xiao ESP32-C3.  I mostly chose this board due to it's size, which to be honest was becuase I was using this board for another project where space is more constrained, but it came in handy to be able to tuck the PCB/case on the inside of the roller frame.  The hall sensor for the speed input connects directly to the PCB and will hang out of the 3d printed enclosure, so the case can be mounted to pick up the magnet on the roller directly.  A 10k resistor is needed for hall sensor pull-up and diode is included so the 5V power supply and USB cable can be connected at the same time.  There are two versions of PCB files attached, one with a 6-pin molex connector, with 3-pins for servo and 3-pins for hall sensor and a PCB mounted DC jack.  The other version has pads for the external components, with the intention of hard wiring the servo and DC jack wires, running internal to the frame, mounting the DC jack directly to the frame end cap, and mounting the PCB close enough to the roller for the direct mounted hall sensor.

![image](https://github.com/acedeuce802/DIY-Smart-Roller/assets/37642264/7512d4a8-5cb4-4c3c-a02b-d1fd64c8bfd3)

![image](https://github.com/acedeuce802/DIY-Smart-Roller/assets/37642264/15237561-8b81-457e-9137-f694e0075fa9)

Molex Mini Fit Jr Pinout:
1. Servo 5V
2. Servo GND
3. Servo Signal
4. Hall 5V
5. Hall GND
6. Hall Signal




Parts list:
Coming soon

Wiring:
Coming soon

Instructions/Assembly:
Coming soon

FTMS code to interface between the ESP32 and cycling training apps is based on the instructable [Zwift Interface for Dumb Turbo Trainer](https://www.instructables.com/Zwift-Interface-for-Dumb-Turbo-Trainer/)

