# dataset-uploader
Tool for upload CSV file MCU via serial port (or UDP)

## Usage
```
~ uploader -h
Usage: uploader [OPTION]...
Tool for upload CSV file MCU

  -h, --help                Print help and exit
  -V, --version             Print version and exit
  -i, --interface=STRING    interface  (possible values="udp", "serial"
                              default=`serial')
  -d, --dataset=STRING      Dataset file  (default=`./dataset.csv')
  -l, --listen-port=INT     Listen port  (default=`50000')
  -p, --send-port=INT       Send port  (default=`50005')
  -s, --serial-port=STRING  Serial port device  (default=`/dev/ttyACM0')
  -b, --baud-rate=INT       Baud rate  (possible values="9600", "115200",
                              "230400" default=`230400')
      --pause=INT           Pause before start  (default=`0')
```

## Build
To build from source install dependencies:

On macOS:
`brew install libuv gengetopt`

On Linux (Ubuntu as example):
`sudo apt install libuv1-dev gengetopt`

Then just run `make`
