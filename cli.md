## DBCH Command Line Interface

This document describes how to operate the DBCH via its command line interface.
The DBCH has an FTDI chip so its serial port should show up when attached via USB.
Its serial port operates at 115200 baud 8N1 with no flow control.

Most commands are provided by [SiLabs CLI infrastructure](https://docs.silabs.com/zigbee/6.4/af_v2/group-cli).
Where a numerical argument is request, the default is to interpret it as decimal, but prefacing `0x` will change this to hexadecimal.
Where a text string argument is requested, it must be enclosed in double quotes.
The serial interface has been rewritten to run raw DOAP commands and pass anything else to the stock SiLabs CLI.
This rewrite also allows use of the backspace key to correct typos.

### Main Coordinator Functionality

#### network form *channel power PAN-ID*
This command forms a network on the given parameters where power is in dBm.

The sub-GHz band is split into 2 sub-bands: 863 - 876MHz and 915 - 921MHz with 63 and 27 channels respectively where:
* f(c) = 863.25 + 0.2c, c in [0, 62]
* f(c) = 915.35 + 0.2c, c in [0, 26]

The first sub-band is further split into 3 pages where the first (page 28) contains channels 0 - 26, the second (page 29) contains 27 - 34 and the third (page 30) contains 35 - 48.
Page 31 contains the entire 915MHz sub-band.
Ember condenses a page and channel into a single byte where the 3 most significant bits represent the page and the 5 least significant bits represent the channel offset.
The page field is 0 for page 0, 4 for page 28, 5 for page 29, 6 for page 30 and 7 for page 31.

* e.g. `network form 12 0 0x1234` form a HAN on 2.4GHz channel 12 (page 0), at 0dBm and with PAN ID h1234.
* e.g. `network form 128 ...` form a HAN on 863MHz channel 0 (page 28).
* e.g. `network form 154 ...` form a HAN on 863MHz channel 26 (page 28).
* e.g. `network form 160 ...` form a HAN on 863MHz channel 27 (page 29).
* e.g. `network form 167 ...` form a HAN on 863MHz channel 34 (page 29).
* e.g. `network form 192 ...` form a HAN on 863MHz channel 35 (page 30).
* e.g. `network form 205 ...` form a HAN on 863MHz channel 48 (page 30).
* e.g. `network form 224 ...` form a HAN on 915MHz channel 0 (page 31).
* e.g. `network form 236 ...` form a HAN on 915MHz channel 12 (page 31).

#### network leave
This command makes the DBCH leave the HAN it formed.

#### network pjoin *secs*
Permit joining for *secs* number of seconds.
If *secs* is 0, joining is disabled.
If *secs* is 255, joining is enabled forever.

#### network change-channel *chn*
Attempts to change device over to a different channel.

#### changekey network *key*
Change the network key to the 16 byte array provided as an argument.

#### zcl time *utc*
Set time to the value given as UTC.

#### keys print
Show nwk and link keys.

### Additional Functionality

#### chf.auth={*IEEE*,*IC*}
Add a link key for the given IEEE address and install code including CRC.

#### chf.kids
Show child table.

#### chf.perm=i
If set to 1, auto enable permit joining at start up.

#### chf.tpwr[=i]
Get / set tx power where i is signed hexadecimal value in dBm.

#### chf.mtrs
Show meters.

#### chf.mtrs={*MeteringDeviceType*,*PaymentControl*}
Add a new meter.

### Meter Interfaces

todo
