# DAQ acquire

Small utility to acquire samples with Comedi-supported DAQ cards.

## Compilation

You need to install comedilib and autotools. Then, simply run these commands:

```bash
git clone https://github.com/Enchufa2/daq_acquire.git
cd daq_acquire
autoreconf --install
./configure
make
```
