edmac_test
==========

QEMU EDMAC Synthetic Capture Test.

Exercises the QEMU EDMAC harness by writing directly to EDMAC channel 2
registers, triggering a synthetic Bayer pattern fill, and saving the
captured frame to ML/LOGS/EDMAC_TEST.RAW.

Requires QEMU_EOS_SYNTHETIC_EDMAC=2 environment variable.

:Author: PRISM research
:License: GPL
:Summary: QEMU EDMAC synthetic capture test module
