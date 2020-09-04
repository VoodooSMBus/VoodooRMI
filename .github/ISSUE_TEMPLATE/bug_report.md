---
name: Bug report
about: For bug reports
title: ''
labels: ''
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.
If the trackpad is not working, or behaving weirdly, you are expected to provide a Log.

**To Reproduce**
Steps to reproduce the behavior:
1. Go to '...'
2. Click on '....'
3. Scroll down to '....'
4. See error

**Expected behavior**
A clear and concise description of what you expected to happen.

**Log**
The `log` command is not very good if you are trying to get logs from early boot. Generally it's better to add `msgbuf=1048576` to your boot args and use `sudo dmesg | grep -i vrmi > ~/Desktop/log.txt` once booted in. You will want to use the DEBUG version of the driver as it provides more logging info. If you attach a log using the RELEASE version, we may ask you to use the DEBUG version and redo the log. This will provides logs for VoodooRMI from when the device was booted up.
