# Pocket Sunrise
[Japanese README](/README.md)

A compact device that allows you to wake up in the morning with natural light.

Here, we are sharing the Arduino sketches for the firmware and the PCB design (KiCad).
- Project: https://yasunori.jp/pocket-sunrise
- Enclosure parts: https://grabcad.com/library/pocket-sunrise-1
- Video: https://youtu.be/eCHGJ3ehzJc

## Candlelight mode

Press all three buttons together to toggle candlelight mode- the LED gently flickers near full brightness, regardless of the timer state.

## Electronic Components

The necessary electronic parts are summarized in this spreadsheet. You can purchase all of them at Akizuki Denshi.
→ [Pocket Sunrise Electronic Components](https://docs.google.com/spreadsheets/d/1AGpVGOaxi01ax8kF4fcREE8NY4uzkjkZFQBg4IJ91os/edit?usp=sharing)

## PCB

I ordered from PCBGOGO, but I think any manufacturer is fine. I ordered 5 boards, and including shipping (and an initial order discount), it was only 1,200 yen.

## PCB Assembly

Please refer to the following photos for assembly. Points to note:

- While the LEDs are operated within their rated specifications and the heat generation from the resistors should be within acceptable limits, there is still a possibility of abnormal heat generation in unforeseen cases. Therefore, please make sure to be nearby when using it. Use at your own risk.
- Use only some of the 300Ω resistors for the 7-segment display, and short-circuit the rest (this design allows you to change the orientation of the 7-segment display later).
- Although the 25Ω current limiting resistor is well within the allowable range for the LED's rated current, the cement resistor generated significant heat. To reduce heat generation, connect 50Ω resistors in parallel and attach heat sinks. If you assemble as shown in the images, it should fit inside the enclosure parts (of course, using a proper LED driver would reduce heat, but since this was my first PCB design, I prioritized a simple circuit).
- You may not be able to upload sketches with the Arduino Nano inserted into the PCB. If you can't upload successfully, please remove it once and then upload.
- Connect the LED to the PCB with about 10 cm of wire. Flexible wire is easier to insert into the enclosure parts.
- It can be assembled with four M3 nuts and bolts, 12 mm in length. Attaching rubber feet to the bottom will prevent slipping.

![](/photos/all.jpg)
![](/photos/board_1.jpg)
![](/photos/board_2.jpg)
![](/photos/LED.jpg)
