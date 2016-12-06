# DAQ acquire

Small utility to acquire samples with Comedi-supported DAQ cards.

## Compilation

You need to install comedilib and autotools. Then, simply run these commands:

```bash
git clone https://github.com/Enchufa2/daq-acquire.git
cd daq-acquire
autoreconf --install
./configure
make
```
