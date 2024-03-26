# ESP32 7600G SIM - SSL - GPS

This is a sketch written with Arduino IDE that uses the LILYGO T-SIM7600G ESP32 device, which has an integrated GPS and 4G modem.

<img src="https://ae01.alicdn.com/kf/Sdc383797e9b641d5a159c93c2edae029n/LILYGO-TTGO-T-SIM7600-ESP32-LTE-Cat4-1-scheda-di-sviluppo-4G-SIM7600G-H-R2-SIM7600SA.jpg" width="850"/>






The project uses several libraries for connection, GPS usage, SSL certificate handling, etc.

The application initially performs a connection and GPS functionality test. Afterwards, it makes a POST call, with SSL connection, sending GPS location data to a dedicated server every certain number of minutes.

A deep sleep mode has been implemented for the weekends (Saturday afternoon and all day Sunday) to save energy, as this ESP32 can be powered by a battery and solar cells.
