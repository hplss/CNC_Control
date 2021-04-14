The goal of this project is to create an extension for the GRBL controller that is used for CNC operations.
To achieve this, the controller will parse all commands sent via input interface(s), respond to a set of specific commands,
and forward the others to a serial port going to the GRBL device.